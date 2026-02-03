#pragma once
#define OPCODE(T) template<> struct OpCodeHandler<T>
#define OP_BODY static void execute(VM& vm)
#define OP_HANDLER_IMP(T) tbl[static_cast<uint8_t>(T)] = &OpCodeHandler<T>::execute
#define OPCODE_DEFAULT(T) template<> struct OpCodeHandler<T> { OP_BODY { return; } }; 

#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <system_error>
#include <string_view>
#include "ss_opcodes.hpp"
#include "ss_vm.hpp"

namespace swiftscript {

    // Note: primary template `OpCodeHandler` and `make_handler_table` are
    // declared in `ss_vm.hpp`. This file provides explicit specializations
    // only. Do not redeclare the primary template or pull the namespace into
    // global scope.
    // Build the handler table by taking pointers to each OpCodeHandler::execute

    namespace {
        std::string_view trim_view(std::string_view text) {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
                text.remove_prefix(1);
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
                text.remove_suffix(1);
            }
            return text;
        }

        std::string to_lower_copy(std::string_view text) {
            std::string out;
            out.reserve(text.size());
            for (unsigned char ch : text) {
                out.push_back(static_cast<char>(std::tolower(ch)));
            }
            return out;
        }

        Int parse_int(std::string_view text) {
            std::string_view trimmed = trim_view(text);
            if (trimmed.empty()) {
                throw std::runtime_error("Int() failed: empty string.");
            }
            Int result = 0;
            const char* start = trimmed.data();
            const char* end = trimmed.data() + trimmed.size();
            auto [ptr, ec] = std::from_chars(start, end, result);
            if (ec != std::errc() || ptr != end) {
                throw std::runtime_error("Int() failed: invalid integer string.");
            }
            return result;
        }

        Float parse_float(std::string_view text) {
            std::string_view trimmed = trim_view(text);
            if (trimmed.empty()) {
                throw std::runtime_error("Float() failed: empty string.");
            }
            std::string temp(trimmed);
            char* end = nullptr;
            errno = 0;
            double value = std::strtod(temp.c_str(), &end);
            if (end != temp.c_str() + temp.size() || errno == ERANGE) {
                throw std::runtime_error("Float() failed: invalid float string.");
            }
            return static_cast<Float>(value);
        }

        Bool parse_bool(std::string_view text) {
            std::string_view trimmed = trim_view(text);
            if (trimmed.empty()) {
                throw std::runtime_error("Bool() failed: empty string.");
            }
            std::string lowered = to_lower_copy(trimmed);
            if (lowered == "true" || lowered == "1") {
                return true;
            }
            if (lowered == "false" || lowered == "0") {
                return false;
            }
            throw std::runtime_error("Bool() failed: invalid boolean string.");
        }

        Value convert_builtin(VM& vm, const std::string& name, const Value& input) {
            if (name == "String") {
                std::string text = input.to_string();
                Object* obj = vm.allocate_object<StringObject>(std::move(text));
                return Value::from_object(obj);
            }
            if (name == "Int") {
                if (input.is_int()) {
                    return input;
                }
                if (input.is_float()) {
                    return Value::from_int(static_cast<Int>(input.as_float()));
                }
                if (input.is_bool()) {
                    return Value::from_int(input.as_bool() ? 1 : 0);
                }
                if (input.is_object() && input.as_object() &&
                    input.as_object()->type == ObjectType::String) {
                    const auto& text = static_cast<StringObject*>(input.as_object())->data;
                    return Value::from_int(parse_int(text));
                }
                throw std::runtime_error("Int() failed: unsupported input type.");
            }
            if (name == "Float") {
                if (input.is_float()) {
                    return input;
                }
                if (input.is_int()) {
                    return Value::from_float(static_cast<Float>(input.as_int()));
                }
                if (input.is_bool()) {
                    return Value::from_float(input.as_bool() ? static_cast<Float>(1.0) : static_cast<Float>(0.0));
                }
                if (input.is_object() && input.as_object() &&
                    input.as_object()->type == ObjectType::String) {
                    const auto& text = static_cast<StringObject*>(input.as_object())->data;
                    return Value::from_float(parse_float(text));
                }
                throw std::runtime_error("Float() failed: unsupported input type.");
            }
            if (name == "Bool") {
                if (input.is_bool()) {
                    return input;
                }
                if (input.is_int()) {
                    return Value::from_bool(input.as_int() != 0);
                }
                if (input.is_float()) {
                    return Value::from_bool(input.as_float() != static_cast<Float>(0.0));
                }
                if (input.is_object() && input.as_object() &&
                    input.as_object()->type == ObjectType::String) {
                    const auto& text = static_cast<StringObject*>(input.as_object())->data;
                    return Value::from_bool(parse_bool(text));
                }
                throw std::runtime_error("Bool() failed: unsupported input type.");
            }
            throw std::runtime_error("Unknown builtin conversion: " + name);
        }
    } // namespace

    template<>
    struct OpCodeHandler<OpCode::OP_CONSTANT> {
        static void execute(VM& vm) {
            vm.push(vm.read_constant());
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_STRING> {
        static void execute(VM& vm) {
            const std::string& str = vm.read_string();
            Object* str_obj = vm.allocate_object<StringObject>(str);
            vm.push(Value::from_object(str_obj));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_NIL> {
        static void execute(VM& vm) {
            vm.push(Value::null());
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_TRUE> {
        static void execute(VM& vm) {
            vm.push(Value::from_bool(true));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_FALSE> {
        static void execute(VM& vm) {
            vm.push(Value::from_bool(false));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_POP> {
        static void execute(VM& vm) {
            vm.pop();
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_DUP> {
        static void execute(VM& vm) {
            vm.push(vm.peek(0));
        }
    };

    OPCODE(OpCode::OP_CALL)
    {
        OP_BODY
        {
			VM* self = &vm;
            uint16_t arg_count = vm.read_short();
            if (vm.stack_.size() < arg_count + 1) {
                throw std::runtime_error("Not enough values for function call.");
            }
            size_t callee_index = vm.stack_.size() - arg_count - 1;
            Value callee = vm.stack_[callee_index];
            bool has_receiver = false;

            if (!callee.is_object() || !callee.as_object()) {
                throw std::runtime_error("Attempted to call a non-function.");
            }

            Object* obj = callee.as_object();

            // EnumCase call -> create instance with associated values
            if (obj->type == ObjectType::EnumCase) {
                auto* template_case = static_cast<EnumCaseObject*>(obj);
                size_t expected = template_case->associated_labels.size();
                if (arg_count != expected) {
                    throw std::runtime_error("Incorrect argument count for enum case.");
                }

                // Create a new enum case instance with associated values
                auto* new_case = vm.allocate_object<EnumCaseObject>(
                    template_case->enum_type,
                    template_case->case_name
                );
                new_case->raw_value = template_case->raw_value;
                new_case->associated_labels = template_case->associated_labels;

                // Collect associated values from arguments
                for (size_t i = 0; i < arg_count; ++i) {
                    Value arg = vm.stack_[callee_index + 1 + i];
                    if (arg.is_object() && arg.ref_type() == RefType::Strong && arg.as_object()) {
                        RC::retain(arg.as_object());
                    }
                    new_case->associated_values.push_back(arg);
                }

                // Pop arguments and callee, push new instance
                while (vm.stack_.size() > callee_index) {
                    vm.pop();
                }
                vm.push(Value::from_object(new_case));
				return;
            }

            // Struct call -> instantiate (value type)
            if (obj->type == ObjectType::Struct) {
                auto* struct_type = static_cast<StructObject*>(obj);
                auto* instance = vm.allocate_object<StructInstanceObject>(struct_type);

                // Initialize properties with default values
                for (const auto& property : struct_type->properties) {
                    Value prop_value = property.default_value;
                    if (prop_value.is_object() && prop_value.ref_type() == RefType::Strong && prop_value.as_object()) {
                        RC::retain(prop_value.as_object());
                    }
                    instance->fields[property.name] = prop_value;
                }

                // Replace callee with instance
                Value old_callee = vm.stack_[callee_index];
                if (old_callee.is_object() && old_callee.ref_type() == RefType::Strong && old_callee.as_object()) {
                    RC::release(self, old_callee.as_object());
                }
                vm.stack_[callee_index] = Value::from_object(instance);
                RC::adopt(instance);

                // Check for init method
                auto it = struct_type->methods.find("init");
                if (it == struct_type->methods.end()) {
                    // No init: check for memberwise initializer pattern
                    // If arguments match property count, do memberwise init
                    if (arg_count == struct_type->properties.size()) {
                        // Memberwise initializer
                        for (size_t i = 0; i < arg_count; ++i) {
                            Value arg = vm.stack_[callee_index + 1 + i];
                            const std::string& prop_name = struct_type->properties[i].name;
                            if (arg.is_object() && arg.ref_type() == RefType::Strong && arg.as_object()) {
                                RC::retain(arg.as_object());
                            }
                            instance->fields[prop_name] = arg;
                        }
                        // Discard args using pop(), leave instance on stack
                        while (vm.stack_.size() > callee_index + 1) {
                            vm.pop();
                        }
                        return;
                    }
                    else if (arg_count == 0) {
                        // No args, no init - just use defaults
                        return;
                    }
                    else {
                        throw std::runtime_error("Struct '" + struct_type->name +
                            "' has no init and argument count doesn't match property count.");
                    }
                }

                // Bind init as bound method for struct
                auto* bound = vm.allocate_object<BoundMethodObject>(instance, it->second);
                Value old_instance = vm.stack_[callee_index];
                if (old_instance.is_object() && old_instance.ref_type() == RefType::Strong && old_instance.as_object()) {
                    RC::release(self, old_instance.as_object());
                }
                vm.stack_[callee_index] = Value::from_object(bound);
                RC::adopt(bound);
                callee = vm.stack_[callee_index];
                obj = callee.as_object();
            }

            // Class call -> instantiate
            if (obj->type == ObjectType::Class) {
                auto* klass = static_cast<ClassObject*>(obj);
                auto* instance = vm.allocate_object<InstanceObject>(klass);

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
                // Adopt the creator ref so the stack slot becomes the owner.
                Value old_callee = vm.stack_[callee_index];
                if (old_callee.is_object() && old_callee.ref_type() == RefType::Strong && old_callee.as_object()) {
                    RC::release(self, old_callee.as_object());
                }
                vm.stack_[callee_index] = Value::from_object(instance);
                RC::adopt(instance);

                // Initializer?
                auto it = klass->methods.find("init");
                if (it == klass->methods.end()) {
                    // No init: discard args using pop(), leave instance on stack
                    while (vm.stack_.size() > callee_index + 1) {
                        vm.pop();
                    }
                    return;
                }
                // Bind init as bound method
                // BoundMethod retains the instance; release the stack slot when swapping.
                auto* bound = vm.allocate_object<BoundMethodObject>(instance, it->second);
                Value old_instance = vm.stack_[callee_index];
                if (old_instance.is_object() && old_instance.ref_type() == RefType::Strong && old_instance.as_object()) {
                    RC::release(self, old_instance.as_object());
                }
                vm.stack_[callee_index] = Value::from_object(bound);
                RC::adopt(bound);
                callee = vm.stack_[callee_index];
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
                    Value arg = vm.peek(0);

                    // Add element to array (retain for the array)
                    if (arg.is_object() && arg.ref_type() == RefType::Strong && arg.as_object()) {
                        RC::retain(arg.as_object());
                    }
                    arr->elements.push_back(arg);

                    // Clean up stack using pop()
                    while (vm.stack_.size() > callee_index) {
                        vm.pop();
                    }

                    // append returns nil
                    vm.push(Value::null());
                    return;
                }

                throw std::runtime_error("Unknown built-in method: " + method->method_name);
            }

            if (obj->type == ObjectType::Function) {
                auto* func = static_cast<FunctionObject*>(obj);
                if (!func->chunk && vm.is_builtin_type_name(func->name)) {
                    if (arg_count != 1) {
                        throw std::runtime_error(func->name + "() requires exactly 1 argument.");
                    }
                    Value input = vm.stack_[callee_index + 1];
                    Value result = convert_builtin(vm, func->name, input);
                    while (vm.stack_.size() > callee_index) {
                        vm.pop();
                    }
                    vm.push(result);
                    return;
                }
            }

            // Bound method: inject receiver
            if (obj->type == ObjectType::BoundMethod) {
                auto* bound = static_cast<BoundMethodObject*>(obj);

                // Insert receiver with retain (new stack slot needs ownership)
                Value receiver_val = Value::from_object(bound->receiver);
                if (receiver_val.is_object() && receiver_val.ref_type() == RefType::Strong && receiver_val.as_object()) {
                    RC::retain(receiver_val.as_object());
                }
                vm.stack_.insert(vm.stack_.begin() + static_cast<long>(callee_index + 1), receiver_val);
                arg_count += 1;

                // Store mutating info and receiver location for later
                bool is_mutating_call = bound->is_mutating;
                size_t receiver_stack_index = callee_index + 1;

                // Replace callee slot: BoundMethod -> method
                // Retain method first (it's referenced from bound which will be released)
                Value method_val = bound->method;
                if (method_val.is_object() && method_val.ref_type() == RefType::Strong && method_val.as_object()) {
                    RC::retain(method_val.as_object());
                }
                // Now release BoundMethod from callee slot
                Value old_bound = vm.stack_[callee_index];
                vm.stack_[callee_index] = method_val;  // Replace first to avoid use-after-free
                if (old_bound.is_object() && old_bound.ref_type() == RefType::Strong && old_bound.as_object()) {
                    RC::release(self, old_bound.as_object());
                }
                callee = vm.stack_[callee_index];
                obj = callee.as_object();
                has_receiver = true;

                // Now handle the actual function call below, but remember mutating info
                // We'll pass this to the CallFrame
                FunctionObject* func = nullptr;
                ClosureObject* closure = nullptr;

                if (obj->type == ObjectType::Closure) {
                    closure = static_cast<ClosureObject*>(obj);
                    func = closure->function;
                }
                else if (obj->type == ObjectType::Function) {
                    func = static_cast<FunctionObject*>(obj);
                }
                else {
                    throw std::runtime_error("Attempted to call a non-function.");
                }

                vm.apply_positional_defaults(arg_count, func, has_receiver);

                uint32_t param_count = has_receiver ? static_cast<uint32_t>(arg_count - 1) : arg_count;
                const TypeDef* type_def = nullptr;
                if (bound->receiver->type == ObjectType::Instance) {
                    auto* inst = static_cast<InstanceObject*>(bound->receiver);
                    if (inst->klass) {
                        type_def = vm.resolve_type_def(inst->klass->name);
                    }
                } else if (bound->receiver->type == ObjectType::StructInstance) {
                    auto* inst = static_cast<StructInstanceObject*>(bound->receiver);
                    if (inst->struct_type) {
                        type_def = vm.resolve_type_def(inst->struct_type->name);
                    }
                } else if (bound->receiver->type == ObjectType::EnumCase) {
                    auto* inst = static_cast<EnumCaseObject*>(bound->receiver);
                    if (inst->enum_type) {
                        type_def = vm.resolve_type_def(inst->enum_type->name);
                    }
                }

                if (type_def) {
                    const MethodDef* method_def = vm.find_method_def_for_type(*type_def, func->name, false, param_count);
                    if (method_def) {
                        vm.invoke_method_def(*method_def, callee_index, arg_count, true, func->is_initializer, is_mutating_call, receiver_stack_index);
                        return;
                    }
                }

                if (!func->chunk) {
                    throw std::runtime_error("Function has no body.");
                }
                vm.call_frames_.emplace_back(callee_index + 1, vm.ip_, vm.chunk_, vm.current_body_idx_, func->name, closure, func->is_initializer, is_mutating_call, receiver_stack_index);
                vm.chunk_ = func->chunk.get();
                vm.current_body_idx_ = vm.entry_body_index(*vm.chunk_);
                vm.set_active_body(vm.current_body_idx_);
                vm.ip_ = 0;
                return;
            }

            FunctionObject* func = nullptr;
            ClosureObject* closure = nullptr;

            if (obj->type == ObjectType::Closure) {
                closure = static_cast<ClosureObject*>(obj);
                func = closure->function;
            }
            else if (obj->type == ObjectType::Function) {
                func = static_cast<FunctionObject*>(obj);
            }
            else {
                throw std::runtime_error("Attempted to call a non-function.");
            }

            vm.apply_positional_defaults(arg_count, func, has_receiver);

            uint32_t param_count = has_receiver ? static_cast<uint32_t>(arg_count - 1) : arg_count;
            const MethodDef* method_def = vm.find_method_def_by_name(func->name, true, param_count);
            if (!method_def) {
                method_def = vm.find_method_def_by_name(func->name, false, param_count);
            }
            if (method_def) {
                vm.invoke_method_def(*method_def, callee_index, arg_count, has_receiver, func->is_initializer);
                return;
            }

            if (arg_count != func->params.size()) {
                throw std::runtime_error("Incorrect argument count.");
            }
            if (!func->chunk) {
                throw std::runtime_error("Function has no body.");
            }
            vm.call_frames_.emplace_back(callee_index + 1, vm.ip_, vm.chunk_, vm.current_body_idx_, func->name, closure, func->is_initializer);
            vm.chunk_ = func->chunk.get();
            vm.current_body_idx_ = vm.entry_body_index(*vm.chunk_);
            vm.set_active_body(vm.current_body_idx_);
            vm.ip_ = 0;
        }
    };

    OPCODE(OpCode::OP_CALL_NAMED)
    {
        OP_BODY
        {
			VM* self = &vm;
            uint16_t arg_count = vm.read_short();
            std::vector<std::optional<std::string>> arg_names;
            arg_names.reserve(arg_count);
            for (uint16_t i = 0; i < arg_count; ++i) {
                uint16_t name_idx = vm.read_short();
                if (name_idx == std::numeric_limits<uint16_t>::max()) {
                    arg_names.emplace_back(std::nullopt);
                    continue;
                }
                if (name_idx >= vm.chunk_->string_table.size()) {
                    throw std::runtime_error("Argument name index out of range.");
                }
                arg_names.emplace_back(vm.chunk_->string_table[name_idx]);
            }

            if (vm.stack_.size() < arg_count + 1) {
                throw std::runtime_error("Not enough values for function call.");
            }
            size_t callee_index = vm.stack_.size() - arg_count - 1;
            Value callee = vm.stack_[callee_index];
            bool has_receiver = false;

            if (!callee.is_object() || !callee.as_object()) {
                throw std::runtime_error("Attempted to call a non-function.");
            }

            Object* obj = callee.as_object();

            // EnumCase call -> create instance with associated values
            if (obj->type == ObjectType::EnumCase) {
                auto* template_case = static_cast<EnumCaseObject*>(obj);
                size_t expected = template_case->associated_labels.size();
                if (arg_count != expected) {
                    throw std::runtime_error("Incorrect argument count for enum case.");
                }

                std::vector<Value> ordered_args(expected);
                std::vector<bool> filled(expected, false);
                size_t next_pos = 0;

                for (size_t i = 0; i < arg_count; ++i) {
                    size_t target = 0;
                    if (arg_names[i].has_value()) {
                        const std::string& name = arg_names[i].value();
                        bool found = false;
                        for (size_t j = 0; j < expected; ++j) {
                            if (!template_case->associated_labels[j].empty() &&
                                template_case->associated_labels[j] == name) {
                                target = j;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            throw std::runtime_error("Unknown enum case label: " + name);
                        }
                    }
                    else {
                        while (next_pos < expected && filled[next_pos]) {
                            ++next_pos;
                        }
                        if (next_pos >= expected) {
                            throw std::runtime_error("Too many positional arguments.");
                        }
                        target = next_pos++;
                    }
                    if (filled[target]) {
                        throw std::runtime_error("Duplicate enum case argument.");
                    }
                    ordered_args[target] = vm.stack_[callee_index + 1 + i];
                    filled[target] = true;
                }

                for (size_t i = 0; i < expected; ++i) {
                    if (!filled[i]) {
                        throw std::runtime_error("Missing enum case argument.");
                    }
                }

                auto* new_case = vm.allocate_object<EnumCaseObject>(
                    template_case->enum_type,
                    template_case->case_name);
                new_case->raw_value = template_case->raw_value;
                new_case->associated_labels = template_case->associated_labels;

                for (const auto& arg : ordered_args) {
                    if (arg.is_object() && arg.ref_type() == RefType::Strong && arg.as_object()) {
                        RC::retain(arg.as_object());
                    }
                    new_case->associated_values.push_back(arg);
                }

                while (vm.stack_.size() > callee_index) {
                    vm.pop();
                }
                vm.push(Value::from_object(new_case));
                return;
            }

            // Struct call -> instantiate (value type)
            if (obj->type == ObjectType::Struct) {
                auto* struct_type = static_cast<StructObject*>(obj);
                auto* instance = vm.allocate_object<StructInstanceObject>(struct_type);

                // Initialize properties with default values
                for (const auto& property : struct_type->properties) {
                    Value prop_value = property.default_value;
                    if (prop_value.is_object() && prop_value.ref_type() == RefType::Strong && prop_value.as_object()) {
                        RC::retain(prop_value.as_object());
                    }
                    instance->fields[property.name] = prop_value;
                }

                Value old_callee = vm.stack_[callee_index];
                if (old_callee.is_object() && old_callee.ref_type() == RefType::Strong && old_callee.as_object()) {
                    RC::release(self, old_callee.as_object());
                }
                vm.stack_[callee_index] = Value::from_object(instance);
                RC::adopt(instance);

                auto it = struct_type->methods.find("init");
                if (it == struct_type->methods.end()) {
                    bool has_named = std::any_of(arg_names.begin(), arg_names.end(),
                        [](const auto& name) { return name.has_value(); });
                    if (arg_count == struct_type->properties.size()) {
                        std::vector<Value> ordered_args(arg_count);
                        std::vector<bool> filled(arg_count, false);
                        size_t next_pos = 0;
                        for (size_t i = 0; i < arg_count; ++i) {
                            size_t target = 0;
                            if (has_named && arg_names[i].has_value()) {
                                const std::string& name = arg_names[i].value();
                                bool found = false;
                                for (size_t j = 0; j < struct_type->properties.size(); ++j) {
                                    if (struct_type->properties[j].name == name) {
                                        target = j;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    throw std::runtime_error("Unknown memberwise argument: " + name);
                                }
                            }
                            else {
                                while (next_pos < arg_count && filled[next_pos]) {
                                    ++next_pos;
                                }
                                if (next_pos >= arg_count) {
                                    throw std::runtime_error("Too many positional arguments.");
                                }
                                target = next_pos++;
                            }
                            if (filled[target]) {
                                throw std::runtime_error("Duplicate memberwise argument.");
                            }
                            ordered_args[target] = vm.stack_[callee_index + 1 + i];
                            filled[target] = true;
                        }

                        for (size_t i = 0; i < arg_count; ++i) {
                            Value arg = ordered_args[i];
                            const std::string& prop_name = struct_type->properties[i].name;
                            if (arg.is_object() && arg.ref_type() == RefType::Strong && arg.as_object()) {
                                RC::retain(arg.as_object());
                            }
                            instance->fields[prop_name] = arg;
                        }

                        while (vm.stack_.size() > callee_index + 1) {
                            vm.pop();
                        }
                        return;
                    }
                    else if (arg_count == 0) {
                        return;
                    }
                    else {
                        throw std::runtime_error("Struct '" + struct_type->name +
                            "' has no init and argument count doesn't match property count.");
                    }
                }

                auto* bound = vm.allocate_object<BoundMethodObject>(instance, it->second);
                Value old_instance = vm.stack_[callee_index];
                if (old_instance.is_object() && old_instance.ref_type() == RefType::Strong && old_instance.as_object()) {
                    RC::release(self, old_instance.as_object());
                }
                vm.stack_[callee_index] = Value::from_object(bound);
                RC::adopt(bound);
                callee = vm.stack_[callee_index];
                obj = callee.as_object();
            }

            // Class call -> instantiate
            if (obj->type == ObjectType::Class) {
                auto* klass = static_cast<ClassObject*>(obj);
                auto* instance = vm.allocate_object<InstanceObject>(klass);

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

                Value old_callee = vm.stack_[callee_index];
                if (old_callee.is_object() && old_callee.ref_type() == RefType::Strong && old_callee.as_object()) {
                    RC::release(self, old_callee.as_object());
                }
                vm.stack_[callee_index] = Value::from_object(instance);
                RC::adopt(instance);

                auto it = klass->methods.find("init");
                if (it == klass->methods.end()) {
                    while (vm.stack_.size() > callee_index + 1) {
                        vm.pop();
                    }
                    return;
                }
                auto* bound = vm.allocate_object<BoundMethodObject>(instance, it->second);
                Value old_instance = vm.stack_[callee_index];
                if (old_instance.is_object() && old_instance.ref_type() == RefType::Strong && old_instance.as_object()) {
                    RC::release(self, old_instance.as_object());
                }
                vm.stack_[callee_index] = Value::from_object(bound);
                RC::adopt(bound);
                callee = vm.stack_[callee_index];
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
                    Value arg = vm.peek(0);

                    if (arg.is_object() && arg.ref_type() == RefType::Strong && arg.as_object()) {
                        RC::retain(arg.as_object());
                    }
                    arr->elements.push_back(arg);

                    while (vm.stack_.size() > callee_index) {
                        vm.pop();
                    }

                    vm.push(Value::null());
                    return;
                }

                throw std::runtime_error("Unknown built-in method: " + method->method_name);
            }

            if (obj->type == ObjectType::Function) {
                auto* func = static_cast<FunctionObject*>(obj);
                if (!func->chunk && vm.is_builtin_type_name(func->name)) {
                    if (arg_count != 1) {
                        throw std::runtime_error(func->name + "() requires exactly 1 argument.");
                    }
                    Value input = vm.stack_[callee_index + 1];
                    Value result = convert_builtin(vm, func->name, input);
                    while (vm.stack_.size() > callee_index) {
                        vm.pop();
                    }
                    vm.push(result);
                    return;
                }
            }

            // Bound method: inject receiver
            if (obj->type == ObjectType::BoundMethod) {
                auto* bound = static_cast<BoundMethodObject*>(obj);

                Value receiver_val = Value::from_object(bound->receiver);
                if (receiver_val.is_object() && receiver_val.ref_type() == RefType::Strong && receiver_val.as_object()) {
                    RC::retain(receiver_val.as_object());
                }
                vm.stack_.insert(vm.stack_.begin() + static_cast<long>(callee_index + 1), receiver_val);
                arg_count += 1;

                Value method_val = bound->method;
                if (method_val.is_object() && method_val.ref_type() == RefType::Strong && method_val.as_object()) {
                    RC::retain(method_val.as_object());
                }
                Value old_bound = vm.stack_[callee_index];
                vm.stack_[callee_index] = method_val;
                if (old_bound.is_object() && old_bound.ref_type() == RefType::Strong && old_bound.as_object()) {
                    RC::release(self, old_bound.as_object());
                }
                callee = vm.stack_[callee_index];
                obj = callee.as_object();
                has_receiver = true;
            }

            FunctionObject* func = nullptr;
            ClosureObject* closure = nullptr;

            if (obj->type == ObjectType::Closure) {
                closure = static_cast<ClosureObject*>(obj);
                func = closure->function;
            }
            else if (obj->type == ObjectType::Function) {
                func = static_cast<FunctionObject*>(obj);
            }
            else {
                throw std::runtime_error("Attempted to call a non-function.");
            }

            vm.apply_named_arguments(callee_index, arg_count, func, has_receiver, arg_names);

            uint32_t param_count = has_receiver ? static_cast<uint32_t>(arg_count - 1) : arg_count;
            const MethodDef* method_def = nullptr;
            if (has_receiver && arg_count > 0) {
                Value receiver_val = vm.stack_[callee_index + 1];
                if (receiver_val.is_object() && receiver_val.as_object()) {
                    Object* receiver_obj = receiver_val.as_object();
                    const TypeDef* type_def = nullptr;
                    if (receiver_obj->type == ObjectType::Instance) {
                        auto* inst = static_cast<InstanceObject*>(receiver_obj);
                        if (inst->klass) {
                            type_def = vm.resolve_type_def(inst->klass->name);
                        }
                    } else if (receiver_obj->type == ObjectType::StructInstance) {
                        auto* inst = static_cast<StructInstanceObject*>(receiver_obj);
                        if (inst->struct_type) {
                            type_def = vm.resolve_type_def(inst->struct_type->name);
                        }
                    } else if (receiver_obj->type == ObjectType::EnumCase) {
                        auto* inst = static_cast<EnumCaseObject*>(receiver_obj);
                        if (inst->enum_type) {
                            type_def = vm.resolve_type_def(inst->enum_type->name);
                        }
                    }
                    if (type_def) {
                        method_def = vm.find_method_def_for_type(*type_def, func->name, false, param_count);
                    }
                }
            }
            if (!method_def) {
                method_def = vm.find_method_def_by_name(func->name, true, param_count);
                if (!method_def) {
                    method_def = vm.find_method_def_by_name(func->name, false, param_count);
                }
            }
            if (method_def) {
                vm.invoke_method_def(*method_def, callee_index, arg_count, has_receiver, func->is_initializer);
                return;
            }

            if (arg_count != func->params.size()) {
                throw std::runtime_error("Incorrect argument count.");
            }
            if (!func->chunk) {
                throw std::runtime_error("Function has no body.");
            }
            vm.call_frames_.emplace_back(callee_index + 1, vm.ip_, vm.chunk_, vm.current_body_idx_, func->name, closure, func->is_initializer);
            vm.chunk_ = func->chunk.get();
            vm.current_body_idx_ = vm.entry_body_index(*vm.chunk_);
            vm.set_active_body(vm.current_body_idx_);
            vm.ip_ = 0;
        }
    };

    OPCODE_DEFAULT(OpCode::OP_RETURN);
    OPCODE_DEFAULT(OpCode::OP_READ_LINE);
	OPCODE_DEFAULT(OpCode::OP_PRINT);
	OPCODE_DEFAULT(OpCode::OP_THROW);
	OPCODE_DEFAULT(OpCode::OP_HALT);

    OPCODE(OpCode::OP_UNWRAP)
    {
        OP_BODY
        {
            Value value = vm.peek(0);
            if (value.is_null()) 
            {
                throw std::runtime_error("Force unwrap of nil value.");
            }
		}
    };

    OPCODE(OpCode::OP_JUMP_IF_NIL)
    {
        OP_BODY
        {
            uint16_t offset = vm.read_short();
            Value condition = vm.peek(0);
            if (condition.is_null()) {
				vm.pop();
                vm.ip_ += offset;
            }
		}
    };

    OPCODE(OpCode::OP_NIL_COALESCE)
    {
        OP_BODY
        {
            Value fallback = vm.pop();
            Value optional = vm.pop();
            vm.push(optional.is_null() ? fallback : optional);
        }
    };

    OPCODE(OpCode::OP_OPTIONAL_CHAIN)
    {
        OP_BODY
        {
            const std::string & name = vm.read_string();
            Value obj = vm.pop();
            if (obj.is_null()) {
                vm.push(Value::null());
            }
            else {
                vm.push(vm.get_property(obj, name));
            }
        }
    };
}
