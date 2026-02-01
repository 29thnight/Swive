#include "pch.h"
#include "ss_vm.hpp"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"

namespace swiftscript {

    // Instantiate the OPCODE handler table from the constexpr factory in the
    // included `ss_vm_opcodes.inl`.
    const std::array<OpHandlerFunc, 256> g_opcode_handlers = make_handler_table();


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

            // Call deinit for any remaining instances (even if refcount never hit zero)
            if (obj->type == ObjectType::Instance) {
                auto* inst = static_cast<InstanceObject*>(obj);
                if (inst->klass) {
                    Value deinit_method;
                    ClassObject* current = inst->klass;
                    while (current) {
                        auto it = current->methods.find("deinit");
                        if (it != current->methods.end()) {
                            deinit_method = it->second;
                            break;
                        }
                        current = current->superclass;
                    }

                    if (!deinit_method.is_null() && deinit_method.is_object()) {
                        try {
                            execute_deinit(inst, deinit_method);
                        } catch (...) {
                            // Swallow any errors during shutdown cleanup
                        }
                    }
                }
            }

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
        const Assembly* saved_chunk = chunk_;
        size_t saved_ip = ip_;
        body_idx saved_body = current_body_idx_;
        const MethodBody* saved_body_ptr = current_body_;
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
                saved_body,             // saved body index
                "deinit",              // function name
                closure,                // closure (if any)
                false                   // is_initializer
            );

            // Switch to deinit's chunk
            chunk_ = func->chunk.get();
            current_body_idx_ = entry_body_index(*chunk_);
            set_active_body(current_body_idx_);
            ip_ = 0;

            // Execute deinit bytecode until OP_RETURN
            // NOTE: We use stack_.push_back/pop_back directly to avoid RC on the dying instance
            while (ip_ < active_bytecode().size()) {
                OpCode op = static_cast<OpCode>(read_byte());

                if (op == OpCode::OP_RETURN) {
                    break;  // Exit deinit execution
                }

                // Execute the OPCODE (simplified - RC-free for deinit context)
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
        current_body_idx_ = saved_body;
        current_body_ = saved_body_ptr;

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
        Assembly chunk = compiler.compile(program);
        return execute(chunk);
    }

    Value VM::execute(const Assembly& chunk) {
        chunk_ = &chunk;
        current_body_idx_ = entry_body_index(chunk);
        set_active_body(current_body_idx_);
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

    Value VM::run() 
    {
        while(true) 
        {
            OpCode op = static_cast<OpCode>(read_byte());
            auto handler = g_opcode_handlers[static_cast<uint8_t>(op)];
            if (!handler)
            {
                std::cerr << "Unknown opcode handlers.\n";
            }
            else
            {
                handler(*this);
            }

            switch (op)
            {
			case OpCode::OP_RETURN:
            {
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
                current_body_idx_ = frame.body_index;
                set_active_body(current_body_idx_);
                push(result);
                break;
            }
            case OpCode::OP_READ_LINE: 
            {
                std::string line;
                if (!std::getline(std::cin, line)) {
                    push(Value::null());
                    break;
                }
                auto* obj = allocate_object<StringObject>(std::move(line));
                push(Value::from_object(obj));
                break;
            }
            case OpCode::OP_PRINT: 
            {
                Value val = pop();
                std::cout << val.to_string() << '\n';
                break;
            }
            case OpCode::OP_THROW: {
                // Throw statement - for now, just throw a runtime error
                Value error_value = pop();
                std::string error_msg = "Uncaught error: " + error_value.to_string();
                throw std::runtime_error(error_msg);
            }
            case OpCode::OP_HALT:
                return stack_.empty() ? Value::null() : pop();
            }
        }
    }

    const std::vector<uint8_t>& VM::active_bytecode() const {
        if (current_body_) {
            return current_body_->bytecode;
        }
        if (chunk_ && !chunk_->method_bodies.empty()) {
            return chunk_->method_bodies.front().bytecode;
        }
        if (!chunk_) {
            throw std::runtime_error("No active chunk.");
        }
        return chunk_->code;
    }

    body_idx VM::entry_body_index(const Assembly& chunk) const {
        if (!chunk.method_definitions.empty()) {
            body_idx idx = chunk.method_definitions.front().body_ptr;
            if (idx < chunk.method_bodies.size()) {
                return idx;
            }
        }
        if (!chunk.method_bodies.empty()) {
            return 0;
        }
        return std::numeric_limits<body_idx>::max();
    }

    void VM::set_active_body(body_idx idx) {
        if (!chunk_ || idx == std::numeric_limits<body_idx>::max()) {
            current_body_ = nullptr;
            return;
        }
        if (idx >= chunk_->method_bodies.size()) {
            throw std::runtime_error("Method body index out of range.");
        }
        current_body_idx_ = idx;
        current_body_ = &chunk_->method_bodies[idx];
    }

    uint8_t VM::read_byte() {
        return active_bytecode()[ip_++];
    }

    uint16_t VM::read_short() {
        uint16_t high = read_byte();
        uint16_t low = read_byte();
        return static_cast<uint16_t>((high << 8) | low);
    }

    Value VM::read_constant() {
        uint16_t idx = read_short();
        const auto& pool = chunk_->global_constant_pool.empty()
            ? chunk_->constants
            : chunk_->global_constant_pool;
        if (idx >= pool.size()) {
            throw std::runtime_error("Constant index out of range.");
        }
        return pool[idx];
    }

    const std::string& VM::read_string() {
        uint16_t idx = read_short();
        if (idx >= chunk_->strings.size()) {
            throw std::runtime_error("String constant index out of range.");
        }
        return chunk_->strings[idx];
    }

    uint32_t VM::read_signature_param_count(signature_idx offset) const {
        if (!chunk_) {
            throw std::runtime_error("No active chunk for signature read.");
        }
        if (offset + sizeof(uint32_t) > chunk_->signature_blob.size()) {
            throw std::runtime_error("Signature offset out of range.");
        }
        auto read_u32 = [&](size_t pos) -> uint32_t {
            if (pos + sizeof(uint32_t) > chunk_->signature_blob.size()) {
                throw std::runtime_error("Signature blob out of range.");
            }
            uint32_t value = 0;
            value |= static_cast<uint32_t>(chunk_->signature_blob[pos]);
            value |= static_cast<uint32_t>(chunk_->signature_blob[pos + 1]) << 8;
            value |= static_cast<uint32_t>(chunk_->signature_blob[pos + 2]) << 16;
            value |= static_cast<uint32_t>(chunk_->signature_blob[pos + 3]) << 24;
            return value;
        };
        uint32_t param_count = read_u32(offset);
        size_t expected_size = sizeof(uint32_t) * (2 + param_count);
        if (offset + expected_size > chunk_->signature_blob.size()) {
            throw std::runtime_error("Signature blob truncated.");
        }
        return param_count;
    }

    const MethodDef* VM::find_method_def_by_name(const std::string& name,
                                                 bool is_static,
                                                 uint32_t param_count) const {
        if (!chunk_) {
            return nullptr;
        }
        if (chunk_->signature_blob.empty()) {
            return nullptr;
        }
        for (const auto& method : chunk_->method_definitions) {
            if (method.name >= chunk_->strings.size()) {
                continue;
            }
            if (chunk_->strings[method.name] != name) {
                continue;
            }
            bool method_static = (method.flags & static_cast<uint32_t>(MethodFlags::Static)) != 0;
            if (method_static != is_static) {
                continue;
            }
            if (read_signature_param_count(method.signature) == param_count) {
                return &method;
            }
        }
        return nullptr;
    }

    const MethodDef* VM::find_method_def_for_type(const TypeDef& type_def,
                                                  const std::string& name,
                                                  bool is_static,
                                                  uint32_t param_count) const {
        if (!chunk_) {
            return nullptr;
        }
        if (chunk_->signature_blob.empty()) {
            return nullptr;
        }
        size_t start = type_def.method_list.start;
        size_t end = start + type_def.method_list.count;
        if (end > chunk_->method_definitions.size()) {
            return nullptr;
        }
        for (size_t i = start; i < end; ++i) {
            const auto& method = chunk_->method_definitions[i];
            if (method.name >= chunk_->strings.size()) {
                continue;
            }
            if (chunk_->strings[method.name] != name) {
                continue;
            }
            bool method_static = (method.flags & static_cast<uint32_t>(MethodFlags::Static)) != 0;
            if (method_static != is_static) {
                continue;
            }
            if (read_signature_param_count(method.signature) == param_count) {
                return &method;
            }
        }
        return nullptr;
    }

    const PropertyDef* VM::find_property_def_for_type(const TypeDef& type_def,
                                                      const std::string& name,
                                                      bool is_static) const {
        if (!chunk_) {
            return nullptr;
        }
        size_t start = type_def.property_list.start;
        size_t end = start + type_def.property_list.count;
        if (end > chunk_->property_definitions.size()) {
            return nullptr;
        }
        for (size_t i = start; i < end; ++i) {
            const auto& prop = chunk_->property_definitions[i];
            if (prop.name >= chunk_->strings.size()) {
                continue;
            }
            if (chunk_->strings[prop.name] != name) {
                continue;
            }
            bool prop_static = (prop.flags & static_cast<uint32_t>(PropertyFlags::Static)) != 0;
            if (prop_static != is_static) {
                continue;
            }
            return &prop;
        }
        return nullptr;
    }

    const FieldDef* VM::find_field_def_for_type(const TypeDef& type_def,
                                                const std::string& name,
                                                bool is_static) const {
        if (!chunk_) {
            return nullptr;
        }
        size_t start = type_def.field_list.start;
        size_t end = start + type_def.field_list.count;
        if (end > chunk_->field_definitions.size()) {
            return nullptr;
        }
        for (size_t i = start; i < end; ++i) {
            const auto& field = chunk_->field_definitions[i];
            if (field.name >= chunk_->strings.size()) {
                continue;
            }
            if (chunk_->strings[field.name] != name) {
                continue;
            }
            bool field_static = (field.flags & static_cast<uint32_t>(FieldFlags::Static)) != 0;
            if (field_static != is_static) {
                continue;
            }
            return &field;
        }
        return nullptr;
    }

    const MethodDef* VM::resolve_method_def_by_index(method_idx idx) const {
        if (!chunk_ || idx >= chunk_->method_definitions.size()) {
            return nullptr;
        }
        return &chunk_->method_definitions[idx];
    }

    bool VM::invoke_method_def(const MethodDef& method_def,
                               size_t callee_index,
                               uint16_t arg_count,
                               bool has_receiver,
                               bool is_initializer,
                               bool is_mutating,
                               size_t receiver_index) {
        if (!chunk_) {
            throw std::runtime_error("No active chunk for method invocation.");
        }
        uint32_t param_count = read_signature_param_count(method_def.signature);
        uint32_t expected = param_count + (has_receiver ? 1u : 0u);
        if (arg_count != expected) {
            throw std::runtime_error("Incorrect argument count.");
        }
        if (method_def.body_ptr == std::numeric_limits<body_idx>::max() ||
            method_def.body_ptr >= chunk_->method_bodies.size()) {
            throw std::runtime_error("Method body not found.");
        }
        std::string method_name = (method_def.name < chunk_->strings.size())
            ? chunk_->strings[method_def.name]
            : std::string("method");
        call_frames_.emplace_back(
            callee_index + 1,
            ip_,
            chunk_,
            current_body_idx_,
            method_name,
            nullptr,
            is_initializer,
            is_mutating,
            receiver_index);
        set_active_body(method_def.body_ptr);
        ip_ = 0;
        return true;
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

    const TypeDef* VM::resolve_type_def(const std::string& name) const {
        if (!chunk_) {
            return nullptr;
        }
        for (const auto& def : chunk_->type_definitions) {
            if (def.name < chunk_->strings.size() && chunk_->strings[def.name] == name) {
                return &def;
            }
        }
        return nullptr;
    }

    bool VM::matches_type(const Value& value, const std::string& type_name) const {
        if (type_name == "Int") {
            return value.is_int();
        }
        if (type_name == "Float") {
            return value.is_float();
        }
        if (type_name == "Bool") {
            return value.is_bool();
        }
        if (type_name == "String") {
            return value.is_object() && value.as_object() &&
                value.as_object()->type == ObjectType::String;
        }
        if (type_name == "Array") {
            return value.is_object() && value.as_object() &&
                value.as_object()->type == ObjectType::List;
        }
        if (type_name == "Dictionary") {
            return value.is_object() && value.as_object() &&
                value.as_object()->type == ObjectType::Map;
        }
        if (type_name == "Void") {
            return value.is_null();
        }
        if (type_name == "Any") {
            return !value.is_null() && !value.is_undefined();
        }

        const TypeDef* type_def = resolve_type_def(type_name);
        if (!type_def || !value.is_object() || !value.as_object()) {
            return false;
        }

        auto has_flag = [](uint32_t flags, TypeFlags flag) {
            return (flags & static_cast<uint32_t>(flag)) != 0;
        };

        Object* obj = value.as_object();
        if (has_flag(type_def->flags, TypeFlags::Class) && obj->type == ObjectType::Instance) {
            auto* inst = static_cast<InstanceObject*>(obj);
            ClassObject* klass = inst->klass;
            while (klass) {
                if (klass->name == type_name) {
                    return true;
                }
                klass = klass->superclass;
            }
            return false;
        }

        if (has_flag(type_def->flags, TypeFlags::Struct) && obj->type == ObjectType::StructInstance) {
            auto* struct_inst = static_cast<StructInstanceObject*>(obj);
            return struct_inst->struct_type && struct_inst->struct_type->name == type_name;
        }

        if (has_flag(type_def->flags, TypeFlags::Enum) && obj->type == ObjectType::EnumCase) {
            auto* enum_case = static_cast<EnumCaseObject*>(obj);
            return enum_case->enum_type && enum_case->enum_type->name == type_name;
        }

        return false;
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
            if (inst->klass) {
                const TypeDef* type_def = resolve_type_def(inst->klass->name);
                if (type_def) {
                    const FieldDef* field_def = find_field_def_for_type(*type_def, name, false);
                    if (field_def) {
                        auto field_it = inst->fields.find(name);
                        if (field_it != inst->fields.end()) {
                            return field_it->second;
                        }
                        return Value::null();
                    }
                }
            }
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
            
            // Check static methods first
            auto static_method_it = klass->static_methods.find(name);
            if (static_method_it != klass->static_methods.end()) {
                return static_method_it->second;
            }
            
            // Check static properties
            if (const TypeDef* type_def = resolve_type_def(klass->name)) {
                if (find_field_def_for_type(*type_def, name, true)) {
                    auto static_prop_it = klass->static_properties.find(name);
                    if (static_prop_it != klass->static_properties.end()) {
                        return static_prop_it->second;
                    }
                    return Value::null();
                }
            }
            auto static_prop_it = klass->static_properties.find(name);
            if (static_prop_it != klass->static_properties.end()) {
                return static_prop_it->second;
            }
            
            // Check instance methods
            Value method_value;
            if (find_method_on_class(klass, name, method_value)) {
                return method_value;
            }
            return Value::null();
        }

        // Struct instance property access
        if (obj->type == ObjectType::StructInstance) {
            auto* inst = static_cast<StructInstanceObject*>(obj);
            if (inst->struct_type) {
                const TypeDef* type_def = resolve_type_def(inst->struct_type->name);
                if (type_def) {
                    const FieldDef* field_def = find_field_def_for_type(*type_def, name, false);
                    if (field_def) {
                        auto field_it = inst->fields.find(name);
                        if (field_it != inst->fields.end()) {
                            return field_it->second;
                        }
                        return Value::null();
                    }
                }
            }
            auto field_it = inst->fields.find(name);
            if (field_it != inst->fields.end()) {
                return field_it->second;
            }
            // Check for methods on the struct type
            if (inst->struct_type) {
                auto method_it = inst->struct_type->methods.find(name);
                if (method_it != inst->struct_type->methods.end()) {
                    // Create a bound method
                    // Check if it's a mutating method
                    bool is_mutating = false;
                    auto mutating_it = inst->struct_type->mutating_methods.find(name);
                    if (mutating_it != inst->struct_type->mutating_methods.end()) {
                        is_mutating = mutating_it->second;
                    }
                    
                    if (is_mutating) {
                        // For mutating methods, bind to the original instance
                        // The VM will handle copying it back after the call
                        auto* bound = allocate_object<BoundMethodObject>(inst, method_it->second, true);
                        return Value::from_object(bound);
                    } else {
                        // For non-mutating methods, copy the instance for value semantics
                        auto* copy = inst->deep_copy(*this);
                        auto* bound = allocate_object<BoundMethodObject>(copy, method_it->second, false);
                        return Value::from_object(bound);
                    }
                }
            }
            return Value::null();
        }

        // Struct type property access (static methods and properties)
        if (obj->type == ObjectType::Struct) {
            auto* struct_type = static_cast<StructObject*>(obj);
            
            // Check static methods first
            auto static_method_it = struct_type->static_methods.find(name);
            if (static_method_it != struct_type->static_methods.end()) {
                return static_method_it->second;
            }
            
            // Check static properties
            if (const TypeDef* type_def = resolve_type_def(struct_type->name)) {
                if (find_field_def_for_type(*type_def, name, true)) {
                    auto static_prop_it = struct_type->static_properties.find(name);
                    if (static_prop_it != struct_type->static_properties.end()) {
                        return static_prop_it->second;
                    }
                    return Value::null();
                }
            }
            auto static_prop_it = struct_type->static_properties.find(name);
            if (static_prop_it != struct_type->static_properties.end()) {
                return static_prop_it->second;
            }
            
            // Check instance methods (for compatibility)
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

        // Tuple label access
        if (obj->type == ObjectType::Tuple) {
            auto* tuple = static_cast<TupleObject*>(obj);
            Value result = tuple->get(name);
            if (!result.is_null() || tuple->has_label(name)) {
                return result;
            }
            throw std::runtime_error("Tuple has no element with label '" + name + "'");
        }

        throw std::runtime_error("Property access supported only on arrays, maps, and instances.");
    }

    std::optional<Value> VM::call_operator_overload(const Value& left, const Value& right, const std::string& name) {
        if (!left.is_object() || !left.as_object()) {
            return std::nullopt;
        }

        Value method_value = Value::null();
        try {
            method_value = get_property(left, name);
        } catch (const std::exception&) {
            return std::nullopt;
        }

        if (method_value.is_null() || !method_value.is_object() || !method_value.as_object()) {
            return std::nullopt;
        }

        Object* obj = method_value.as_object();
        FunctionObject* func = nullptr;
        ClosureObject* closure = nullptr;
        std::vector<Value> args;

        if (obj->type == ObjectType::BoundMethod) {
            auto* bound = static_cast<BoundMethodObject*>(obj);
            args.push_back(Value::from_object(bound->receiver));
            args.push_back(right);
            Value bound_method = bound->method;
            if (!bound_method.is_object() || !bound_method.as_object()) {
                return std::nullopt;
            }
            Object* bound_obj = bound_method.as_object();
            if (bound_obj->type == ObjectType::Closure) {
                closure = static_cast<ClosureObject*>(bound_obj);
                func = closure->function;
            } else if (bound_obj->type == ObjectType::Function) {
                func = static_cast<FunctionObject*>(bound_obj);
            } else {
                return std::nullopt;
            }
            return execute_function(func, closure, args);
        }

        if (obj->type == ObjectType::Closure) {
            closure = static_cast<ClosureObject*>(obj);
            func = closure->function;
            args.push_back(left);
            args.push_back(right);
            return execute_function(func, closure, args);
        }

        if (obj->type == ObjectType::Function) {
            func = static_cast<FunctionObject*>(obj);
            args.push_back(left);
            args.push_back(right);
            return execute_function(func, nullptr, args);
        }

        return std::nullopt;
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

    void VM::build_param_defaults(const FunctionPrototype& proto,
                                  std::vector<Value>& defaults,
                                  std::vector<bool>& has_defaults) {
        defaults.clear();
        has_defaults.clear();
        defaults.reserve(proto.param_defaults.size());
        has_defaults.reserve(proto.param_defaults.size());

        for (const auto& def : proto.param_defaults) {
            has_defaults.push_back(def.has_default);
            if (!def.has_default) {
                defaults.push_back(Value::null());
                continue;
            }

            Value value = def.value;
            if (def.string_value.has_value()) {
                auto* str_obj = allocate_object<StringObject>(*def.string_value);
                value = Value::from_object(str_obj);
            }

            if (value.is_object() && value.ref_type() == RefType::Strong && value.as_object()) {
                RC::retain(value.as_object());
            }
            defaults.push_back(value);
        }
    }

    void VM::apply_positional_defaults(uint16_t& arg_count,
                                       FunctionObject* func,
                                       bool has_receiver) {
        size_t offset = has_receiver ? 1 : 0;
        if (func->params.size() < offset) {
            throw std::runtime_error("Invalid parameter metadata.");
        }

        size_t expected = func->params.size() - offset;
        size_t provided = has_receiver ? (arg_count - 1) : arg_count;

        if (provided > expected) {
            return;
        }

        for (size_t i = provided; i < expected; ++i) {
            size_t param_index = i + offset;
            if (param_index >= func->param_has_default.size() || !func->param_has_default[param_index]) {
                return;
            }
            Value def = func->param_defaults[param_index];
            push(def);
        }

        arg_count = static_cast<uint16_t>(expected + offset);
    }

    void VM::apply_named_arguments(size_t callee_index,
                                   uint16_t& arg_count,
                                   FunctionObject* func,
                                   bool has_receiver,
                                   const std::vector<std::optional<std::string>>& arg_names) {
        size_t offset = has_receiver ? 1 : 0;
        if (func->params.size() < offset) {
            throw std::runtime_error("Invalid parameter metadata.");
        }

        size_t expected = func->params.size() - offset;
        size_t provided = arg_names.size();
        if (provided > expected) {
            throw std::runtime_error("Too many arguments.");
        }

        struct ArgValue {
            Value value;
            bool needs_retain;
        };

        std::vector<ArgValue> final_args(expected, {Value::null(), false});
        std::vector<bool> filled(expected, false);

        size_t next_pos = 0;
        size_t start_index = callee_index + 1 + offset;

        for (size_t i = 0; i < provided; ++i) {
            size_t target = 0;
            if (arg_names[i].has_value()) {
                const std::string& name = arg_names[i].value();
                bool found = false;
                for (size_t j = 0; j < expected; ++j) {
                    size_t param_index = j + offset;
                    if (param_index < func->param_labels.size() &&
                        func->param_labels[param_index] == name &&
                        !func->param_labels[param_index].empty()) {
                        target = j;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    throw std::runtime_error("Unknown named argument: " + name);
                }
            } else {
                while (next_pos < expected && filled[next_pos]) {
                    ++next_pos;
                }
                if (next_pos >= expected) {
                    throw std::runtime_error("Too many positional arguments.");
                }
                target = next_pos++;
            }

            if (filled[target]) {
                throw std::runtime_error("Duplicate argument for parameter.");
            }

            final_args[target] = {stack_[start_index + i], false};
            filled[target] = true;
        }

        for (size_t i = 0; i < expected; ++i) {
            if (filled[i]) {
                continue;
            }
            size_t param_index = i + offset;
            if (param_index >= func->param_has_default.size() || !func->param_has_default[param_index]) {
                throw std::runtime_error("Missing argument for parameter.");
            }
            final_args[i] = {func->param_defaults[param_index], true};
            filled[i] = true;
        }

        for (size_t i = provided; i < expected; ++i) {
            push(Value::null());
        }

        for (size_t i = 0; i < expected; ++i) {
            if (final_args[i].needs_retain &&
                final_args[i].value.is_object() &&
                final_args[i].value.ref_type() == RefType::Strong &&
                final_args[i].value.as_object()) {
                RC::retain(final_args[i].value.as_object());
            }
            stack_[start_index + i] = final_args[i].value;
        }

        arg_count = static_cast<uint16_t>(expected + offset);
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

    void VM::call_property_observer(Value observer, Value instance, Value argument) {
        if (observer.is_null() || !observer.is_object()) {
            return;  // No observer to call
        }
        
        Object* obj = observer.as_object();
        FunctionObject* func = nullptr;
        ClosureObject* closure = nullptr;
        
        if (obj->type == ObjectType::Closure) {
            closure = static_cast<ClosureObject*>(obj);
            func = closure->function;
        } else if (obj->type == ObjectType::Function) {
            func = static_cast<FunctionObject*>(obj);
        } else {
            return;  // Not a callable
        }
        
        if (!func || !func->chunk || func->params.size() != 2) {
            return;  // Invalid observer function
        }
        
        // Use the new execute_function helper
        std::vector<Value> args = {instance, argument};
        execute_function(func, closure, args);
    }

    Value VM::execute_function(FunctionObject* func, ClosureObject* closure, const std::vector<Value>& args) {
        if (!func || !func->chunk) {
            throw std::runtime_error("Invalid function to execute");
        }
        
        // Save current execution state
        const Assembly* saved_chunk = chunk_;
        size_t saved_ip = ip_;
        body_idx saved_body = current_body_idx_;
        const MethodBody* saved_body_ptr = current_body_;
        
        // Push function and arguments onto stack
        if (closure) {
            push(Value::from_object(closure));
        } else {
            push(Value::from_object(func));
        }
        
        for (const auto& arg : args) {
            push(arg);
        }
        
        // Calculate callee index
        size_t callee_index = stack_.size() - args.size() - 1;
        
        // Create call frame
        call_frames_.emplace_back(callee_index + 1, saved_ip, saved_chunk, saved_body, func->name, closure, false);
        
        // Execute the function
        chunk_ = func->chunk.get();
        current_body_idx_ = entry_body_index(*chunk_);
        set_active_body(current_body_idx_);
        ip_ = 0;
        
        Value result = Value::null();
        
        try {
            // Execute until OP_RETURN
            while (ip_ < active_bytecode().size()) {
                OpCode op = static_cast<OpCode>(read_byte());
                
                if (op == OpCode::OP_RETURN) {
                    // Function is returning
                    result = pop();  // Get return value
                    
                    // Clean up call frame
                    CallFrame& frame = call_frames_.back();
                    
                    // Close upvalues
                    if (stack_.size() > frame.stack_base) {
                        close_upvalues(&stack_[frame.stack_base]);
                    }
                    
                    // Restore execution state
                    chunk_ = frame.chunk;
                    ip_ = frame.return_address;
                    current_body_idx_ = frame.body_index;
                    set_active_body(current_body_idx_);
                    
                    // Pop call frame
                    call_frames_.pop_back();
                    
                    // Clean up stack (remove callee and args)
                    while (stack_.size() > callee_index) {
                        pop();
                    }
                    
                    return result;
                }
                
                // Handle other opcodes by executing them inline
                // We need to step back because read_byte() already consumed the OPCODE
                ip_--;
                
                // Execute this one instruction
                switch (static_cast<OpCode>(active_bytecode()[ip_])) {
                    case OpCode::OP_READ_LINE: {
                        ip_++;
                        std::string line;
                        if (!std::getline(std::cin, line)) {
                            push(Value::null());
                            break;
                        }
                        auto* obj = allocate_object<StringObject>(std::move(line));
                        push(Value::from_object(obj));
                        break;
                    }
                    case OpCode::OP_PRINT: {
                        ip_++;  // consume OPCODE
                        Value val = pop();
                        std::cout << val.to_string() << "\n";
                        break;
                    }
                    case OpCode::OP_POP:
                        ip_++;
                        pop();
                        break;
                    case OpCode::OP_GET_LOCAL: {
                        ip_++;
                        uint16_t slot = read_short();
                        CallFrame& frame = call_frames_.back();
                        push(stack_[frame.stack_base + slot]);
                        break;
                    }
                    case OpCode::OP_GET_PROPERTY: {
                        ip_++;
                        const std::string& name = read_string();
                        Value obj = pop();
                        push(get_property(obj, name));
                        break;
                    }
                    case OpCode::OP_NIL:
                        ip_++;
                        push(Value::null());
                        break;
                    case OpCode::OP_CONSTANT:
                        ip_++;
                        push(read_constant());
                        break;
                    case OpCode::OP_STRING: {
                        ip_++;
                        const std::string& str = read_string();
                        auto* obj = allocate_object<StringObject>(str);
                        push(Value::from_object(obj));
                        break;
                    }
                    case OpCode::OP_TRUE:
                        ip_++;
                        push(Value::from_bool(true));
                        break;
                    case OpCode::OP_FALSE:
                        ip_++;
                        push(Value::from_bool(false));
                        break;
                    default:
                        // For other opcodes, we need to handle them
                        // This is a simplified version - ideally we'd refactor run() to handle one OPCODE at a time
                        throw std::runtime_error("Unsupported opcode in nested function execution");
                }
            }
            
        } catch (...) {
            // Restore state on error
            chunk_ = saved_chunk;
            ip_ = saved_ip;
            current_body_idx_ = saved_body;
            current_body_ = saved_body_ptr;
            
            // Pop call frame if still there
            if (!call_frames_.empty() && call_frames_.back().function_name == func->name) {
                call_frames_.pop_back();
            }
            
            // Clean up stack
            while (stack_.size() > callee_index) {
                pop();
            }
            
            throw;
        }
        
        return result;
    }

} // namespace swiftscript
