#pragma once
#include "ss_vm_opcodes_arithmetic.inl"

namespace swiftscript {

    // ============================================================================
    // Control Flow
    // ============================================================================

    template<>
    struct OpCodeHandler<OpCode::OP_JUMP> {
        static void execute(VM& vm) {
            uint16_t offset = vm.read_short();
            vm.ip_ += offset;
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_JUMP_IF_FALSE> {
        static void execute(VM& vm) {
            uint16_t offset = vm.read_short();
            if (!vm.is_truthy(vm.peek(0))) {
                vm.ip_ += offset;
            }
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_LOOP> {
        static void execute(VM& vm) {
            uint16_t offset = vm.read_short();
            vm.ip_ -= offset;
        }
    };

    OPCODE_DEFAULT(OpCode::OP_RANGE_INCLUSIVE)
	OPCODE_DEFAULT(OpCode::OP_RANGE_EXCLUSIVE)

	template<>
    struct OpCodeHandler<OpCode::OP_FUNCTION> {
        static void execute(VM& vm) {
            uint16_t index = vm.read_short();
            if (index >= vm.chunk_->function_prototypes.size()) {
                throw std::runtime_error("Function index out of range.");
            }
            const auto& proto = vm.chunk_->function_prototypes[index];
            std::vector<Value> defaults;
            std::vector<bool> has_defaults;
            vm.build_param_defaults(proto, defaults, has_defaults);
            auto* func = vm.allocate_object<FunctionObject>(
                proto.name,
                proto.params,
                proto.param_labels,
                std::move(defaults),
                std::move(has_defaults),
                proto.chunk,
                proto.is_initializer,
                proto.is_override);
            vm.push_new(func);  // Transfer ownership
		}
	};

    // ============================================================================
    // Variables
    // ============================================================================

    template<>
    struct OpCodeHandler<OpCode::OP_GET_GLOBAL> {
        static void execute(VM& vm) {
            const std::string& name = vm.read_string();
            vm.push(vm.get_global(name));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_SET_GLOBAL> {
        static void execute(VM& vm) {
            const std::string& name = vm.read_string();
            vm.set_global(name, vm.peek(0));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_GET_LOCAL> {
        static void execute(VM& vm) {
            uint16_t slot = vm.read_short();
            size_t base = vm.current_stack_base();
            if (base + slot >= vm.stack_.size()) {
                throw std::runtime_error("Local slot out of range.");
            }
            vm.push(vm.stack_[base + slot]);
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_GET_LOCAL_MOVE> {
        static void execute(VM& vm) {
            uint16_t slot = vm.read_short();
            size_t base = vm.current_stack_base();
            if (base + slot >= vm.stack_.size()) {
                throw std::runtime_error("Local slot out of range.");
            }
            
            // Move semantics: Transfer value WITHOUT retain
            Value val = vm.stack_[base + slot];
            
            // Clear the local slot (prevent double-release)
            vm.stack_[base + slot] = Value::null();
            
            // Push without RC (ownership transferred)
            if (val.is_object() && val.ref_type() == RefType::Strong) {
                // Push raw (no retain)
                vm.stack_.push_back(val);
            } else {
                vm.stack_.push_back(val);
            }
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_SET_LOCAL> {
        static void execute(VM& vm) {
            uint16_t slot = vm.read_short();
            size_t base = vm.current_stack_base();
            if (base + slot >= vm.stack_.size()) {
                throw std::runtime_error("Local slot out of range.");
            }
            Value new_val = vm.peek(0);
            Value& old_val = vm.stack_[base + slot];
            // Release old value
            if (old_val.is_object() && old_val.ref_type() == RefType::Strong && old_val.as_object()) {
                RC::release(&vm, old_val.as_object());
            }
            // Retain new value
            if (new_val.is_object() && new_val.ref_type() == RefType::Strong && new_val.as_object()) {
                RC::retain(new_val.as_object());
            }
            old_val = new_val;
        }
    };

    // ============================================================================
    // Property Access Handlers (중간 복잡도)
    // ============================================================================

    template<>
    struct OpCodeHandler<OpCode::OP_GET_PROPERTY> {
       
        static void execute(VM& vm) {
            const std::string& name = vm.read_string();
            Value obj = vm.pop();

            if (!obj.is_object() || !obj.as_object()) {
                vm.push(vm.get_property(obj, name));
                return;
            }

            Object* o = obj.as_object();
            constexpr method_idx kInvalidMethod = std::numeric_limits<method_idx>::max();

            switch (o->type) {
            case ObjectType::Instance: {
                auto* inst = static_cast<InstanceObject*>(o);
                if (inst->klass) {
                    const TypeDef* type_def = vm.resolve_type_def(inst->klass->name);
                    if (type_def) {
                        const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, false);
                        if (prop_def && prop_def->getter != kInvalidMethod) {
                            const MethodDef* getter_def = vm.resolve_method_def_by_index(prop_def->getter);
                            if (getter_def) {
                                vm.push(Value::null());
                                vm.push(obj);
                                size_t callee_index = vm.stack_.size() - 2;
                                vm.invoke_method_def(*getter_def, callee_index, 1, true, false);
                                return;
                            }
                        }
                    }
                }
                if (inst->klass) {
                    for (const auto& cp : inst->klass->computed_properties) {
                        if (cp.name == name) {
                            // TryInvokeComputedGetter가 obj를 다시 push하므로,
                            // pop()으로 가져온 obj의 ownership을 먼저 release
                            if (obj.is_object() && obj.ref_type() == RefType::Strong && obj.as_object()) {
                                RC::release(&vm, obj.as_object());
                            }
                            VM::TryInvokeComputedGetter(vm, cp.getter, obj);
                            return;
                        }
                    }
                }
                break;
            }
            case ObjectType::EnumCase: {
                auto* ec = static_cast<EnumCaseObject*>(o);
                if (ec->enum_type) {
                    const TypeDef* type_def = vm.resolve_type_def(ec->enum_type->name);
                    if (type_def) {
                        const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, false);
                        if (prop_def && prop_def->getter != kInvalidMethod) {
                            const MethodDef* getter_def = vm.resolve_method_def_by_index(prop_def->getter);
                            if (getter_def) {
                                vm.push(Value::null());
                                vm.push(obj);
                                size_t callee_index = vm.stack_.size() - 2;
                                vm.invoke_method_def(*getter_def, callee_index, 1, true, false);
                                return;
                            }
                        }
                    }
                }
                if (ec->enum_type) {
                    for (const auto& cp : ec->enum_type->computed_properties) {
                        if (cp.name == name) {
                            // TryInvokeComputedGetter가 obj를 다시 push하므로,
                            // pop()으로 가져온 obj의 ownership을 먼저 release
                            if (obj.is_object() && obj.ref_type() == RefType::Strong && obj.as_object()) {
                                RC::release(&vm, obj.as_object());
                            }
                            VM::TryInvokeComputedGetter(vm, cp.getter, obj);
                            return;
                        }
                    }
                }
                break;
            }
            case ObjectType::StructInstance: {
                auto* si = static_cast<StructInstanceObject*>(o);
                if (si->struct_type) {
                    const TypeDef* type_def = vm.resolve_type_def(si->struct_type->name);
                    if (type_def) {
                        const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, false);
                        if (prop_def && prop_def->getter != kInvalidMethod) {
                            const MethodDef* getter_def = vm.resolve_method_def_by_index(prop_def->getter);
                            if (getter_def) {
                                vm.push(Value::null());
                                vm.push(obj);
                                size_t callee_index = vm.stack_.size() - 2;
                                vm.invoke_method_def(*getter_def, callee_index, 1, true, false);
                                return;
                            }
                        }
                    }
                }
                if (si->struct_type) {
                    for (const auto& cp : si->struct_type->computed_properties) {
                        if (cp.name == name) {
                            // TryInvokeComputedGetter가 obj를 다시 push하므로,
                            // pop()으로 가져온 obj의 ownership을 먼저 release
                            if (obj.is_object() && obj.ref_type() == RefType::Strong && obj.as_object()) {
                                RC::release(&vm, obj.as_object());
                            }
                            VM::TryInvokeComputedGetter(vm, cp.getter, obj);
                            return;
                        }
                    }
                }
                break;
            }
            case ObjectType::Class: {
                auto* klass = static_cast<ClassObject*>(o);
                const TypeDef* type_def = klass ? vm.resolve_type_def(klass->name) : nullptr;
                if (type_def) {
                    const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, true);
                    if (prop_def && prop_def->getter != kInvalidMethod) {
                        const MethodDef* getter_def = vm.resolve_method_def_by_index(prop_def->getter);
                        if (getter_def) {
                            vm.push(Value::null());
                            size_t callee_index = vm.stack_.size() - 1;
                            vm.invoke_method_def(*getter_def, callee_index, 0, false, false);
                            return;
                        }
                    }
                }
                break;
            }
            case ObjectType::Struct: {
                auto* st = static_cast<StructObject*>(o);
                const TypeDef* type_def = st ? vm.resolve_type_def(st->name) : nullptr;
                if (type_def) {
                    const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, true);
                    if (prop_def && prop_def->getter != kInvalidMethod) {
                        const MethodDef* getter_def = vm.resolve_method_def_by_index(prop_def->getter);
                        if (getter_def) {
                            vm.push(Value::null());
                            size_t callee_index = vm.stack_.size() - 1;
                            vm.invoke_method_def(*getter_def, callee_index, 0, false, false);
                            return;
                        }
                    }
                }
                break;
            }
            case ObjectType::Enum: {
                auto* en = static_cast<EnumObject*>(o);
                const TypeDef* type_def = en ? vm.resolve_type_def(en->name) : nullptr;
                if (type_def) {
                    const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, true);
                    if (prop_def && prop_def->getter != kInvalidMethod) {
                        const MethodDef* getter_def = vm.resolve_method_def_by_index(prop_def->getter);
                        if (getter_def) {
                            vm.push(Value::null());
                            size_t callee_index = vm.stack_.size() - 1;
                            vm.invoke_method_def(*getter_def, callee_index, 0, false, false);
                            return;
                        }
                    }
                }
                break;
            }
            case ObjectType::Native: {
                auto* native_obj = static_cast<NativeObject*>(o);
                // Look up the ClassObject for this native type to check computed properties
                for (auto& [gname, gval] : vm.globals_) {
                    if (!gval.is_object() || !gval.as_object()) continue;
                    if (gval.as_object()->type != ObjectType::Class) continue;
                    auto* klass = static_cast<ClassObject*>(gval.as_object());
                    if (klass->native_type_name == native_obj->type_name) {
                        for (const auto& cp : klass->computed_properties) {
                            if (cp.name == name) {
                                if (obj.is_object() && obj.ref_type() == RefType::Strong && obj.as_object()) {
                                    RC::release(&vm, obj.as_object());
                                }
                                VM::TryInvokeComputedGetter(vm, cp.getter, obj);
                                return;
                            }
                        }
                        break;
                    }
                }
                break;
            }
            default:
                break;
            }

            Value result = vm.get_property(obj, name);

            // pop()으로 가져온 obj의 ownership을 release
            // get_property가 BoundMethod를 반환한 경우, BoundMethod가 receiver를 이미 retain했으므로 안전
            // 필드 값을 반환한 경우, 필드 값은 Instance가 소유하고 있으므로 push에서 retain됨
            if (obj.is_object() && obj.ref_type() == RefType::Strong && obj.as_object()) {
                RC::release(&vm, obj.as_object());
            }

            vm.push(result);
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_SET_PROPERTY> {
        static void execute(VM& vm) {
            const std::string& name = vm.read_string();
            Value value = vm.pop();
            Value obj_val = vm.peek(0); // setter는 기존처럼 object는 pop하지 않고 peek 기준

            if (!obj_val.is_object() || !obj_val.as_object()) {
                throw std::runtime_error("Property set on non-object.");
            }

            Object* o = obj_val.as_object();
            constexpr method_idx kInvalidMethod = std::numeric_limits<method_idx>::max();

            switch (o->type) {
            case ObjectType::Instance: {
                auto* inst = static_cast<InstanceObject*>(o);

                // 1) computed property 우선
                if (inst->klass) {
                    const TypeDef* type_def = vm.resolve_type_def(inst->klass->name);
                    if (type_def) {
                        const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, false);
                        if (prop_def && prop_def->setter != kInvalidMethod) {
                            const MethodDef* setter_def = vm.resolve_method_def_by_index(prop_def->setter);
                            if (!setter_def) {
                                throw std::runtime_error("Setter method not found for property: " + name);
                            }
                            vm.pop();
                            vm.push(Value::null());
                            vm.push(obj_val);
                            vm.push(value);
                            size_t callee_index = vm.stack_.size() - 3;
                            vm.invoke_method_def(*setter_def, callee_index, 2, true, false);
                            return;
                        }
                    }
                    for (const auto& cp : inst->klass->computed_properties) {
                        if (cp.name == name) {
                            if (cp.setter.is_null()) {
                                throw std::runtime_error("Cannot set read-only computed property: " + name);
                            }

                            // obj_val을 스택에서 pop (ownership 이전)
                            // value는 이미 위에서 pop으로 ownership을 가져옴
                            vm.pop();

                            // TryInvokeComputedSetter에 ownership transfer로 전달
                            // (retain 없이 스택에 직접 추가)
                            VM::TryInvokeComputedSetter(vm, cp.setter, obj_val, value, true);
                            return;
                        }
                    }
                }

                // 2) stored property + observers
                Value will_set = Value::null();
                Value did_set = Value::null();
                Value old_val = Value::null();

                if (inst->klass) {
                    for (const auto& prop : inst->klass->properties) {
                        if (prop.name == name) {
                            will_set = prop.will_set_observer;
                            did_set = prop.did_set_observer;
                            auto it = inst->fields.find(name);
                            if (it != inst->fields.end()) old_val = it->second;
                            break;
                        }
                    }
                }

                vm.pop(); // obj_val 제거 (기존: 최종적으로 value만 push)

                if (!will_set.is_null()) {
                    vm.call_property_observer(will_set, obj_val, value);
                }

                // Release old value before assignment
                auto field_it = inst->fields.find(name);
                if (field_it != inst->fields.end()) {
                    if (field_it->second.is_object() && field_it->second.ref_type() == RefType::Strong && field_it->second.as_object()) {
                        RC::release(&vm, field_it->second.as_object());
                    }
                }
                // Retain new value
                if (value.is_object() && value.ref_type() == RefType::Strong && value.as_object()) {
                    RC::retain(value.as_object());
                }
                inst->fields[name] = value;

                if (!did_set.is_null()) {
                    vm.call_property_observer(did_set, obj_val, old_val);
                }

                vm.push(value);
                return;
            }

            case ObjectType::EnumCase: {
                // EnumCase는 일반적으로 읽기 전용이 많지만,
                // 네 설계에서 setter 허용하려면 여기에 computed setter만 지원하는 게 일관적.
                auto* ec = static_cast<EnumCaseObject*>(o);
                if (ec->enum_type) {
                    const TypeDef* type_def = vm.resolve_type_def(ec->enum_type->name);
                    if (type_def) {
                        const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, false);
                        if (prop_def && prop_def->setter != kInvalidMethod) {
                            const MethodDef* setter_def = vm.resolve_method_def_by_index(prop_def->setter);
                            if (!setter_def) {
                                throw std::runtime_error("Setter method not found for property: " + name);
                            }
                            vm.pop();
                            vm.push(Value::null());
                            vm.push(obj_val);
                            vm.push(value);
                            size_t callee_index = vm.stack_.size() - 3;
                            vm.invoke_method_def(*setter_def, callee_index, 2, true, false);
                            return;
                        }
                    }
                    for (const auto& cp : ec->enum_type->computed_properties) {
                        if (cp.name == name) {
                            // enum computed setter는 보통 금지. 허용할 거면 조건 완화.
                            if (cp.setter.is_null()) {
                                throw std::runtime_error("Cannot set read-only enum computed property: " + name);
                            }
                            vm.pop();
                            // TryInvokeComputedSetter에 ownership transfer로 전달
                            VM::TryInvokeComputedSetter(vm, cp.setter, obj_val, value, true);
                            return;
                        }
                    }
                }
                throw std::runtime_error("EnumCase property set not supported: " + name);
            }

            case ObjectType::StructInstance: {
                // StructInstance도 computed setter 우선 처리 (extension computed property)
                auto* si = static_cast<StructInstanceObject*>(o);
                if (si->struct_type) {
                    const TypeDef* type_def = vm.resolve_type_def(si->struct_type->name);
                    if (type_def) {
                        const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, false);
                        if (prop_def && prop_def->setter != kInvalidMethod) {
                            const MethodDef* setter_def = vm.resolve_method_def_by_index(prop_def->setter);
                            if (!setter_def) {
                                throw std::runtime_error("Setter method not found for property: " + name);
                            }
                            vm.pop();
                            vm.push(Value::null());
                            vm.push(obj_val);
                            vm.push(value);
                            size_t callee_index = vm.stack_.size() - 3;
                            vm.invoke_method_def(*setter_def, callee_index, 2, true, false);
                            return;
                        }
                    }
                    for (const auto& cp : si->struct_type->computed_properties) {
                        if (cp.name == name) {
                            if (cp.setter.is_null()) {
                                throw std::runtime_error("Cannot set read-only computed property: " + name);
                            }
                            vm.pop();
                            // TryInvokeComputedSetter에 ownership transfer로 전달
                            VM::TryInvokeComputedSetter(vm, cp.setter, obj_val, value, true);
                            return;
                        }
                    }
                }

                // 2) stored property + observers
                Value will_set = Value::null();
                Value did_set = Value::null();
                Value old_val = Value::null();

                if (si->struct_type) {
                    for (const auto& prop : si->struct_type->properties) {
                        if (prop.name == name) {
                            will_set = prop.will_set_observer;
                            did_set = prop.did_set_observer;
                            auto it = si->fields.find(name);
                            if (it != si->fields.end()) old_val = it->second;
                            break;
                        }
                    }
                }

                vm.pop();  // Remove instance

                // Call willSet if present
                if (!will_set.is_null()) {
                    vm.call_property_observer(will_set, obj_val, value);
                }

                // Release old value before assignment
                auto field_it = si->fields.find(name);
                if (field_it != si->fields.end()) {
                    if (field_it->second.is_object() && field_it->second.ref_type() == RefType::Strong && field_it->second.as_object()) {
                        RC::release(&vm, field_it->second.as_object());
                    }
                }
                // Retain new value
                if (value.is_object() && value.ref_type() == RefType::Strong && value.as_object()) {
                    RC::retain(value.as_object());
                }
                // Set the property
                si->fields[name] = value;

                // Call didSet if present
                if (!did_set.is_null()) {
                    vm.call_property_observer(did_set, obj_val, old_val);
                }

                vm.push(value);
                return;
            }

            case ObjectType::Class: {
                auto* klass = static_cast<ClassObject*>(o);
                const TypeDef* type_def = klass ? vm.resolve_type_def(klass->name) : nullptr;
                if (type_def) {
                    const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, true);
                    if (prop_def && prop_def->setter != kInvalidMethod) {
                        const MethodDef* setter_def = vm.resolve_method_def_by_index(prop_def->setter);
                        if (!setter_def) {
                            throw std::runtime_error("Setter method not found for property: " + name);
                        }
                        vm.pop();
                        vm.push(Value::null());
                        vm.push(value);
                        size_t callee_index = vm.stack_.size() - 2;
                        vm.invoke_method_def(*setter_def, callee_index, 1, false, false);
                        return;
                    }
                }
                vm.pop();
                if (klass) {
                    klass->static_properties[name] = value;
                }
                vm.push(value);
                return;
            }

            case ObjectType::Struct: {
                auto* st = static_cast<StructObject*>(o);
                const TypeDef* type_def = st ? vm.resolve_type_def(st->name) : nullptr;
                if (type_def) {
                    const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, true);
                    if (prop_def && prop_def->setter != kInvalidMethod) {
                        const MethodDef* setter_def = vm.resolve_method_def_by_index(prop_def->setter);
                        if (!setter_def) {
                            throw std::runtime_error("Setter method not found for property: " + name);
                        }
                        vm.pop();
                        vm.push(Value::null());
                        vm.push(value);
                        size_t callee_index = vm.stack_.size() - 2;
                        vm.invoke_method_def(*setter_def, callee_index, 1, false, false);
                        return;
                    }
                }
                vm.pop();
                if (st) {
                    st->static_properties[name] = value;
                }
                vm.push(value);
                return;
            }

            case ObjectType::Enum: {
                auto* en = static_cast<EnumObject*>(o);
                const TypeDef* type_def = en ? vm.resolve_type_def(en->name) : nullptr;
                if (type_def) {
                    const PropertyDef* prop_def = vm.find_property_def_for_type(*type_def, name, true);
                    if (prop_def && prop_def->setter != kInvalidMethod) {
                        const MethodDef* setter_def = vm.resolve_method_def_by_index(prop_def->setter);
                        if (!setter_def) {
                            throw std::runtime_error("Setter method not found for property: " + name);
                        }
                        vm.pop();
                        vm.push(Value::null());
                        vm.push(value);
                        size_t callee_index = vm.stack_.size() - 2;
                        vm.invoke_method_def(*setter_def, callee_index, 1, false, false);
                        return;
                    }
                }
                throw std::runtime_error("Enum property set not supported: " + name);
            }

            case ObjectType::Map: {
                vm.pop();
                auto* dict = static_cast<MapObject*>(o);
                dict->entries[name] = value;
                vm.push(value);
                return;
            }

            case ObjectType::Native: {
                auto* native_obj = static_cast<NativeObject*>(o);
                // Look up the ClassObject for this native type to check computed properties
                for (auto& [gname, gval] : vm.globals_) {
                    if (!gval.is_object() || !gval.as_object()) continue;
                    if (gval.as_object()->type != ObjectType::Class) continue;
                    auto* klass = static_cast<ClassObject*>(gval.as_object());
                    if (klass->native_type_name == native_obj->type_name) {
                        for (const auto& cp : klass->computed_properties) {
                            if (cp.name == name) {
                                if (cp.setter.is_null()) {
                                    throw std::runtime_error("Cannot set read-only computed property: " + name);
                                }
                                vm.pop();  // pop native object
                                VM::TryInvokeComputedSetter(vm, cp.setter, obj_val, value, true);
                                return;
                            }
                        }
                        break;
                    }
                }
                throw std::runtime_error("Native object '" + native_obj->type_name + "' has no settable property named '" + name + "'");
            }

            default:
                break;
            }

            throw std::runtime_error("Property set only supported on instances/struct-instances/maps (and computed properties).");
        }
    };

    // ============================================================================
    // Closure Handler
    // ============================================================================

    template<>
    struct OpCodeHandler<OpCode::OP_CLOSURE> {
        static void execute(VM& vm) {
            uint16_t index = vm.read_short();
            if (index >= vm.chunk_->function_prototypes.size()) {
                throw std::runtime_error("Function index out of range.");
            }

            const auto& proto = vm.chunk_->function_prototypes[index];
            std::vector<Value> defaults;
            std::vector<bool> has_defaults;
            vm.build_param_defaults(proto, defaults, has_defaults);

            auto* func = vm.allocate_object<FunctionObject>(
                proto.name,
                proto.params,
                proto.param_labels,
                std::move(defaults),
                std::move(has_defaults),
                proto.chunk,
                proto.is_initializer,
                proto.is_override);

            // ClosureObject constructor retains func, so we pass ownership
            auto* closure = vm.allocate_object<ClosureObject>(func);
            closure->upvalues.resize(proto.upvalues.size(), nullptr);

            ClosureObject* enclosing_closure = vm.call_frames_.empty() ? nullptr : vm.call_frames_.back().closure;
            size_t base = vm.current_stack_base();

            for (size_t i = 0; i < proto.upvalues.size(); ++i) {
                const auto& uv = proto.upvalues[i];
                if (uv.is_local) {
                    if (base + uv.index >= vm.stack_.size()) {
                        throw std::runtime_error("Upvalue local slot out of range.");
                    }
                    closure->upvalues[i] = vm.capture_upvalue(&vm.stack_[base + uv.index]);
                }
                else {
                    if (!enclosing_closure) {
                        throw std::runtime_error("Upvalue refers to enclosing closure, but none is active.");
                    }
                    if (uv.index >= enclosing_closure->upvalues.size()) {
                        throw std::runtime_error("Upvalue index out of range.");
                    }
                    closure->upvalues[i] = enclosing_closure->upvalues[uv.index];
                }
            }

            vm.push_new(closure);  // Transfer ownership
        }
    };

    // ============================================================================
    // Upvalue Handlers
    // ============================================================================

    template<>
    struct OpCodeHandler<OpCode::OP_GET_UPVALUE> {
        static void execute(VM& vm) {
            uint16_t slot = vm.read_short();
            if (vm.call_frames_.empty() || !vm.call_frames_.back().closure) {
                throw std::runtime_error("No closure active for upvalue read.");
            }
            auto* closure = vm.call_frames_.back().closure;
            if (slot >= closure->upvalues.size()) {
                throw std::runtime_error("Upvalue index out of range.");
            }
            vm.push(*closure->upvalues[slot]->location);
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_SET_UPVALUE> {
        static void execute(VM& vm) {
            uint16_t slot = vm.read_short();
            if (vm.call_frames_.empty() || !vm.call_frames_.back().closure) {
                throw std::runtime_error("No closure active for upvalue write.");
            }
            auto* closure = vm.call_frames_.back().closure;
            if (slot >= closure->upvalues.size()) {
                throw std::runtime_error("Upvalue index out of range.");
            }
            Value new_val = vm.peek(0);
            Value& old_val = *closure->upvalues[slot]->location;
            // Release old value
            if (old_val.is_object() && old_val.ref_type() == RefType::Strong && old_val.as_object()) {
                RC::release(&vm, old_val.as_object());
            }
            // Retain new value
            if (new_val.is_object() && new_val.ref_type() == RefType::Strong && new_val.as_object()) {
                RC::retain(new_val.as_object());
            }
            old_val = new_val;
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_CLOSE_UPVALUE> {
        static void execute(VM& vm) {
            if (vm.stack_.empty()) {
                throw std::runtime_error("Stack underflow on close upvalue.");
            }
            vm.close_upvalues(&vm.stack_.back());
            vm.pop();
        }
    };

	// ============================================================================
    // Type
	// ============================================================================

    OPCODE(OpCode::OP_CLASS) {
        OP_BODY {
            uint16_t name_idx = vm.read_short();
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Class name index out of range.");
            }
            const std::string& name = vm.chunk_->string_table[name_idx];
            auto* klass = vm.allocate_object<ClassObject>(name);
            vm.push_new(klass);  // Transfer ownership
        }
	};

    OPCODE(OpCode::OP_METHOD)
    {
        OP_BODY
        {
            uint16_t name_idx = vm.read_short();
            if (vm.stack_.size() < 2) {
                throw std::runtime_error("Stack underflow on method definition.");
            }
            
            // Peek first, retain for map ownership, then discard from stack
            Value method_val = vm.peek(0);
            
            // RETAIN for map ownership
            if (method_val.is_object() && method_val.ref_type() == RefType::Strong && method_val.as_object()) {
                RC::retain(method_val.as_object());
            }
            
            // Discard from stack (releases stack's ownership)
            vm.discard();
            
            Value type_val = vm.peek(0);

            if (!type_val.is_object() || !type_val.as_object()) {
                throw std::runtime_error("OP_METHOD expects class or enum on stack.");
            }
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Method name index out of range.");
            }

            Object* type_obj = type_val.as_object();
            const std::string& name = vm.chunk_->string_table[name_idx];

            // Handle ClassObject
            if (type_obj->type == ObjectType::Class) {
                auto* klass = static_cast<ClassObject*>(type_obj);

                // Get function prototype to check is_override flag and static
                FunctionObject* func = nullptr;
                if (method_val.is_object() && method_val.as_object()) {
                    Object* obj = method_val.as_object();
                    if (obj->type == ObjectType::Closure) {
                        func = static_cast<ClosureObject*>(obj)->function;
                    }
                    else if (obj->type == ObjectType::Function) {
                        func = static_cast<FunctionObject*>(obj);
                    }
                }

                // Check if static (no 'self' parameter)
                // Special case: 'init' methods are always instance methods even without 'self' parameter
                bool is_static = false;
                if (name != "init" && func && (func->params.empty() || func->params[0] != "self")) {
                    is_static = true;
                }

                // Validate override usage (only for instance methods)
                if (func && klass->superclass && !is_static) {
                    Value parent_method;
                    bool parent_has_method = vm.find_method_on_class(klass->superclass, name, parent_method);

                    bool is_override = func->is_override;

                    if (parent_has_method && !is_override && name != "init") {
                        throw std::runtime_error("Method '" + name + "' overrides a superclass method but is not marked with 'override'");
                    }

                    if (!parent_has_method && is_override) {
                        throw std::runtime_error("Method '" + name + "' marked with 'override' but does not override any superclass method");
                    }
                }

                // Method already retained before pop, just store in map
                if (is_static) {
                    klass->static_methods[name] = method_val;
                }
                else {
                    klass->methods[name] = method_val;
                }
            }
            // Handle EnumObject
            else if (type_obj->type == ObjectType::Enum) {
                auto* enum_type = static_cast<EnumObject*>(type_obj);
                
                
                // Method already retained before pop, just store in map
                enum_type->methods[name] = method_val;
            }
            // Handle StructObject (for extension and static methods)
            else if (type_obj->type == ObjectType::Struct) {
                auto* struct_type = static_cast<StructObject*>(type_obj);

                // Determine if this is a static method by checking param count
                // Static methods have no 'self' parameter
                FunctionObject* func = nullptr;
                if (method_val.is_object() && method_val.as_object()) {
                    Object* obj = method_val.as_object();
                    if (obj->type == ObjectType::Closure) {
                        func = static_cast<ClosureObject*>(obj)->function;
                    }
                    else if (obj->type == ObjectType::Function) {
                        func = static_cast<FunctionObject*>(obj);
                    }
                }

                // If function has no params or first param is not "self", it's static
                bool is_static = false;
                if (func && (func->params.empty() || func->params[0] != "self")) {
                    is_static = true;
                }

                // Method already retained before pop, just store in map

                if (is_static) {
                    struct_type->static_methods[name] = method_val;
                }
                else {
                    struct_type->methods[name] = method_val;
                    struct_type->mutating_methods[name] = false; // Extension methods are non-mutating by default
                }
            }
            else {
                throw std::runtime_error("OP_METHOD expects class, enum, or struct on stack.");
            }
        }
    };

    OPCODE(OpCode::OP_DEFINE_PROPERTY)
    {
        OP_BODY
        {
            uint16_t name_idx = vm.read_short();
            uint8_t flags = vm.read_byte();
            bool is_let = (flags & 0x1) != 0;
            bool is_static = (flags & 0x2) != 0;  // bit 1 = is_static
            if (vm.stack_.size() < 2) {
                throw std::runtime_error("Stack underflow on property definition.");
            }
            Value default_value = vm.peek(0);
            // Retain for property storage
            if (default_value.is_object() && default_value.ref_type() == RefType::Strong) {
                RC::retain(default_value.as_object());
            }
            // Discard from stack (releases stack's ownership)
            vm.discard();
            Value type_val = vm.peek(0);
            if (!type_val.is_object() || !type_val.as_object()) {
                throw std::runtime_error("OP_DEFINE_PROPERTY expects class or struct on stack.");
            }
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Property name index out of range.");
            }
            Object* type_obj = type_val.as_object();
            const std::string& prop_name = vm.chunk_->string_table[name_idx];

            if (type_obj->type == ObjectType::Class) {
                auto* klass = static_cast<ClassObject*>(type_obj);
                if (is_static) {
                    // Store in static_properties
                    klass->static_properties[prop_name] = default_value;
                }
                else {
                    ClassObject::PropertyInfo info;
                    info.name = prop_name;
                    info.default_value = default_value;
                    info.is_let = is_let;
                    klass->properties.push_back(std::move(info));
                }
            }
            else if (type_obj->type == ObjectType::Struct) {
                auto* struct_type = static_cast<StructObject*>(type_obj);
                if (is_static) {
                    // Store in static_properties
                    struct_type->static_properties[prop_name] = default_value;
                }
                else {
                    StructObject::PropertyInfo info;
                    info.name = prop_name;
                    info.default_value = default_value;
                    info.is_let = is_let;
                    struct_type->properties.push_back(std::move(info));
                }
            }
            else {
                throw std::runtime_error("OP_DEFINE_PROPERTY expects class or struct on stack.");
            }
		}
    };

    OPCODE(OpCode::OP_DEFINE_COMPUTED_PROPERTY)
    {
        static ClosureObject* make_closure_from_proto_index(VM & vm, uint16_t fn_idx) {
            if (fn_idx >= vm.chunk_->function_prototypes.size()) {
                throw std::runtime_error("Function index out of range.");
            }

            const auto& proto = vm.chunk_->function_prototypes[fn_idx];

            std::vector<Value> defaults;
            std::vector<bool>  has_defaults;
            vm.build_param_defaults(proto, defaults, has_defaults);

            auto* func = vm.allocate_object<FunctionObject>(
                proto.name,
                proto.params,
                proto.param_labels,
                std::move(defaults),
                std::move(has_defaults),
                proto.chunk,
                /*is_initializer*/ false,
                /*is_override*/    false
            );

            auto* closure = vm.allocate_object<ClosureObject>(func);
            // Retain for storage (allocate starts at rc:0)
            RC::retain(closure);
            return closure;
        }

        static Value make_optional_closure_value(VM & vm, uint16_t fn_idx) {
            if (fn_idx == 0xFFFF) return Value::null();
            // make_closure_from_proto_index already retains
            return Value::from_object(make_closure_from_proto_index(vm, fn_idx));
        }

        OP_BODY {
            uint16_t name_idx = vm.read_short();
            uint16_t getter_idx = vm.read_short();
            uint16_t setter_idx = vm.read_short();

            Value type_val = vm.peek(0);
            if (!type_val.is_object() || !type_val.as_object()) {
                throw std::runtime_error("OP_DEFINE_COMPUTED_PROPERTY expects class/enum/struct on stack.");
            }
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Property name index out of range.");
            }
            if (getter_idx >= vm.chunk_->function_prototypes.size()) {
                throw std::runtime_error("Getter function index out of range.");
            }

            Object* type_obj = type_val.as_object();
            const std::string& prop_name = vm.chunk_->string_table[name_idx];

            // Build getter once
            Value getter_val = Value::from_object(make_closure_from_proto_index(vm, getter_idx));

            switch (type_obj->type) {
            case ObjectType::Class: {
                auto* klass = static_cast<ClassObject*>(type_obj);

                ClassObject::ComputedPropertyInfo info;
                info.name = prop_name;
                info.getter = getter_val;
                info.setter = make_optional_closure_value(vm, setter_idx);

                klass->computed_properties.push_back(std::move(info));
                break;
            }
            case ObjectType::Struct: {
                auto* st = static_cast<StructObject*>(type_obj);

                StructObject::ComputedPropertyInfo info;
                info.name = prop_name;
                info.getter = getter_val;
                info.setter = make_optional_closure_value(vm, setter_idx);

                st->computed_properties.push_back(std::move(info));
                break;
            }
            case ObjectType::Enum: {
                auto* en = static_cast<EnumObject*>(type_obj);

                EnumObject::ComputedPropertyInfo info;
                info.name = prop_name;
                info.getter = getter_val;
                info.setter = Value::null(); // read-only

                en->computed_properties.push_back(std::move(info));
                break;
            }
            default:
                throw std::runtime_error("OP_DEFINE_COMPUTED_PROPERTY expects class, enum, or struct.");
            }
        }
    };

    OPCODE(OpCode::OP_DEFINE_PROPERTY_WITH_OBSERVERS)
    {
        OP_BODY
        {
            // Stack: [... class/struct, default_value]
            // Read: property_name_idx, flags, will_set_idx, did_set_idx
            uint16_t name_idx = vm.read_short();
            uint8_t flags = vm.read_byte();
            uint16_t will_set_idx = vm.read_short();
            uint16_t did_set_idx = vm.read_short();

            bool is_let = (flags & 0x1) != 0;
            bool is_static = (flags & 0x2) != 0;
            bool is_lazy = (flags & 0x4) != 0;

            Value default_value = vm.pop();
            Value type_val = vm.peek(0);

            if (!type_val.is_object() || !type_val.as_object()) {
                throw std::runtime_error("OP_DEFINE_PROPERTY_WITH_OBSERVERS expects class or struct on stack.");
            }
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Property name index out of range.");
            }

            Object* type_obj = type_val.as_object();
            const std::string& prop_name = vm.chunk_->string_table[name_idx];

            // Create willSet observer closure if present
            Value will_set_observer = Value::null();
            if (will_set_idx != 0xFFFF && will_set_idx < vm.chunk_->function_prototypes.size()) {
                const auto& will_set_proto = vm.chunk_->function_prototypes[will_set_idx];
                std::vector<Value> will_set_defaults;
                std::vector<bool> will_set_has_defaults;
                vm.build_param_defaults(will_set_proto, will_set_defaults, will_set_has_defaults);
                auto* will_set_func = vm.allocate_object<FunctionObject>(
                    will_set_proto.name,
                    will_set_proto.params,
                    will_set_proto.param_labels,
                    std::move(will_set_defaults),
                    std::move(will_set_has_defaults),
                    will_set_proto.chunk,
                    false,
                    false
                );
                auto* will_set_closure = vm.allocate_object<ClosureObject>(will_set_func);
                RC::retain(will_set_closure);  // Retain for storage
                will_set_observer = Value::from_object(will_set_closure);
            }

            // Create didSet observer closure if present
            Value did_set_observer = Value::null();
            if (did_set_idx != 0xFFFF && did_set_idx < vm.chunk_->function_prototypes.size()) {
                const auto& did_set_proto = vm.chunk_->function_prototypes[did_set_idx];
                std::vector<Value> did_set_defaults;
                std::vector<bool> did_set_has_defaults;
                vm.build_param_defaults(did_set_proto, did_set_defaults, did_set_has_defaults);
                auto* did_set_func = vm.allocate_object<FunctionObject>(
                    did_set_proto.name,
                    did_set_proto.params,
                    did_set_proto.param_labels,
                    std::move(did_set_defaults),
                    std::move(did_set_has_defaults),
                    did_set_proto.chunk,
                    false,
                    false
                );
                auto* did_set_closure = vm.allocate_object<ClosureObject>(did_set_func);
                RC::retain(did_set_closure);  // Retain for storage
                did_set_observer = Value::from_object(did_set_closure);
            }

            if (type_obj->type == ObjectType::Class) {
                auto* klass = static_cast<ClassObject*>(type_obj);
                ClassObject::PropertyInfo info;
                info.name = prop_name;
                info.default_value = default_value;
                info.is_let = is_let;
                info.is_lazy = is_lazy;
                info.will_set_observer = will_set_observer;
                info.did_set_observer = did_set_observer;
                klass->properties.push_back(std::move(info));
            }
            else if (type_obj->type == ObjectType::Struct) {
                auto* struct_type = static_cast<StructObject*>(type_obj);
                StructObject::PropertyInfo info;
                info.name = prop_name;
                info.default_value = default_value;
                info.is_let = is_let;
                info.is_lazy = is_lazy;
                info.will_set_observer = will_set_observer;
                info.did_set_observer = did_set_observer;
                struct_type->properties.push_back(std::move(info));
            }
            else {
                throw std::runtime_error("OP_DEFINE_PROPERTY_WITH_OBSERVERS expects class or struct on stack.");
            }
        }
    };

    OPCODE(OpCode::OP_INHERIT)
    {
        OP_BODY
        {
            if (vm.stack_.size() < 2) {
                throw std::runtime_error("Stack underflow on inherit.");
            }
            Value subclass_val = vm.peek(0);
            Value superclass_val = vm.stack_[vm.stack_.size() - 2];
            if (!subclass_val.is_object() || !subclass_val.as_object() || subclass_val.as_object()->type != ObjectType::Class) {
                throw std::runtime_error("OP_INHERIT expects subclass on stack.");
            }
            if (!superclass_val.is_object() || !superclass_val.as_object() || superclass_val.as_object()->type != ObjectType::Class) {
                throw std::runtime_error("Superclass must be a class.");
            }
            auto* subclass = static_cast<ClassObject*>(subclass_val.as_object());
            auto* superclass = static_cast<ClassObject*>(superclass_val.as_object());
            subclass->superclass = superclass;
            // Remove superclass from stack with proper RC
            // Release the superclass reference before erasing
            if (superclass_val.is_object() && superclass_val.ref_type() == RefType::Strong && superclass_val.as_object()) {
                RC::release(&vm, superclass_val.as_object());
            }
            vm.stack_.erase(vm.stack_.end() - 2);
        }
	};

    OPCODE(OpCode::OP_SUPER)
    {
        OP_BODY {
            const std::string& name = vm.read_string();
            Value receiver = vm.pop();
            if (!receiver.is_object() || receiver.as_object()->type != ObjectType::Instance) {
                throw std::runtime_error("'super' can only be used on instances.");
            }
            auto* inst = static_cast<InstanceObject*>(receiver.as_object());
            if (!inst->klass || !inst->klass->superclass) {
                throw std::runtime_error("No superclass available for 'super' call.");
            }

            Value method_value;
            if (!vm.find_method_on_class(inst->klass->superclass, name, method_value)) {
                throw std::runtime_error("Undefined super method: " + name);
            }

            auto* bound = vm.allocate_object<BoundMethodObject>(inst, method_value);
            vm.push_new(bound);  // Transfer ownership
        }
    };

    OPCODE(OpCode::OP_ARRAY)
    {
        OP_BODY {
            uint16_t count = vm.read_short();
            auto* arr = vm.allocate_object<ListObject>();
            arr->elements.reserve(count);
            // Pop elements in reverse order (last pushed first)
            std::vector<Value> temp(count);
            for (int i = count - 1; i >= 0; --i) {
                temp[i] = vm.pop();
            }
            for (const auto& v : temp) {
                // Retain for storage in list (pop released the stack's reference)
                if (v.is_object() && v.ref_type() == RefType::Strong && v.as_object()) {
                    RC::retain(v.as_object());
                }
                arr->elements.push_back(v);
            }
            vm.push_new(arr);  // Transfer ownership
		}
    };

    OPCODE(OpCode::OP_DICT)
    {
        OP_BODY {
            uint16_t count = vm.read_short();
            auto* dict = vm.allocate_object<MapObject>();
            // Pop key-value pairs in reverse order
            std::vector<std::pair<Value, Value>> temp(count);
            for (int i = count - 1; i >= 0; --i) {
                Value value = vm.pop();
                Value key = vm.pop();
                temp[i] = {key, value};
            }
            for (const auto& [k, v] : temp) {
                if (!k.is_object() || k.as_object()->type != ObjectType::String) {
                    throw std::runtime_error("Dictionary key must be a string.");
                }
                auto* str_key = static_cast<StringObject*>(k.as_object());
                // Retain value for storage (pop released the stack's reference)
                if (v.is_object() && v.ref_type() == RefType::Strong && v.as_object()) {
                    RC::retain(v.as_object());
                }
                dict->entries[str_key->data] = v;
            }
            vm.push_new(dict);  // Transfer ownership
        }
	};

    OPCODE(OpCode::OP_GET_SUBSCRIPT)
    {
        OP_BODY {
            Value index = vm.pop();
            Value collection = vm.pop();
            if (!collection.is_object() || !collection.as_object()) {
                throw std::runtime_error("Subscript get on non-object.");
            }
            Object* obj = collection.as_object();
            switch (obj->type) {
            case ObjectType::List: {
                auto* list = static_cast<ListObject*>(obj);
                if (!index.is_number()) {
                    throw std::runtime_error("List subscript must be a number.");
                }
                int idx = static_cast<int>(index.as_int());
                if (idx < 0 || idx >= static_cast<int>(list->elements.size())) {
                    throw std::runtime_error("List subscript out of range.");
                }
                vm.push(list->elements[idx]);
                return;
            }
            case ObjectType::Map: {
                auto* dict = static_cast<MapObject*>(obj);
                if (!index.is_object() || index.as_object()->type != ObjectType::String) {
                    throw std::runtime_error("Dictionary subscript must be a string.");
                }
                auto* str_key = static_cast<StringObject*>(index.as_object());
                auto it = dict->entries.find(str_key->data);
                if (it == dict->entries.end()) {
                    vm.push(Value::null());
                }
                vm.push(it->second);
                return;
            }
            }
            throw std::runtime_error("Subscript access only supported on arrays and dictionaries.");
		}
    };

    OPCODE(OpCode::OP_SET_SUBSCRIPT)
    {
        OP_BODY {
            Value value = vm.pop();
            Value index = vm.pop();
            Value collection = vm.pop();
            if (!collection.is_object() || !collection.as_object()) {
                throw std::runtime_error("Subscript set on non-object.");
            }
            Object* obj = collection.as_object();
            switch (obj->type) {
            case ObjectType::List: {
                auto* list = static_cast<ListObject*>(obj);
                if (!index.is_int()) {
                    throw std::runtime_error("List subscript must be a int.");
                }
                int idx = static_cast<int>(index.as_int());
                if (idx < 0 || idx >= static_cast<int>(list->elements.size())) {
                    throw std::runtime_error("List subscript out of range.");
                }
                // Release old value before assignment
                Value& old_val = list->elements[idx];
                if (old_val.is_object() && old_val.ref_type() == RefType::Strong && old_val.as_object()) {
                    RC::release(&vm, old_val.as_object());
                }
                // Retain new value (value was popped, so it's been released, need to retain for storage)
                if (value.is_object() && value.ref_type() == RefType::Strong && value.as_object()) {
                    RC::retain(value.as_object());
                }
                old_val = value;
                vm.push(value);
                return;
            }
            case ObjectType::Map: {
                auto* dict = static_cast<MapObject*>(obj);
                if (!index.is_object() || index.as_object()->type != ObjectType::String) {
                    throw std::runtime_error("Dictionary subscript must be a string.");
                }
                auto* str_key = static_cast<StringObject*>(index.as_object());
                // Release old value if exists
                auto it = dict->entries.find(str_key->data);
                if (it != dict->entries.end()) {
                    if (it->second.is_object() && it->second.ref_type() == RefType::Strong && it->second.as_object()) {
                        RC::release(&vm, it->second.as_object());
                    }
                }
                // Retain new value (value was popped, so need to retain for storage)
                if (value.is_object() && value.ref_type() == RefType::Strong && value.as_object()) {
                    RC::retain(value.as_object());
                }
                dict->entries[str_key->data] = value;
                vm.push(value);
                return;
            }
            }
			throw std::runtime_error("Subscript assignment only supported on arrays and dictionaries.");
        }
	};

    OPCODE(OpCode::OP_TUPLE)
    {
        OP_BODY
        {
            uint16_t count = vm.read_short();
            auto* tuple = vm.allocate_object<TupleObject>();
            tuple->elements.reserve(count);
            tuple->labels.reserve(count);

            // Pop elements in reverse order
            std::vector<Value> temp(count);
            for (int i = count - 1; i >= 0; --i) {
                temp[i] = vm.pop();
            }
            for (const auto& v : temp) {
                // Retain for storage in tuple (pop released the stack's reference)
                if (v.is_object() && v.ref_type() == RefType::Strong && v.as_object()) {
                    RC::retain(v.as_object());
                }
                tuple->elements.push_back(v);
            }

            // Read labels
            for (uint16_t i = 0; i < count; ++i) {
                uint16_t label_idx = vm.read_short();
                if (label_idx == 0xFFFF) {
                    tuple->labels.push_back(std::nullopt);
                }
                else {
                    if (label_idx < vm.chunk_->string_table.size()) {
                        tuple->labels.push_back(vm.chunk_->string_table[label_idx]);
                    }
                    else {
                        tuple->labels.push_back(std::nullopt);
                    }
                }
            }

            vm.push_new(tuple);  // Transfer ownership
        }
    };

    OPCODE(OpCode::OP_GET_TUPLE_INDEX)
    {
        OP_BODY
        {
            uint16_t index = vm.read_short();
            Value tuple_val = vm.pop();
            if (!tuple_val.is_object() || tuple_val.as_object()->type != ObjectType::Tuple) {
                throw std::runtime_error("Tuple index access on non-tuple.");
            }
            auto* tuple = static_cast<TupleObject*>(tuple_val.as_object());
            if (index >= tuple->elements.size()) {
                throw std::runtime_error("Tuple index out of bounds.");
            }
            vm.push(tuple->elements[index]);
		}
    };

	OPCODE(OpCode::OP_GET_TUPLE_LABEL)
    {
        OP_BODY
        {
            uint16_t label_idx = vm.read_short();
            Value tuple_val = vm.pop();
            if (!tuple_val.is_object() || tuple_val.as_object()->type != ObjectType::Tuple) {
                throw std::runtime_error("Tuple label access on non-tuple.");
            }
            auto* tuple = static_cast<TupleObject*>(tuple_val.as_object());
            if (label_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Tuple label index out of range.");
            }
            const std::string& label = vm.chunk_->string_table[label_idx];
            Value result = tuple->get(label);
            if (result.is_null()) {
                throw std::runtime_error("Tuple has no element with label: " + label);
            }
            vm.push(result);
        }
	};

    OPCODE(OpCode::OP_STRUCT)
    {
        OP_BODY
        {
            uint16_t name_idx = vm.read_short();
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Struct name index out of range.");
            }
            const std::string& name = vm.chunk_->string_table[name_idx];
            auto* struct_type = vm.allocate_object<StructObject>(name);
            vm.push_new(struct_type);  // Transfer ownership
        }
    };

    OPCODE(OpCode::OP_STRUCT_METHOD)
    {
        OP_BODY
        {
            // Similar to OP_METHOD but with mutating flag
            uint16_t name_idx = vm.read_short();
            uint8_t is_mutating = vm.read_byte();
            if (vm.stack_.size() < 2) {
                throw std::runtime_error("Stack underflow on struct method definition.");
            }
            
            // Peek first, retain for map ownership, then discard from stack
            Value method_val = vm.peek(0);
            
            // RETAIN for map ownership
            if (method_val.is_object() && method_val.ref_type() == RefType::Strong && method_val.as_object()) {
                RC::retain(method_val.as_object());
            }
            
            // Discard from stack (releases stack's ownership)
            vm.discard();
            
            Value struct_val = vm.peek(0);
            if (!struct_val.is_object() || !struct_val.as_object() ||
                struct_val.as_object()->type != ObjectType::Struct) {
                throw std::runtime_error("OP_STRUCT_METHOD expects struct on stack.");
            }
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Method name index out of range.");
            }
            auto* struct_type = static_cast<StructObject*>(struct_val.as_object());
            const std::string& name = vm.chunk_->string_table[name_idx];
            
            // Method already retained, just store in map
            struct_type->methods[name] = method_val;
            struct_type->mutating_methods[name] = (is_mutating != 0);
        }
    };





    OPCODE(OpCode::OP_COPY_VALUE)
    {
        OP_BODY
        {
            // Deep copy a struct instance for value semantics
            Value val = vm.peek(0);
            if (val.is_object() && val.as_object() &&
                val.as_object()->type == ObjectType::StructInstance) {
                auto* inst = static_cast<StructInstanceObject*>(val.as_object());
                auto* copy = inst->deep_copy(vm);
                vm.discard();  // Discard original
                vm.push_new(copy);  // Transfer ownership
            }
            else {
                // Not a struct instance, just leave on stack (no-op)
                // No need to pop and re-push the same value
            }
        }
    };



    OPCODE(OpCode::OP_ENUM)
    {
        OP_BODY
        {
            // Create an enum type object (similar to OP_CLASS and OP_STRUCT)
            uint16_t name_idx = vm.read_short();
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Enum name index out of range.");
            }
            const std::string& name = vm.chunk_->string_table[name_idx];
            auto* enum_type = vm.allocate_object<EnumObject>(name);
            vm.push_new(enum_type);  // Transfer ownership
        }
    };

    OPCODE(OpCode::OP_ENUM_CASE)
    {
        OP_BODY
        {
            // Define an enum case
                    // Stack: [enum_object, raw_value]
            uint16_t case_name_idx = vm.read_short();
            uint8_t associated_count = vm.read_byte();

            if (vm.stack_.size() < 2) {
                throw std::runtime_error("Stack underflow on enum case definition.");
            }

            Value raw_value = vm.pop();  // May be nil
            Value enum_val = vm.peek(0);

            if (!enum_val.is_object() || !enum_val.as_object() ||
                enum_val.as_object()->type != ObjectType::Enum) {
                throw std::runtime_error("OP_ENUM_CASE expects enum on stack.");
            }
            if (case_name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Enum case name index out of range.");
            }

            auto* enum_type = static_cast<EnumObject*>(enum_val.as_object());
            const std::string& case_name = vm.chunk_->string_table[case_name_idx];

            // Create enum case instance
            auto* case_obj = vm.allocate_object<EnumCaseObject>(enum_type, case_name);

            // Retain the enum type for the case's lifetime
            RC::retain(enum_type);

            case_obj->raw_value = raw_value;
            case_obj->associated_labels.reserve(associated_count);
            for (uint8_t i = 0; i < associated_count; ++i) {
                uint16_t label_idx = vm.read_short();
                if (label_idx == std::numeric_limits<uint16_t>::max()) {
                    case_obj->associated_labels.emplace_back("");
                }
                else if (label_idx < vm.chunk_->string_table.size()) {
                    case_obj->associated_labels.emplace_back(vm.chunk_->string_table[label_idx]);
                }
                else {
                    throw std::runtime_error("Associated value label index out of range.");
                }
            }

            // Store in enum's cases map (retain for map ownership)
            RC::retain(case_obj);
            enum_type->cases[case_name] = Value::from_object(case_obj);
        }
    };

    OPCODE(OpCode::OP_MATCH_ENUM_CASE)
    {
        OP_BODY
        {
            const std::string& case_name = vm.read_string();
            Value value = vm.pop();
            bool matches = false;
            if (value.is_object() && value.as_object() &&
                value.as_object()->type == ObjectType::EnumCase) {
                auto* enum_case = static_cast<EnumCaseObject*>(value.as_object());
                matches = enum_case->case_name == case_name;
            }
            vm.push(Value::from_bool(matches));
        }
    };

    OPCODE(OpCode::OP_GET_ASSOCIATED)
    {
        OP_BODY
        {
            uint16_t index = vm.read_short();
            Value value = vm.pop();
            if (!value.is_object() || !value.as_object() ||
                value.as_object()->type != ObjectType::EnumCase) {
                throw std::runtime_error("Associated value access on non-enum case.");
            }
            auto* enum_case = static_cast<EnumCaseObject*>(value.as_object());
            if (index >= enum_case->associated_values.size()) {
                throw std::runtime_error("Associated value index out of range.");
            }
            vm.push(enum_case->associated_values[index]);
        }
    };

    OPCODE(OpCode::OP_PROTOCOL)
    {
        OP_BODY
        {
            // Create protocol object
            uint16_t protocol_idx = vm.read_short();
            if (protocol_idx >= vm.chunk_->protocol_definitions.size()) {
                throw std::runtime_error("Protocol index out of range.");
            }

            auto protocol = vm.chunk_->protocol_definitions[protocol_idx];
            auto* protocol_obj = vm.allocate_object<ProtocolObject>(protocol->name);

            // Store protocol requirements for runtime validation
            for (const auto& method_req : protocol->method_requirements) {
                protocol_obj->method_requirements.push_back(method_req.name);
            }
            for (const auto& prop_req : protocol->property_requirements) {
                protocol_obj->property_requirements.push_back(prop_req.name);
            }

            vm.push(Value::from_object(protocol_obj));
        }
    };

    OPCODE(OpCode::OP_DEFINE_GLOBAL)
    {
        OP_BODY
        {
            // Define a global variable with the value on top of stack
            uint16_t name_idx = vm.read_short();
            if (name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Global name index out of range.");
            }
            const std::string& name = vm.chunk_->string_table[name_idx];
            vm.globals_[name] = vm.peek(0);
            vm.pop();
        }
    };

    OPCODE(OpCode::OP_TYPE_CHECK)
    {
        OP_BODY
        {
            // is operator: value is Type
            uint16_t type_name_idx = vm.read_short();
            if (type_name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Type name index out of range.");
            }
            const std::string& type_name = vm.chunk_->string_table[type_name_idx];
            Value value = vm.pop();

            vm.push(Value::from_bool(vm.matches_type(value, type_name)));
        }
    };

    OPCODE(OpCode::OP_TYPE_CAST)
    {
        OP_BODY
        {
            // as operator: basic cast (for now, just verify type matches)
            uint16_t type_name_idx = vm.read_short();
            if (type_name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Type name index out of range.");
            }
            const std::string& type_name = vm.chunk_->string_table[type_name_idx];
            Value value = vm.peek(0);  // Keep value on stack

            // For now, basic cast just passes through
            // In a full implementation, this would validate the cast
        }
    };

    OPCODE(OpCode::OP_TYPE_CAST_OPTIONAL)
    {
        OP_BODY
        {
            // as? operator: optional cast (returns nil if fails)
            uint16_t type_name_idx = vm.read_short();
            if (type_name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Type name index out of range.");
            }
            const std::string& type_name = vm.chunk_->string_table[type_name_idx];
            Value value = vm.pop();

            bool is_valid = vm.matches_type(value, type_name);

            if (is_valid) {
                vm.push(value);
            }
            else {
                vm.push(Value::null());
            }
        }
    };

    OPCODE(OpCode::OP_TYPE_CAST_FORCED)
    {
        OP_BODY
        {
            // as! operator: forced cast (throws if fails)
            uint16_t type_name_idx = vm.read_short();
            if (type_name_idx >= vm.chunk_->string_table.size()) {
                throw std::runtime_error("Type name index out of range.");
            }
            const std::string& type_name = vm.chunk_->string_table[type_name_idx];
            Value value = vm.peek(0);

            bool is_valid = vm.matches_type(value, type_name);

            if (!is_valid) {
                throw std::runtime_error("Forced cast (as!) failed: value is not of type '" + type_name + "'");
            }
            // Value stays on stack
        }
    };

    constexpr std::array<OpHandlerFunc, 256> make_handler_table() 
    {
        std::array<OpHandlerFunc, 256> tbl{};
        tbl.fill(nullptr);

#define X(op) tbl[static_cast<uint8_t>(OpCode::op)] = &OpCodeHandler<OpCode::op>::execute;
#include "ss_opcodes.def"
#undef X

        return tbl;
    }
}
