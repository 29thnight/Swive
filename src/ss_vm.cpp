#include "ss_vm.hpp"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include <iostream>
#include <iomanip>

namespace swiftscript {

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
                    auto* func = allocate_object<FunctionObject>(proto.name, proto.params, proto.chunk);
                    push(Value::from_object(func));
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
                    if (!callee.is_object() || !callee.as_object() ||
                        callee.as_object()->type != ObjectType::Function) {
                        throw std::runtime_error("Attempted to call a non-function.");
                    }
                    auto* func = static_cast<FunctionObject*>(callee.as_object());
                    if (arg_count != func->params.size()) {
                        throw std::runtime_error("Incorrect argument count.");
                    }
                    if (!func->chunk) {
                        throw std::runtime_error("Function has no body.");
                    }
                    call_frames_.emplace_back(callee_index + 1, ip_, chunk_, func->name);
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
                    throw std::runtime_error("Property set not implemented.");
                case OpCode::OP_PRINT: {
                    Value val = pop();
                    std::cout << val.to_string() << "\n";
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
        if (!obj || obj->type != ObjectType::Map) {
            throw std::runtime_error("Property access supported only on Map objects.");
        }
        auto* map = static_cast<MapObject*>(obj);
        auto it = map->entries.find(name);
        if (it == map->entries.end()) {
            return Value::null();
        }
        return it->second;
    }

} // namespace swiftscript
