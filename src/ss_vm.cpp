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
            // Push self directly without RC (instance is already being deallocated)
            // Do NOT use push() as it would retain the dying object
            stack_.push_back(Value::from_object(inst));

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
            // NOTE: We use stack_.push_back/pop_back directly to avoid RC on the dying instance
            while (ip_ < chunk_->code.size()) {
                OpCode op = static_cast<OpCode>(read_byte());

                if (op == OpCode::OP_RETURN) {
                    break;  // Exit deinit execution
                }

                // Execute the opcode (simplified - RC-free for deinit context)
                switch (op) {
                    case OpCode::OP_CONSTANT: {
                        stack_.push_back(read_constant());
                        break;
                    }
                    case OpCode::OP_STRING: {
                        const std::string& str = read_string();
                        auto* obj = allocate_object<StringObject>(str);
                        stack_.push_back(Value::from_object(obj));
                        break;
                    }
                    case OpCode::OP_GET_LOCAL: {
                        uint16_t slot = read_short();
                        size_t base = call_frames_.back().stack_base;
                        // Push without RC (self is dying, other locals shouldn't exist in deinit)
                        stack_.push_back(stack_[base + slot - 1]);
                        break;
                    }
                    case OpCode::OP_GET_PROPERTY: {
                        const std::string& name = read_string();
                        Value obj_val = stack_.back();
                        stack_.pop_back();
                        // get_property may allocate BoundMethod, handle with push for proper RC
                        Value prop = get_property(obj_val, name);
                        stack_.push_back(prop);
                        break;
                    }
                    case OpCode::OP_PRINT: {
                        Value val = stack_.back();
                        stack_.pop_back();
                        std::cout << val.to_string() << '\n';
                        break;
                    }
                    case OpCode::OP_POP:
                        stack_.pop_back();
                        break;
                    case OpCode::OP_NIL:
                        stack_.push_back(Value::null());
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

        // Restore stack without RC (deinit runs in RC-free context)
        // All values pushed during deinit are either primitives or from the dying object
        stack_.resize(saved_stack_size);

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
        Value result = run();
        run_cleanup();
        while (!deferred_releases_.empty()) {
            run_cleanup();
        }
        return result;
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
                    Value type_val = peek(0);
                    
                    if (!type_val.is_object() || !type_val.as_object()) {
                        throw std::runtime_error("OP_METHOD expects class or enum on stack.");
                    }
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Method name index out of range.");
                    }
                    
                    Object* type_obj = type_val.as_object();
                    const std::string& name = chunk_->strings[name_idx];
                    
                    // Handle ClassObject
                    if (type_obj->type == ObjectType::Class) {
                        auto* klass = static_cast<ClassObject*>(type_obj);

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
                            
                            bool is_override = func->is_override;
                            
                            if (parent_has_method && !is_override && name != "init") {
                                throw std::runtime_error("Method '" + name + "' overrides a superclass method but is not marked with 'override'");
                            }
                            
                            if (!parent_has_method && is_override) {
                                throw std::runtime_error("Method '" + name + "' marked with 'override' but does not override any superclass method");
                            }
                        }

                        klass->methods[name] = method_val;
                    }
                    // Handle EnumObject
                    else if (type_obj->type == ObjectType::Enum) {
                        auto* enum_type = static_cast<EnumObject*>(type_obj);
                        enum_type->methods[name] = method_val;
                    }
                    else {
                        throw std::runtime_error("OP_METHOD expects class or enum on stack.");
                    }
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
                    Value type_val = peek(0);
                    if (!type_val.is_object() || !type_val.as_object()) {
                        throw std::runtime_error("OP_DEFINE_PROPERTY expects class or struct on stack.");
                    }
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Property name index out of range.");
                    }
                    Object* type_obj = type_val.as_object();
                    if (type_obj->type == ObjectType::Class) {
                        auto* klass = static_cast<ClassObject*>(type_obj);
                        ClassObject::PropertyInfo info;
                        info.name = chunk_->strings[name_idx];
                        info.default_value = default_value;
                        info.is_let = is_let;
                        klass->properties.push_back(std::move(info));
                    } else if (type_obj->type == ObjectType::Struct) {
                        auto* struct_type = static_cast<StructObject*>(type_obj);
                        StructObject::PropertyInfo info;
                        info.name = chunk_->strings[name_idx];
                        info.default_value = default_value;
                        info.is_let = is_let;
                        struct_type->properties.push_back(std::move(info));
                    } else {
                        throw std::runtime_error("OP_DEFINE_PROPERTY expects class or struct on stack.");
                    }
                    break;
                }
                case OpCode::OP_DEFINE_COMPUTED_PROPERTY: {
                    // Stack: [... class]
                    // Read: property_name_idx, getter_idx, setter_idx
                    uint16_t name_idx = read_short();
                    uint16_t getter_idx = read_short();
                    uint16_t setter_idx = read_short();
                    
                    Value type_val = peek(0);
                    if (!type_val.is_object() || !type_val.as_object()) {
                        throw std::runtime_error("OP_DEFINE_COMPUTED_PROPERTY expects class or enum on stack.");
                    }
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Property name index out of range.");
                    }
                    if (getter_idx >= chunk_->functions.size()) {
                        throw std::runtime_error("Getter function index out of range.");
                    }
                    
                    Object* type_obj = type_val.as_object();
                    if (type_obj->type == ObjectType::Class) {
                        auto* klass = static_cast<ClassObject*>(type_obj);
                        ClassObject::ComputedPropertyInfo info;
                        info.name = chunk_->strings[name_idx];
                        
                        // Create getter function and closure
                        const auto& getter_proto = chunk_->functions[getter_idx];
                        auto* getter_func = allocate_object<FunctionObject>(
                            getter_proto.name,
                            getter_proto.params,
                            getter_proto.chunk,
                            false,
                            false
                        );
                        auto* getter_closure = allocate_object<ClosureObject>(getter_func);
                        info.getter = Value::from_object(getter_closure);
                        
                        // Create setter function and closure if present
                        if (setter_idx != 0xFFFF && setter_idx < chunk_->functions.size()) {
                            const auto& setter_proto = chunk_->functions[setter_idx];
                            auto* setter_func = allocate_object<FunctionObject>(
                                setter_proto.name,
                                setter_proto.params,
                                setter_proto.chunk,
                                false,
                                false
                            );
                            auto* setter_closure = allocate_object<ClosureObject>(setter_func);
                            info.setter = Value::from_object(setter_closure);
                        } else {
                            info.setter = Value::null();
                        }
                        
                        klass->computed_properties.push_back(std::move(info));
                    } else if (type_obj->type == ObjectType::Enum) {
                        auto* enum_type = static_cast<EnumObject*>(type_obj);
                        EnumObject::ComputedPropertyInfo info;
                        info.name = chunk_->strings[name_idx];
                        
                        // Create getter function and closure
                        const auto& getter_proto = chunk_->functions[getter_idx];
                        auto* getter_func = allocate_object<FunctionObject>(
                            getter_proto.name,
                            getter_proto.params,
                            getter_proto.chunk,
                            false,
                            false
                        );
                        auto* getter_closure = allocate_object<ClosureObject>(getter_func);
                        info.getter = Value::from_object(getter_closure);
                        
                        // Enum computed properties are read-only (no setter)
                        info.setter = Value::null();
                        
                        enum_type->computed_properties.push_back(std::move(info));
                    } else {
                        throw std::runtime_error("OP_DEFINE_COMPUTED_PROPERTY expects class or enum on stack.");
                    }
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

                    // Struct call -> instantiate (value type)
                    if (obj->type == ObjectType::Struct) {
                        auto* struct_type = static_cast<StructObject*>(obj);
                        auto* instance = allocate_object<StructInstanceObject>(struct_type);

                        // Initialize properties with default values
                        for (const auto& property : struct_type->properties) {
                            Value prop_value = property.default_value;
                            if (prop_value.is_object() && prop_value.ref_type() == RefType::Strong && prop_value.as_object()) {
                                RC::retain(prop_value.as_object());
                            }
                            instance->fields[property.name] = prop_value;
                        }

                        // Replace callee with instance
                        Value old_callee = stack_[callee_index];
                        if (old_callee.is_object() && old_callee.ref_type() == RefType::Strong && old_callee.as_object()) {
                            RC::release(this, old_callee.as_object());
                        }
                        stack_[callee_index] = Value::from_object(instance);

                        // Check for init method
                        auto it = struct_type->methods.find("init");
                        if (it == struct_type->methods.end()) {
                            // No init: check for memberwise initializer pattern
                            // If arguments match property count, do memberwise init
                            if (arg_count == struct_type->properties.size()) {
                                // Memberwise initializer
                                for (size_t i = 0; i < arg_count; ++i) {
                                    Value arg = stack_[callee_index + 1 + i];
                                    const std::string& prop_name = struct_type->properties[i].name;
                                    if (arg.is_object() && arg.ref_type() == RefType::Strong && arg.as_object()) {
                                        RC::retain(arg.as_object());
                                    }
                                    instance->fields[prop_name] = arg;
                                }
                                // Discard args using pop(), leave instance on stack
                                while (stack_.size() > callee_index + 1) {
                                    pop();
                                }
                                break;
                            } else if (arg_count == 0) {
                                // No args, no init - just use defaults
                                break;
                            } else {
                                throw std::runtime_error("Struct '" + struct_type->name +
                                    "' has no init and argument count doesn't match property count.");
                            }
                        }

                        // Bind init as bound method for struct
                        auto* bound = allocate_object<BoundMethodObject>(instance, it->second);
                        stack_[callee_index] = Value::from_object(bound);
                        callee = stack_[callee_index];
                        obj = callee.as_object();
                    }

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

                        // Replace callee with instance: release old callee (class object)
                        // Note: instance starts with refcount=1 from allocate_object, which represents the stack slot
                        Value old_callee = stack_[callee_index];
                        if (old_callee.is_object() && old_callee.ref_type() == RefType::Strong && old_callee.as_object()) {
                            RC::release(this, old_callee.as_object());
                        }
                        stack_[callee_index] = Value::from_object(instance);

                        // Initializer?
                        auto it = klass->methods.find("init");
                        if (it == klass->methods.end()) {
                            // No init: discard args using pop(), leave instance on stack
                            while (stack_.size() > callee_index + 1) {
                                pop();
                            }
                            break;
                        }
                        // Bind init as bound method
                        // BoundMethod takes ownership of instance (no retain/release needed)
                        // bound gets refcount=1 from allocate, instance refcount stays at 1 (now owned by BoundMethod)
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
                            Value arg = peek(0);

                            // Add element to array (retain for the array)
                            if (arg.is_object() && arg.ref_type() == RefType::Strong && arg.as_object()) {
                                RC::retain(arg.as_object());
                            }
                            arr->elements.push_back(arg);

                            // Clean up stack using pop()
                            while (stack_.size() > callee_index) {
                                pop();
                            }

                            // append returns nil
                            push(Value::null());
                            break;
                        }

                        throw std::runtime_error("Unknown built-in method: " + method->method_name);
                    }

                    // Bound method: inject receiver
                    if (obj->type == ObjectType::BoundMethod) {
                        auto* bound = static_cast<BoundMethodObject*>(obj);

                        // Insert receiver with retain (new stack slot needs ownership)
                        Value receiver_val = Value::from_object(bound->receiver);
                        if (receiver_val.is_object() && receiver_val.ref_type() == RefType::Strong && receiver_val.as_object()) {
                            RC::retain(receiver_val.as_object());
                        }
                        stack_.insert(stack_.begin() + static_cast<long>(callee_index + 1), receiver_val);
                        arg_count += 1;

                        // Replace callee slot: BoundMethod -> method
                        // Retain method first (it's referenced from bound which will be released)
                        Value method_val = bound->method;
                        if (method_val.is_object() && method_val.ref_type() == RefType::Strong && method_val.as_object()) {
                            RC::retain(method_val.as_object());
                        }
                        // Now release BoundMethod from callee slot
                        Value old_bound = stack_[callee_index];
                        stack_[callee_index] = method_val;  // Replace first to avoid use-after-free
                        if (old_bound.is_object() && old_bound.ref_type() == RefType::Strong && old_bound.as_object()) {
                            RC::release(this, old_bound.as_object());
                        }
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
                        // For initializer, return the instance (self) instead
                        result = stack_[frame.stack_base];
                        // Retain it since we're returning it
                        if (result.is_object() && result.ref_type() == RefType::Strong && result.as_object()) {
                            RC::retain(result.as_object());
                        }
                    }
                    close_upvalues(stack_.data() + frame.stack_base);
                    // Pop all locals and arguments (releases their refcounts)
                    size_t callee_index = frame.stack_base - 1;
                    while (stack_.size() > callee_index) {
                        pop();
                    }
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
                    Value obj = pop();  // Pop object from stack
                    bool handled_computed = false;
                    
                    // Check for computed property first (for instances)
                    if (obj.is_object() && obj.as_object()->type == ObjectType::Instance) {
                        auto* inst = static_cast<InstanceObject*>(obj.as_object());
                        if (inst->klass) {
                            for (const auto& comp_prop : inst->klass->computed_properties) {
                                if (comp_prop.name == name) {
                                    // Found computed property - call getter
                                    // Setup call: push getter function, push self
                                    Value getter = comp_prop.getter;
                                    
                                    if (!getter.is_object()) {
                                        throw std::runtime_error("Computed property getter is not a function.");
                                    }
                                    
                                    Object* obj_callee = getter.as_object();
                                    FunctionObject* func = nullptr;
                                    ClosureObject* closure = nullptr;
                                    
                                    if (obj_callee->type == ObjectType::Closure) {
                                        closure = static_cast<ClosureObject*>(obj_callee);
                                        func = closure->function;
                                    } else if (obj_callee->type == ObjectType::Function) {
                                        func = static_cast<FunctionObject*>(obj_callee);
                                    } else {
                                        throw std::runtime_error("Computed property getter must be a function.");
                                    }
                                    
                                    if (!func || !func->chunk) {
                                        throw std::runtime_error("Getter function has no body.");
                                    }
                                    if (func->params.size() != 1) {
                                        throw std::runtime_error("Getter should have exactly 1 parameter (self).");
                                    }
                                    
                                    // Push callee and self argument
                                    push(getter);
                                    push(obj);  // self
                                    
                                    // Setup call frame
                                    size_t callee_index = stack_.size() - 2;  // -2 because we have [getter, self]
                                    call_frames_.emplace_back(callee_index + 1, ip_, chunk_, func->name, closure, false);
                                    chunk_ = func->chunk.get();
                                    ip_ = 0;
                                    handled_computed = true;
                                    break;  // Continue execution in getter
                                }
                            }
                        }
                    }
                    if (handled_computed) {
                        break;
                    }
                    
                    // Check for computed property on EnumCase
                    if (obj.is_object() && obj.as_object()->type == ObjectType::EnumCase) {
                        auto* enum_case = static_cast<EnumCaseObject*>(obj.as_object());
                        if (enum_case->enum_type) {
                            for (const auto& comp_prop : enum_case->enum_type->computed_properties) {
                                if (comp_prop.name == name) {
                                    // Found computed property - call getter
                                    Value getter = comp_prop.getter;
                                    
                                    if (!getter.is_object()) {
                                        throw std::runtime_error("Computed property getter is not a function.");
                                    }
                                    
                                    Object* obj_callee = getter.as_object();
                                    FunctionObject* func = nullptr;
                                    ClosureObject* closure = nullptr;
                                    
                                    if (obj_callee->type == ObjectType::Closure) {
                                        closure = static_cast<ClosureObject*>(obj_callee);
                                        func = closure->function;
                                    } else if (obj_callee->type == ObjectType::Function) {
                                        func = static_cast<FunctionObject*>(obj_callee);
                                    } else {
                                        throw std::runtime_error("Computed property getter must be a function.");
                                    }
                                    
                                    if (!func || !func->chunk) {
                                        throw std::runtime_error("Getter function has no body.");
                                    }
                                    if (func->params.size() != 1) {
                                        throw std::runtime_error("Getter should have exactly 1 parameter (self).");
                                    }
                                    
                                    // Push callee and self argument
                                    push(getter);
                                    push(obj);  // self (enum case)
                                    
                                    // Setup call frame
                                    size_t callee_index = stack_.size() - 2;
                                    call_frames_.emplace_back(callee_index + 1, ip_, chunk_, func->name, closure, false);
                                    chunk_ = func->chunk.get();
                                    ip_ = 0;
                                    handled_computed = true;
                                    break;  // Continue execution in getter
                                }
                            }
                        }
                    }
                    if (handled_computed) {
                        break;
                    }
                    
                    // Not a computed property, use regular get_property
                    push(get_property(obj, name));
                    break;
                }
                case OpCode::OP_SET_PROPERTY:
                {
                    const std::string& name = read_string();
                    Value value = pop();
                    Value obj_val = peek(0);  // Keep object on stack temporarily
                    
                    if (!obj_val.is_object()) {
                        throw std::runtime_error("Property set on non-object.");
                    }
                    Object* obj = obj_val.as_object();
                    if (!obj) {
                        throw std::runtime_error("Null object in property set.");
                    }
                    
                    if (obj->type == ObjectType::Instance) {
                        auto* inst = static_cast<InstanceObject*>(obj);
                        
                        // Check if it's a computed property
                        if (inst->klass) {
                            for (const auto& comp_prop : inst->klass->computed_properties) {
                                if (comp_prop.name == name) {
                                    // Found computed property - call setter
                                    if (comp_prop.setter.is_null()) {
                                        throw std::runtime_error("Cannot set read-only computed property: " + name);
                                    }
                                    
                                    // Stack: [... instance]
                                    // Need to call setter with (self, newValue)
                                    Value setter = comp_prop.setter;
                                    
                                    pop();  // Remove instance from stack
                                    
                                    push(setter);    // callee
                                    push(obj_val);   // self arg
                                    push(value);     // newValue arg
                                    
                                    // Execute OP_CALL logic inline
                                    size_t arg_count = 2;
                                    size_t callee_index = stack_.size() - arg_count - 1;
                                    Value callee = stack_[callee_index];
                                    
                                    if (!callee.is_object()) {
                                        throw std::runtime_error("Computed property setter is not a function.");
                                    }
                                    
                                    Object* obj_callee = callee.as_object();
                                    FunctionObject* func = nullptr;
                                    ClosureObject* closure = nullptr;
                                    
                                    if (obj_callee->type == ObjectType::Closure) {
                                        closure = static_cast<ClosureObject*>(obj_callee);
                                        func = closure->function;
                                    } else if (obj_callee->type == ObjectType::Function) {
                                        func = static_cast<FunctionObject*>(obj_callee);
                                    } else {
                                        throw std::runtime_error("Computed property setter must be a function.");
                                    }
                                    
                                    if (arg_count != func->params.size()) {
                                        throw std::runtime_error("Incorrect argument count for computed property setter.");
                                    }
                                    if (!func->chunk) {
                                        throw std::runtime_error("Setter function has no body.");
                                    }
                                    
                                    call_frames_.emplace_back(callee_index + 1, ip_, chunk_, func->name, closure, false);
                                    chunk_ = func->chunk.get();
                                    ip_ = 0;
                                    
                                    // Note: When setter returns, we need to push the value
                                    // This is handled by the return which will push value onto stack
                                    // But we actually want to push 'value' not the setter's return
                                    // So we'll handle this differently - save ip and continue after setter returns
                                    break;
                                }
                            }
                        }
                        
                        // Regular stored property
                        pop();  // Remove instance
                        inst->fields[name] = value;
                        push(value);
                    } else if (obj->type == ObjectType::StructInstance) {
                        pop();
                        auto* inst = static_cast<StructInstanceObject*>(obj);
                        inst->fields[name] = value;
                        push(value);
                    } else if (obj->type == ObjectType::Map) {
                        pop();
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
                case OpCode::OP_STRUCT: {
                    // Create a struct type object (similar to OP_CLASS)
                    uint16_t name_idx = read_short();
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Struct name index out of range.");
                    }
                    const std::string& name = chunk_->strings[name_idx];
                    auto* struct_type = allocate_object<StructObject>(name);
                    push(Value::from_object(struct_type));
                    break;
                }
                case OpCode::OP_STRUCT_METHOD: {
                    // Similar to OP_METHOD but with mutating flag
                    uint16_t name_idx = read_short();
                    uint8_t is_mutating = read_byte();
                    if (stack_.size() < 2) {
                        throw std::runtime_error("Stack underflow on struct method definition.");
                    }
                    Value method_val = pop();
                    Value struct_val = peek(0);
                    if (!struct_val.is_object() || !struct_val.as_object() ||
                        struct_val.as_object()->type != ObjectType::Struct) {
                        throw std::runtime_error("OP_STRUCT_METHOD expects struct on stack.");
                    }
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Method name index out of range.");
                    }
                    auto* struct_type = static_cast<StructObject*>(struct_val.as_object());
                    const std::string& name = chunk_->strings[name_idx];
                    struct_type->methods[name] = method_val;
                    struct_type->mutating_methods[name] = (is_mutating != 0);
                    break;
                }
                case OpCode::OP_COPY_VALUE: {
                    // Deep copy a struct instance for value semantics
                    Value val = pop();
                    if (val.is_object() && val.as_object() &&
                        val.as_object()->type == ObjectType::StructInstance) {
                        auto* inst = static_cast<StructInstanceObject*>(val.as_object());
                        auto* copy = inst->deep_copy(*this);
                        push(Value::from_object(copy));
                    } else {
                        // Not a struct instance, just pass through
                        push(val);
                    }
                    break;
                }
                case OpCode::OP_ENUM: {
                    // Create an enum type object (similar to OP_CLASS and OP_STRUCT)
                    uint16_t name_idx = read_short();
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Enum name index out of range.");
                    }
                    const std::string& name = chunk_->strings[name_idx];
                    auto* enum_type = allocate_object<EnumObject>(name);
                    push(Value::from_object(enum_type));
                    break;
                }
                case OpCode::OP_ENUM_CASE: {
                    // Define an enum case
                    // Stack: [enum_object, raw_value]
                    uint16_t case_name_idx = read_short();
                    uint8_t associated_count = read_byte();
                    
                    if (stack_.size() < 2) {
                        throw std::runtime_error("Stack underflow on enum case definition.");
                    }
                    
                    Value raw_value = pop();  // May be nil
                    Value enum_val = peek(0);
                    
                    if (!enum_val.is_object() || !enum_val.as_object() ||
                        enum_val.as_object()->type != ObjectType::Enum) {
                        throw std::runtime_error("OP_ENUM_CASE expects enum on stack.");
                    }
                    if (case_name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Enum case name index out of range.");
                    }
                    
                    auto* enum_type = static_cast<EnumObject*>(enum_val.as_object());
                    const std::string& case_name = chunk_->strings[case_name_idx];
                    
                    // Create enum case instance
                    auto* case_obj = allocate_object<EnumCaseObject>(enum_type, case_name);
                    case_obj->raw_value = raw_value;
                    // associated_count is for future use (associated values)
                    
                    // Store in enum's cases map
                    enum_type->cases[case_name] = Value::from_object(case_obj);
                    break;
                }
                case OpCode::OP_PROTOCOL: {
                    // Create protocol object
                    uint16_t protocol_idx = read_short();
                    if (protocol_idx >= chunk_->protocols.size()) {
                        throw std::runtime_error("Protocol index out of range.");
                    }
                    
                    auto protocol = chunk_->protocols[protocol_idx];
                    auto* protocol_obj = allocate_object<ProtocolObject>(protocol->name);
                    
                    // Store protocol requirements for runtime validation
                    for (const auto& method_req : protocol->method_requirements) {
                        protocol_obj->method_requirements.push_back(method_req.name);
                    }
                    for (const auto& prop_req : protocol->property_requirements) {
                        protocol_obj->property_requirements.push_back(prop_req.name);
                    }
                    
                    push(Value::from_object(protocol_obj));
                    break;
                }
                case OpCode::OP_DEFINE_GLOBAL: {
                    // Define a global variable with the value on top of stack
                    uint16_t name_idx = read_short();
                    if (name_idx >= chunk_->strings.size()) {
                        throw std::runtime_error("Global name index out of range.");
                    }
                    const std::string& name = chunk_->strings[name_idx];
                    globals_[name] = peek(0);
                    pop();
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
            // Note: Computed properties are handled in OP_GET_PROPERTY directly
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

        // Struct instance property access
        if (obj->type == ObjectType::StructInstance) {
            auto* inst = static_cast<StructInstanceObject*>(obj);
            auto field_it = inst->fields.find(name);
            if (field_it != inst->fields.end()) {
                return field_it->second;
            }
            // Check for methods on the struct type
            if (inst->struct_type) {
                auto method_it = inst->struct_type->methods.find(name);
                if (method_it != inst->struct_type->methods.end()) {
                    // Create a bound method
                    // For struct, we need to copy the instance for value semantics
                    auto* copy = inst->deep_copy(*this);
                    auto* bound = allocate_object<BoundMethodObject>(copy, method_it->second);
                    return Value::from_object(bound);
                }
            }
            return Value::null();
        }

        // Struct type property access (static methods would go here)
        if (obj->type == ObjectType::Struct) {
            auto* struct_type = static_cast<StructObject*>(obj);
            auto method_it = struct_type->methods.find(name);
            if (method_it != struct_type->methods.end()) {
                return method_it->second;
            }
            return Value::null();
        }

        // Enum type: access enum cases (Direction.north)
        if (obj->type == ObjectType::Enum) {
            auto* enum_type = static_cast<EnumObject*>(obj);
            auto case_it = enum_type->cases.find(name);
            if (case_it != enum_type->cases.end()) {
                return case_it->second;  // Return EnumCaseObject
            }
            // Also check for static methods
            auto method_it = enum_type->methods.find(name);
            if (method_it != enum_type->methods.end()) {
                return method_it->second;
            }
            throw std::runtime_error("Enum '" + enum_type->name + "' has no case or method named '" + name + "'");
        }

        // Enum case: access methods and properties (direction.describe())
        if (obj->type == ObjectType::EnumCase) {
            auto* enum_case = static_cast<EnumCaseObject*>(obj);
            
            // Special property: rawValue
            if (name == "rawValue") {
                return enum_case->raw_value;
            }
            
            // Note: Computed properties are handled in OP_GET_PROPERTY directly
            // Here we only handle methods
            
            // Look for methods in the enum type
            if (enum_case->enum_type) {
                auto method_it = enum_case->enum_type->methods.find(name);
                if (method_it != enum_case->enum_type->methods.end()) {
                    // Bind the method to this enum case
                    auto* bound = allocate_object<BoundMethodObject>(enum_case, method_it->second);
                    return Value::from_object(bound);
                }
            }
            throw std::runtime_error("Enum case '" + enum_case->case_name + "' has no property or method named '" + name + "'");
        }

        throw std::runtime_error("Property access supported only on arrays, maps, and instances.");
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
