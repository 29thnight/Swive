#include "ss_vm.hpp"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include <iostream>
#include <iomanip>

namespace swiftscript {

    // Fast I/O initialization
    static struct FastIO {
        FastIO() {
            std::ios_base::sync_with_stdio(false);
            std::cin.tie(nullptr);
            std::cout.tie(nullptr);
        }
    } fast_io_init;

    VM::VM(VMConfig config)
        : config_(config) {
        stack_.reserve(config_.initial_stack_size);
    }

    VM::~VM() {
        // First, drain the deferred releases queue
        is_collecting_ = true;
        RC::process_deferred_releases(this);
        // Process any newly-deferred objects from child releases
        while (!deferred_releases_.empty()) {
            RC::process_deferred_releases(this);
        }
        is_collecting_ = false;

        // Clean up all remaining objects in the linked list
        Object* obj = objects_head_;
        while (obj) {
            Object* next = obj->next;

            // Mark as dead and nil out weak references
            obj->rc.is_dead = true;
            RC::nil_weak_refs(obj);

            delete obj;
            obj = next;
        }
        objects_head_ = nullptr;

        if (config_.enable_debug) {
            print_stats();
        }
    }

    void VM::push(Value val) {
        if (stack_.size() >= config_.max_stack_size) {
            throw std::runtime_error("Stack overflow");
        }

        // Handle RC for object values
        if (val.is_object() && val.ref_type() == RefType::Strong) {
            RC::retain(val.as_object());
            stats_.retain_count++;
            record_rc_operation();
        }

        stack_.push_back(val);
    }

    Value VM::pop() {
        if (stack_.empty()) {
            throw std::runtime_error("Stack underflow");
        }

        Value val = stack_.back();
        stack_.pop_back();

        // Handle RC for object values
        if (val.is_object() && val.ref_type() == RefType::Strong) {
            RC::release(this, val.as_object());
            stats_.release_count++;
            record_rc_operation();
        }

        return val;
    }

    Value VM::peek(size_t offset) const {
        if (offset >= stack_.size()) {
            throw std::runtime_error("Stack peek out of bounds");
        }
        return stack_[stack_.size() - 1 - offset];
    }

    void VM::set_global(const std::string& name, Value val) {
        // Release old value if exists
        auto it = globals_.find(name);
        if (it != globals_.end()) {
            if (it->second.is_object() && it->second.ref_type() == RefType::Strong) {
                RC::release(this, it->second.as_object());
                record_rc_operation();
            }
        }

        // Retain new value
        if (val.is_object() && val.ref_type() == RefType::Strong) {
            RC::retain(val.as_object());
            stats_.retain_count++;
            record_rc_operation();
        }

        globals_[name] = val;
    }

    Value VM::get_global(const std::string& name) const {
        auto it = globals_.find(name);
        if (it == globals_.end()) {
            return Value::undefined();
        }
        return it->second;
    }

    bool VM::has_global(const std::string& name) const {
        return globals_.find(name) != globals_.end();
    }

    void VM::add_deferred_release(Object* obj) {
        deferred_releases_.push_back(obj);
    }

    void VM::remove_from_objects_list(Object* obj) {
        if (!objects_head_) return;

        // Special case: object is head
        if (objects_head_ == obj) {
            objects_head_ = obj->next;
            return;
        }

        // Find and remove from linked list
        Object* current = objects_head_;
        while (current->next) {
            if (current->next == obj) {
                current->next = obj->next;
                return;
            }
            current = current->next;
        }
    }

    void VM::run_cleanup() {
        if (is_collecting_ || deferred_releases_.empty()) {
            return;
        }
        is_collecting_ = true;
        RC::process_deferred_releases(this);
        is_collecting_ = false;
    }

    void VM::collect_if_needed() {
        if (!is_collecting_ &&
            static_cast<size_t>(rc_operations_) >= config_.deferred_cleanup_threshold) {
            run_cleanup();
            rc_operations_ = 0;
        }
    }

    void VM::record_rc_operation() {
        ++rc_operations_;
        collect_if_needed();
    }

    void VM::record_deallocation(const Object& obj) {
        stats_.total_freed += obj.tracked_size;
        stats_.current_objects--;
    }

    void VM::record_allocation_delta(Object& obj, size_t new_size) {
        if (new_size > obj.tracked_size) {
            stats_.total_allocated += (new_size - obj.tracked_size);
        } else if (new_size < obj.tracked_size) {
            stats_.total_freed += (obj.tracked_size - new_size);
        }
        obj.tracked_size = new_size;
    }

    void VM::print_stats() const {
        int64_t leaked = static_cast<int64_t>(stats_.total_allocated)
                       - static_cast<int64_t>(stats_.total_freed);
        std::cout << "\n=== SwiftScript VM Statistics ===\n";
        std::cout << "Total Allocated:  " << std::setw(10) << stats_.total_allocated << " bytes\n";
        std::cout << "Total Freed:      " << std::setw(10) << stats_.total_freed << " bytes\n";
        std::cout << "Current Objects:  " << std::setw(10) << stats_.current_objects << "\n";
        std::cout << "Peak Objects:     " << std::setw(10) << stats_.peak_objects << "\n";
        std::cout << "Total Retains:    " << std::setw(10) << stats_.retain_count << "\n";
        std::cout << "Total Releases:   " << std::setw(10) << stats_.release_count << "\n";
        std::cout << "Memory Leaked:    " << std::setw(10) << leaked << " bytes\n";
        std::cout << "=================================\n";
    }

    void VM::execute_deinit(InstanceObject* inst, Value deinit_method) {
        if (!deinit_method.is_object()) return;
        
        Object* method_obj = deinit_method.as_object();
        FunctionObject* func = nullptr;
        ClosureObject* closure = nullptr;
        
        if (method_obj->type == ObjectType::Closure) {
            closure = static_cast<ClosureObject*>(method_obj);
            func = closure->function;
        } else if (method_obj->type == ObjectType::Function) {
            func = static_cast<FunctionObject*>(method_obj);
        }
        
        if (!func || !func->chunk) return;
        
        // Save current execution state
        const Chunk* saved_chunk = chunk_;
        size_t saved_ip = ip_;
        size_t saved_stack_size = stack_.size();
        size_t saved_frames = call_frames_.size();
        
        try {
            // Push self (the instance being deallocated)
            push(Value::from_object(inst));
            
            // Setup call frame for deinit
            call_frames_.emplace_back(
                saved_stack_size + 1,  // stack_base (after self)
                0,                      // return_address (not used)
                chunk_,                 // saved chunk
                "deinit",              // function name
                closure,                // closure (if any)
                false                   // is_initializer
            );
            
            // Switch to deinit's chunk
            chunk_ = func->chunk.get();
            ip_ = 0;
            
            // Execute deinit bytecode until OP_RETURN
            while (ip_ < chunk_->code.size()) {
                OpCode op = static_cast<OpCode>(read_byte());
                
                if (op == OpCode::OP_RETURN) {
                    break;  // Exit deinit execution
                }
                
                // Execute the opcode (simplified - just run the main loop step)
                // We need to handle opcodes manually or call a helper
                switch (op) {
                    case OpCode::OP_CONSTANT: {
                        push(read_constant());
                        break;
                    }
                    case OpCode::OP_STRING: {
                        const std::string& str = read_string();
                        auto* obj = allocate_object<StringObject>(str);
                        push(Value::from_object(obj));
                        break;
                    }
                    case OpCode::OP_GET_LOCAL: {
                        uint16_t slot = read_short();
                        size_t base = call_frames_.back().stack_base;
                        push(stack_[base + slot - 1]);  // -1 because self is at base-1
                        break;
                    }
                    case OpCode::OP_GET_PROPERTY: {
                        const std::string& name = read_string();
                        Value obj_val = pop();
                        push(get_property(obj_val, name));
                        break;
                    }
                    case OpCode::OP_PRINT: {
                        Value val = pop();
                        std::cout << val.to_string() << '\n';
                        break;
                    }
                    case OpCode::OP_POP:
                        pop();
                        break;
                    case OpCode::OP_NIL:
                        push(Value::null());
                        break;
                    // Add other necessary opcodes...
                    default:
                        // For now, skip unknown opcodes
                        break;
                }
            }
            
        } catch (...) {
            // Ignore errors in deinit
        }
        
        // Restore execution state
        chunk_ = saved_chunk;
        ip_ = saved_ip;
        
        // Restore stack
        while (stack_.size() > saved_stack_size) {
            pop();
        }
        
        // Restore call frames
        while (call_frames_.size() > saved_frames) {
            call_frames_.pop_back();
        }
    }

    Value VM::interpret(const std::string& source) {
        Lexer lexer(source);
        auto tokens = lexer.tokenize_all();
        Parser parser(std::move(tokens));
        auto program = parser.parse();
        Compiler compiler;
        Chunk chunk = compiler.compile(program);
        return execute(chunk);
    }

    Value VM::execute(const Chunk& chunk) {
        chunk_ = &chunk;
        ip_ = 0;
        stack_.clear();
        call_frames_.clear();
        return run();
    }

    Value VM::run() {
        for (;;) {
            OpCode op = static_cast<OpCode>(read_byte());
            switch (op) {
                case OpCode::OP_CONSTANT: {
                    push(read_constant());
                    break;
                }
                case OpCode::OP_STRING: {
                    const std::string& str = read_string();
                    auto* obj = allocate_object<StringObject>(str);
                    push(Value::from_object(obj));
                    break;
                }
                case OpCode::OP_NIL:
                    push(Value::null());
                    break;
                case OpCode::OP_TRUE:
                    push(Value::from_bool(true));
                    break;
                case OpCode::OP_FALSE:
                    push(Value::from_bool(false));
                    break;
                case OpCode::OP_POP:
                    pop();
                    break;
                case OpCode::OP_ADD: {
                    Value b = pop();
                    Value a = pop();
                    if (a.is_int() && b.is_int()) {
                        push(Value::from_int(a.as_int() + b.as_int()));
                    } else {
                        auto fa = a.try_as<Float>();
                        auto fb = b.try_as<Float>();
                        if (!fa || !fb) {
                            throw std::runtime_error("Operands must be numbers for addition.");
                        }
                        push(Value::from_float(*fa + *fb));
                    }
                    break;
                }
                case OpCode::OP_SUBTRACT: {
                    Value b = pop();
                    Value a = pop();
                    auto fa = a.try_as<Float>();
                    auto fb = b.try_as<Float>();
                    if (!fa || !fb) {
                        throw std::runtime_error("Operands must be numbers for subtraction.");
                    }
                    if (a.is_int() && b.is_int()) {
                        push(Value::from_int(a.as_int() - b.as_int()));
                    } else {
                        push(Value::from_float(*fa - *fb));
                    }
                    break;
                }
                case OpCode::OP_MULTIPLY: {
                    Value b = pop();
                    Value a = pop();
                    auto fa = a.try_as<Float>();
                    auto fb = b.try_as<Float>();
                    if (!fa || !fb) {
                        throw std::runtime_error("Operands must be numbers for multiplication.");
                    }
                    if (a.is_int() && b.is_int()) {
                        push(Value::from_int(a.as_int() * b.as_int()));
                    } else {
                        push(Value::from_float(*fa * *fb));
                    }
                    break;
                }
                case OpCode::OP_DIVIDE: {
                    Value b = pop();
                    Value a = pop();
                    auto fa = a.try_as<Float>();
                    auto fb = b.try_as<Float>();
                    if (!fa || !fb) {
                        throw std::runtime_error("Operands must be numbers for division.");
                    }
                    push(Value::from_float(*fa / *fb));
                    break;
                }
                case OpCode::OP_MODULO: {
                    Value b = pop();
                    Value a = pop();
                    if (!a.is_int() || !b.is_int()) {
                        throw std::runtime_error("Operands must be integers for modulo.");
                    }
                    push(Value::from_int(a.as_int() % b.as_int()));
                    break;
                }
                case OpCode::OP_NEGATE: {
                    Value a = pop();
                    if (a.is_int()) {
                        push(Value::from_int(-a.as_int()));
                    } else if (a.is_float()) {
                        push(Value::from_float(-a.as_float()));
                    } else {
                        throw std::runtime_error("Operand must be number for negation.");
                    }
                    break;
                }
                case OpCode::OP_BITWISE_NOT: {
                    Value a = pop();
                    if (!a.is_int()) {
                        throw std::runtime_error("Operand must be integer for bitwise not.");
                    }
                    push(Value::from_int(~a.as_int()));
                    break;
                }
                case OpCode::OP_EQUAL: {
                    Value b = pop();
                    Value a = pop();
                    push(Value::from_bool(a.equals(b)));
                    break;
                }
                case OpCode::OP_NOT_EQUAL: {
                    Value b = pop();
                    Value a = pop();
                    push(Value::from_bool(!a.equals(b)));
                    break;
                }
                case OpCode::OP_LESS:
                case OpCode::OP_GREATER:
                case OpCode::OP_LESS_EQUAL:
                case OpCode::OP_GREATER_EQUAL: {
                    Value b = pop();
                    Value a = pop();
                    auto fa = a.try_as<Float>();
                    auto fb = b.try_as<Float>();
                    if (!fa || !fb) {
                        throw std::runtime_error("Operands must be numbers for comparison.");
                    }
                    bool result = false;
                    switch (op) {
                        case OpCode::OP_LESS:
                            result = *fa < *fb;
                            break;
                        case OpCode::OP_GREATER:
                            result = *fa > *fb;
                            break;
                        case OpCode::OP_LESS_EQUAL:
                            result = *fa <= *fb;
                            break;
                        case OpCode::OP_GREATER_EQUAL:
                            result = *fa >= *fb;
                            break;
                        default:
                            break;
                    }
                    push(Value::from_bool(result));
                    break;
                }
                case OpCode::OP_NOT: {
                    Value a = pop();
                    push(Value::from_bool(!is_truthy(a)));
                    break;
                }
                case OpCode::OP_AND: {
                    Value b = pop();
                    Value a = pop();
                    push(Value::from_bool(is_truthy(a) && is_truthy(b)));
                    break;
                }
                case OpCode::OP_OR: {
                    Value b = pop();
                    Value a = pop();
                    push(Value::from_bool(is_truthy(a) || is_truthy(b)));
                    break;
                }
                case OpCode::OP_GET_GLOBAL: {
                    const std::string& name = read_string();
                    push(get_global(name));
                    break;
                }
                case OpCode::OP_SET_GLOBAL: {
                    const std::string& name = read_string();
                    set_global(name, peek(0));
                    break;
                }
                case OpCode::OP_GET_LOCAL: {
                    uint16_t slot = read_short();
                    size_t base = current_stack_base();
                    if (base + slot >= stack_.size()) {
                        throw std::runtime_error("Local slot out of range.");
                    }
                    push(stack_[base + slot]);
                    break;
                }
                case OpCode::OP_SET_LOCAL: {
                    uint16_t slot = read_short();
                    size_t base = current_stack_base();
                    if (base + slot >= stack_.size()) {
                        throw std::runtime_error("Local slot out of range.");
                    }
                    stack_[base + slot] = peek(0);
                    break;
                }
                case OpCode::OP_GET_UPVALUE: {
                    uint16_t slot = read_short();
                    if (call_frames_.empty() || !call_frames_.back().closure) {
                        throw std::runtime_error("No closure active for upvalue read.");
                    }
                    auto* closure = call_frames_.back().closure;
                    if (slot >= closure->upvalues.size()) {
                        throw std::runtime_error("Upvalue index out of range.");
                    }
                    push(*closure->upvalues[slot]->location);
                    break;
                }
                case OpCode::OP_SET_UPVALUE: {
                    uint16_t slot = read_short();
                    if (call_frames_.empty() || !call_frames_.back().closure) {
                        throw std::runtime_error("No closure active for upvalue write.");
                    }
                    auto* closure = call_frames_.back().closure;
                    if (slot >= closure->upvalues.size()) {
                        throw std::runtime_error("Upvalue index out of range.");
                    }
                    *closure->upvalues[slot]->location = peek(0);
                    break;
                }
                case OpCode::OP_CLOSE_UPVALUE: {
                    if (stack_.empty()) {
                        throw std::runtime_error("Stack underflow on close upvalue.");
                    }
                    close_upvalues(&stack_.back());
                    pop();
                    break;
                }
                case OpCode::OP_JUMP: {
                    uint16_t offset = read_short();
                    ip_ += offset;
                    break;
                }
                case OpCode::OP_JUMP_IF_FALSE: {
                    uint16_t offset = read_short();
                    if (!is_truthy(peek(0))) {
                        ip_ += offset;
                    }
                    break;
                }
                case OpCode::OP_LOOP: {
                    uint16_t offset = read_short();
                    ip_ -= offset;
                    break;
                }
                case OpCode::OP_FUNCTION: {
                    uint16_t index = read_short();
                    if (index >= chunk_->functions.size()) {
                        throw std::runtime_error("Function index out of range.");
                    }
                    const auto& proto = chunk_->functions[index];
                    auto* func = allocate_object<FunctionObject>(proto.name, proto.params, proto.chunk, proto.is_initializer, proto.is_override);
                    push(Value::from_object(func));
                    break;
                }
                case OpCode::OP_CLOSURE: {
                    uint16_t index = read_short();
                    if (index >= chunk_->functions.size()) {
                        throw std::runtime_error("Function index out of range.");
                    }
                    const auto& proto = chunk_->functions[index];
                    auto* func = allocate_object<FunctionObject>(proto.name, proto.params, proto.chunk, proto.is_initializer, proto.is_override);
                    auto* closure = allocate_object<ClosureObject>(func);
                    closure->upvalues.resize(proto.upvalues.size(), nullptr);

                    ClosureObject* enclosing_closure = call_frames_.empty() ? nullptr : call_frames_.back().closure;
                    size_t base = current_stack_base();

                    for (size_t i = 0; i < proto.upvalues.size(); ++i) {
                        const auto& uv = proto.upvalues[i];
                        if (uv.is_local) {
                            if (base + uv.index >= stack_.size()) {
                                throw std::runtime_error("Upvalue local slot out of range.");
                            }
                            closure->upvalues[i] = capture_upvalue(&stack_[base + uv.index]);
                        } else {
                            if (!enclosing_closure) {
                                throw std::runtime_error("Upvalue refers to enclosing closure, but none is active.");
                            }
                            if (uv.index >= enclosing_closure->upvalues.size()) {
                                throw std::runtime_error("Upvalue index out of range.");
                            }
                            closure->upvalues[i] = enclosing_closure->upvalues[uv.index];
                        }
                    }

                    push(Value::from_object(closure));
                    break;
                }
                case OpCode::OP_CLASS: {
                    uint16_t name_idx = read_short();
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Class name index out of range.");
                    }
                    const std::string& name = chunk_->strings[name_idx];
                    auto* klass = allocate_object<ClassObject>(name);
                    push(Value::from_object(klass));
                    break;
                }
                case OpCode::OP_METHOD: {
                    uint16_t name_idx = read_short();
                    if (stack_.size() < 2) {
                        throw std::runtime_error("Stack underflow on method definition.");
                    }
                    Value method_val = pop();
                    Value class_val = peek(0);
                    if (!class_val.is_object() || !class_val.as_object() || class_val.as_object()->type != ObjectType::Class) {
                        throw std::runtime_error("OP_METHOD expects class on stack.");
                    }
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Method name index out of range.");
                    }
                    auto* klass = static_cast<ClassObject*>(class_val.as_object());
                    const std::string& name = chunk_->strings[name_idx];

                    // Get function prototype to check is_override flag
                    FunctionObject* func = nullptr;
                    if (method_val.is_object() && method_val.as_object()) {
                        Object* obj = method_val.as_object();
                        if (obj->type == ObjectType::Closure) {
                            func = static_cast<ClosureObject*>(obj)->function;
                        } else if (obj->type == ObjectType::Function) {
                            func = static_cast<FunctionObject*>(obj);
                        }
                    }

                    // Validate override usage
                    if (func && klass->superclass) {
                        Value parent_method;
                        bool parent_has_method = find_method_on_class(klass->superclass, name, parent_method);
                        
                        // Check in function chunk's functions array for is_override flag
                        // Since we're in OP_METHOD right after OP_FUNCTION/OP_CLOSURE, the function was just created
                        // We need to get is_override from the FunctionPrototype
                        bool is_override = func->is_override;
                        
                        if (parent_has_method && !is_override && name != "init") {
                            throw std::runtime_error("Method '" + name + "' overrides a superclass method but is not marked with 'override'");
                        }
                        
                        if (!parent_has_method && is_override) {
                            throw std::runtime_error("Method '" + name + "' marked with 'override' but does not override any superclass method");
                        }
                    }

                    klass->methods[name] = method_val;
                    break;
                }
                case OpCode::OP_DEFINE_PROPERTY: {
                    uint16_t name_idx = read_short();
                    uint8_t flags = read_byte();
                    bool is_let = (flags & 0x1) != 0;
                    if (stack_.size() < 2) {
                        throw std::runtime_error("Stack underflow on property definition.");
                    }
                    Value default_value = peek(0);
                    if (default_value.is_object() && default_value.ref_type() == RefType::Strong) {
                        RC::retain(default_value.as_object());
                    }
                    pop();
                    Value class_val = peek(0);
                    if (!class_val.is_object() || !class_val.as_object() || class_val.as_object()->type != ObjectType::Class) {
                        throw std::runtime_error("OP_DEFINE_PROPERTY expects class on stack.");
                    }
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Property name index out of range.");
                    }
                    auto* klass = static_cast<ClassObject*>(class_val.as_object());
                    ClassObject::PropertyInfo info;
                    info.name = chunk_->strings[name_idx];
                    info.default_value = default_value;
                    info.is_let = is_let;
                    klass->properties.push_back(std::move(info));
                    break;
                }
                case OpCode::OP_INHERIT: {
                    if (stack_.size() < 2) {
                        throw std::runtime_error("Stack underflow on inherit.");
                    }
                    Value subclass_val = peek(0);
                    Value superclass_val = stack_[stack_.size() - 2];
                    if (!subclass_val.is_object() || !subclass_val.as_object() || subclass_val.as_object()->type != ObjectType::Class) {
                        throw std::runtime_error("OP_INHERIT expects subclass on stack.");
                    }
                    if (!superclass_val.is_object() || !superclass_val.as_object() || superclass_val.as_object()->type != ObjectType::Class) {
                        throw std::runtime_error("Superclass must be a class.");
                    }
                    auto* subclass = static_cast<ClassObject*>(subclass_val.as_object());
                    auto* superclass = static_cast<ClassObject*>(superclass_val.as_object());
                    subclass->superclass = superclass;
                    // Remove superclass from stack, keep subclass on top
                    stack_.erase(stack_.end() - 2);
                    break;
                }
                case OpCode::OP_CALL:
                {
                    uint16_t arg_count = read_short();
                    if (stack_.size() < arg_count + 1) {
                        throw std::runtime_error("Not enough values for function call.");
                    }
                    size_t callee_index = stack_.size() - arg_count - 1;
                    Value callee = stack_[callee_index];
                    bool has_receiver = false;
                    
                    if (!callee.is_object() || !callee.as_object()) {
                        throw std::runtime_error("Attempted to call a non-function.");
                    }
                    
                    Object* obj = callee.as_object();
                    
                    // Class call -> instantiate
                    if (obj->type == ObjectType::Class) {
                        auto* klass = static_cast<ClassObject*>(obj);
                        auto* instance = allocate_object<InstanceObject>(klass);

                        // Initialize properties from the entire inheritance chain (base first)
                        std::vector<ClassObject*> hierarchy;
                        for (ClassObject* c = klass; c != nullptr; c = c->superclass) {
                            hierarchy.push_back(c);
                        }
                        for (auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it) {
                            for (const auto& property : (*it)->properties) {
                                Value prop_value = property.default_value;
                                if (prop_value.is_object() && prop_value.ref_type() == RefType::Strong) {
                                    RC::retain(prop_value.as_object());
                                }
                                instance->fields[property.name] = prop_value;
                            }
                        }

                        // Replace callee with instance for return value
                        stack_[callee_index] = Value::from_object(instance);
                        // Initializer?
                        auto it = klass->methods.find("init");
                        if (it == klass->methods.end()) {
                            // No init: discard args, leave instance on stack
                            stack_.resize(callee_index + 1);
                            break;
                        }
                        // Bind init as bound method
                        auto* bound = allocate_object<BoundMethodObject>(instance, it->second);
                        stack_[callee_index] = Value::from_object(bound);
                        callee = stack_[callee_index];
                        obj = callee.as_object();
                    }

                    // Built-in method call handling
                    if (obj->type == ObjectType::BuiltinMethod) {
                        auto* method = static_cast<BuiltinMethodObject*>(obj);
                        
                        if (method->method_name == "append") {
                            if (arg_count != 1) {
                                throw std::runtime_error("append() requires exactly 1 argument.");
                            }
                            
                            if (method->target->type != ObjectType::List) {
                                throw std::runtime_error("append() can only be called on arrays.");
                            }
                            
                            auto* arr = static_cast<ListObject*>(method->target);
                            Value arg = stack_[stack_.size() - 1];
                            
                            // Add element to array
                            arr->elements.push_back(arg);
                            
                            // Clean up stack: remove callee and arguments
                            stack_.resize(callee_index);
                            
                            // append returns nil
                            push(Value::null());
                            break;
                        }
                        
                        throw std::runtime_error("Unknown built-in method: " + method->method_name);
                    }

                    // Bound method: inject receiver
                    if (obj->type == ObjectType::BoundMethod) {
                        auto* bound = static_cast<BoundMethodObject*>(obj);
                        stack_.insert(stack_.begin() + static_cast<long>(callee_index + 1), Value::from_object(bound->receiver));
                        arg_count += 1;
                        stack_[callee_index] = bound->method;
                        callee = stack_[callee_index];
                        obj = callee.as_object();
                        has_receiver = true;
                    }
                    
                    FunctionObject* func = nullptr;
                    ClosureObject* closure = nullptr;

                    if (obj->type == ObjectType::Closure) {
                        closure = static_cast<ClosureObject*>(obj);
                        func = closure->function;
                    } else if (obj->type == ObjectType::Function) {
                        func = static_cast<FunctionObject*>(obj);
                    } else {
                        throw std::runtime_error("Attempted to call a non-function.");
                    }

                    if (has_receiver) {
                        if (arg_count != func->params.size()) {
                            throw std::runtime_error("Incorrect argument count.");
                        }
                    } else {
                        if (arg_count != func->params.size()) {
                            throw std::runtime_error("Incorrect argument count.");
                        }
                    }
                    if (!func->chunk) {
                        throw std::runtime_error("Function has no body.");
                    }
                    call_frames_.emplace_back(callee_index + 1, ip_, chunk_, func->name, closure, func->is_initializer);
                    chunk_ = func->chunk.get();
                    ip_ = 0;
                    break;
                }
                case OpCode::OP_RETURN: {
                    Value result = pop();
                    if (call_frames_.empty()) {
                        return result;
                    }
                    CallFrame frame = call_frames_.back();
                    call_frames_.pop_back();
                    if (frame.is_initializer) {
                        result = stack_[frame.stack_base];
                    }
                    close_upvalues(stack_.data() + frame.stack_base);
                    size_t callee_index = frame.stack_base - 1;
                    stack_.resize(callee_index);
                    chunk_ = frame.chunk;
                    ip_ = frame.return_address;
                    push(result);
                    break;
                }
                case OpCode::OP_UNWRAP: {
                    Value v = peek(0);
                    if (v.is_null()) {
                        throw std::runtime_error("Force unwrap of nil value.");
                    }
                    break;
                }
                case OpCode::OP_JUMP_IF_NIL: {
                    uint16_t offset = read_short();
                    if (peek(0).is_null()) {
                        pop();
                        ip_ += offset;
                    }
                    break;
                }
                case OpCode::OP_NIL_COALESCE: {
                    Value fallback = pop();
                    Value optional = pop();
                    push(optional.is_null() ? fallback : optional);
                    break;
                }
                case OpCode::OP_OPTIONAL_CHAIN: {
                    const std::string& name = read_string();
                    Value obj = pop();
                    if (obj.is_null()) {
                        push(Value::null());
                    } else {
                        push(get_property(obj, name));
                    }
                    break;
                }
                case OpCode::OP_GET_PROPERTY: {
                    const std::string& name = read_string();
                    Value obj = pop();
                    push(get_property(obj, name));
                    break;
                }
                case OpCode::OP_SET_PROPERTY:
                {
                    const std::string& name = read_string();
                    Value value = pop();
                    Value obj_val = pop();
                    if (!obj_val.is_object()) {
                        throw std::runtime_error("Property set on non-object.");
                    }
                    Object* obj = obj_val.as_object();
                    if (!obj) {
                        throw std::runtime_error("Null object in property set.");
                    }
                    if (obj->type == ObjectType::Instance) {
                        auto* inst = static_cast<InstanceObject*>(obj);
                        inst->fields[name] = value;
                        push(value);
                    } else if (obj->type == ObjectType::Map) {
                        auto* dict = static_cast<MapObject*>(obj);
                        dict->entries[name] = value;
                        push(value);
                    } else {
                        throw std::runtime_error("Property set only supported on instances or maps.");
                    }
                    break;
                }
                case OpCode::OP_SUPER: {
                    const std::string& name = read_string();
                    Value receiver = pop();
                    if (!receiver.is_object() || receiver.as_object()->type != ObjectType::Instance) {
                        throw std::runtime_error("'super' can only be used on instances.");
                    }
                    auto* inst = static_cast<InstanceObject*>(receiver.as_object());
                    if (!inst->klass || !inst->klass->superclass) {
                        throw std::runtime_error("No superclass available for 'super' call.");
                    }

                    Value method_value;
                    if (!find_method_on_class(inst->klass->superclass, name, method_value)) {
                        throw std::runtime_error("Undefined super method: " + name);
                    }

                    auto* bound = allocate_object<BoundMethodObject>(inst, method_value);
                    push(Value::from_object(bound));
                    break;
                }
                case OpCode::OP_RANGE_INCLUSIVE:
                case OpCode::OP_RANGE_EXCLUSIVE:
                    // Range opcodes are no-ops: start and end values are already on the stack
                    // The for-in loop implementation uses these values directly
                    break;
                case OpCode::OP_ARRAY: {
                    uint16_t count = read_short();
                    auto* arr = allocate_object<ListObject>();
                    arr->elements.reserve(count);
                    // Pop elements in reverse order (last pushed first)
                    std::vector<Value> temp(count);
                    for (int i = count - 1; i >= 0; --i) {
                        temp[i] = pop();
                    }
                    for (const auto& v : temp) {
                        arr->elements.push_back(v);
                    }
                    push(Value::from_object(arr));
                    break;
                }
                case OpCode::OP_DICT: {
                    uint16_t count = read_short();
                    auto* dict = allocate_object<MapObject>();
                    // Pop key-value pairs in reverse order
                    std::vector<std::pair<Value, Value>> temp(count);
                    for (int i = count - 1; i >= 0; --i) {
                        Value value = pop();
                        Value key = pop();
                        temp[i] = {key, value};
                    }
                    for (const auto& [k, v] : temp) {
                        if (!k.is_object() || k.as_object()->type != ObjectType::String) {
                            throw std::runtime_error("Dictionary key must be a string.");
                        }
                        auto* str_key = static_cast<StringObject*>(k.as_object());
                        dict->entries[str_key->data] = v;
                    }
                    push(Value::from_object(dict));
                    break;
                }
                case OpCode::OP_GET_SUBSCRIPT: {
                    Value index = pop();
                    Value object = pop();
                    if (!object.is_object()) {
                        throw std::runtime_error("Subscript access on non-object.");
                    }
                    Object* obj = object.as_object();
                    if (obj->type == ObjectType::List) {
                        auto* arr = static_cast<ListObject*>(obj);
                        if (!index.is_int()) {
                            throw std::runtime_error("Array index must be an integer.");
                        }
                        int64_t idx = index.as_int();
                        if (idx < 0 || static_cast<size_t>(idx) >= arr->elements.size()) {
                            throw std::runtime_error("Array index out of bounds.");
                        }
                        push(arr->elements[static_cast<size_t>(idx)]);
                    } else if (obj->type == ObjectType::Map) {
                        auto* dict = static_cast<MapObject*>(obj);
                        if (!index.is_object() || index.as_object()->type != ObjectType::String) {
                            throw std::runtime_error("Dictionary key must be a string.");
                        }
                        auto* str_key = static_cast<StringObject*>(index.as_object());
                        auto it = dict->entries.find(str_key->data);
                        if (it == dict->entries.end()) {
                            push(Value::null());
                        } else {
                            push(it->second);
                        }
                    } else {
                        throw std::runtime_error("Subscript access only supported on arrays and dictionaries.");
                    }
                    break;
                }
                case OpCode::OP_SET_SUBSCRIPT: {
                    Value value = pop();
                    Value index = pop();
                    Value object = pop();
                    if (!object.is_object()) {
                        throw std::runtime_error("Subscript assignment on non-object.");
                    }
                    Object* obj = object.as_object();
                    if (obj->type == ObjectType::List) {
                        auto* arr = static_cast<ListObject*>(obj);
                        if (!index.is_int()) {
                            throw std::runtime_error("Array index must be an integer.");
                        }
                        int64_t idx = index.as_int();
                        if (idx < 0 || static_cast<size_t>(idx) >= arr->elements.size()) {
                            throw std::runtime_error("Array index out of bounds.");
                        }
                        arr->elements[static_cast<size_t>(idx)] = value;
                    } else if (obj->type == ObjectType::Map) {
                        auto* dict = static_cast<MapObject*>(obj);
                        if (!index.is_object() || index.as_object()->type != ObjectType::String) {
                            throw std::runtime_error("Dictionary key must be a string.");
                        }
                        auto* str_key = static_cast<StringObject*>(index.as_object());
                        dict->entries[str_key->data] = value;
                    } else {
                        throw std::runtime_error("Subscript assignment only supported on arrays and dictionaries.");
                    }
                    push(value);
                    break;
                }
                case OpCode::OP_PRINT: {
                    Value val = pop();
                    std::cout << val.to_string() << '\n';
                    break;
                }
                case OpCode::OP_HALT:
                    return stack_.empty() ? Value::null() : pop();
                default:
                    throw std::runtime_error("Unknown opcode.");
            }
        }
    }

    uint8_t VM::read_byte() {
        return chunk_->code[ip_++];
    }

    uint16_t VM::read_short() {
        uint16_t high = read_byte();
        uint16_t low = read_byte();
        return static_cast<uint16_t>((high << 8) | low);
    }

    Value VM::read_constant() {
        uint16_t idx = read_short();
        if (idx >= chunk_->constants.size()) {
            throw std::runtime_error("Constant index out of range.");
        }
        return chunk_->constants[idx];
    }

    const std::string& VM::read_string() {
        uint16_t idx = read_short();
        if (idx >= chunk_->strings.size()) {
            throw std::runtime_error("String constant index out of range.");
        }
        return chunk_->strings[idx];
    }

    size_t VM::current_stack_base() const {
        if (call_frames_.empty()) {
            return 0;
        }
        return call_frames_.back().stack_base;
    }

    bool VM::is_truthy(const Value& value) const {
        if (value.is_null()) return false;
        if (value.is_bool()) return value.as_bool();
        return true;
    }

    Value VM::get_property(const Value& object, const std::string& name) {
        if (!object.is_object()) {
            throw std::runtime_error("Attempted property access on non-object.");
        }

        Object* obj = object.as_object();
        if (!obj) {
            throw std::runtime_error("Null object in property access.");
        }

        // Array properties/methods
        if (obj->type == ObjectType::List) {
            auto* arr = static_cast<ListObject*>(obj);

            if (name == "count") {
                return Value::from_int(static_cast<int64_t>(arr->elements.size()));
            }

            if (name == "isEmpty") {
                return Value::from_bool(arr->elements.empty());
            }

            if (name == "append") {
                auto* method = allocate_object<BuiltinMethodObject>(obj, "append");
                return Value::from_object(method);
            }

            throw std::runtime_error("Unknown array property: " + name);
        }

        // Map object properties
        if (obj->type == ObjectType::Map) {
            auto* map = static_cast<MapObject*>(obj);
            auto it = map->entries.find(name);
            if (it == map->entries.end()) {
                return Value::null();
            }
            return it->second;
        }

        if (obj->type == ObjectType::Instance) {
            auto* inst = static_cast<InstanceObject*>(obj);
            auto field_it = inst->fields.find(name);
            if (field_it != inst->fields.end()) {
                return field_it->second;
            }
            if (inst->klass) {
                Value method_value;
                if (find_method_on_class(inst->klass, name, method_value)) {
                    auto* bound = allocate_object<BoundMethodObject>(inst, method_value);
                    return Value::from_object(bound);
                }
            }
            return Value::null();
        }

        if (obj->type == ObjectType::Class) {
            auto* klass = static_cast<ClassObject*>(obj);
            Value method_value;
            if (find_method_on_class(klass, name, method_value)) {
                return method_value;
            }
            return Value::null();
        }

        throw std::runtime_error("Property access supported only on arrays and maps.");
    }

    bool VM::find_method_on_class(ClassObject* klass, const std::string& name, Value& out_method) const {
        for (ClassObject* current = klass; current != nullptr; current = current->superclass) {
            auto it = current->methods.find(name);
            if (it != current->methods.end()) {
                out_method = it->second;
                return true;
            }
        }
        return false;
    }

    UpvalueObject* VM::capture_upvalue(Value* local) {
        UpvalueObject* prev = nullptr;
        UpvalueObject* current = open_upvalues_;

        while (current && current->location > local) {
            prev = current;
            current = current->next_upvalue;
        }

        if (current && current->location == local) {
            return current;
        }

        auto* created = allocate_object<UpvalueObject>(local);
        created->next_upvalue = current;
        if (!prev) {
            open_upvalues_ = created;
        } else {
            prev->next_upvalue = created;
        }
        return created;
    }

    void VM::close_upvalues(Value* last) {
        while (open_upvalues_ && open_upvalues_->location >= last) {
            UpvalueObject* upvalue = open_upvalues_;
            upvalue->closed = *upvalue->location;
            upvalue->location = &upvalue->closed;
            open_upvalues_ = upvalue->next_upvalue;
        }
    }

} // namespace swiftscript
