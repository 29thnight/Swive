#include "pch.h"
#include "ss_core.hpp"
#include "ss_value.hpp"
#include "ss_vm.hpp"

namespace swiftscript {

// ---- Helper: nil out all weak reference slots for an object ----
void RC::nil_weak_refs(Object* obj) {
    for (Value* weak_slot : obj->rc.weak_refs) {
        if (weak_slot) {
            *weak_slot = Value::null();
        }
    }
    obj->rc.weak_refs.clear();
}

// ---- Helper: execute deinit if object is an instance ----
void RC::execute_deinit_if_needed(VM* vm, Object* obj) {
    if (!vm || obj->type != ObjectType::Instance) return;
    
    auto* inst = static_cast<InstanceObject*>(obj);
    if (!inst->klass) return;
    
    // Look for deinit method
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
    
    // Call deinit if found
    if (!deinit_method.is_null() && deinit_method.is_object()) {
        try {
            vm->execute_deinit(inst, deinit_method);
        } catch (...) {
            // Ignore deinit errors during cleanup
        }
    }
}

// ---- Helper: release child objects in containers ----
void RC::release_children(VM* vm, Object* obj) {
    if (obj->type == ObjectType::List) {
        auto* list = static_cast<ListObject*>(obj);
        for (auto& elem : list->elements) {
            if (elem.is_object() && elem.ref_type() == RefType::Strong) {
                Object* child = elem.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Map) {
        auto* map = static_cast<MapObject*>(obj);
        for (auto& [key, value] : map->entries) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Class) {
        auto* klass = static_cast<ClassObject*>(obj);
        for (auto& [key, value] : klass->methods) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (auto& [key, value] : klass->static_methods) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (auto& [key, value] : klass->static_properties) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (const auto& property : klass->properties) {
            if (property.default_value.is_object() &&
                property.default_value.ref_type() == RefType::Strong) {
                Object* child = property.default_value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
            // Release property observers
            if (property.will_set_observer.is_object() && property.will_set_observer.ref_type() == RefType::Strong) {
                Object* child = property.will_set_observer.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
            if (property.did_set_observer.is_object() && property.did_set_observer.ref_type() == RefType::Strong) {
                Object* child = property.did_set_observer.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
            if (property.lazy_initializer.is_object() && property.lazy_initializer.ref_type() == RefType::Strong) {
                Object* child = property.lazy_initializer.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (const auto& cp : klass->computed_properties) {
            if (cp.getter.is_object() && cp.getter.ref_type() == RefType::Strong) {
                Object* child = cp.getter.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
            if (cp.setter.is_object() && cp.setter.ref_type() == RefType::Strong) {
                Object* child = cp.setter.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Instance) {
        auto* inst = static_cast<InstanceObject*>(obj);
        for (auto& [key, value] : inst->fields) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        // Release the class reference (retained when instance was created)
        if (inst->klass && !inst->klass->rc.is_dead) {
            RC::release(vm, inst->klass);
        }
    } else if (obj->type == ObjectType::BoundMethod) {
        auto* bound = static_cast<BoundMethodObject*>(obj);
        if (bound->receiver) {
            RC::release(vm, bound->receiver);
        }
        if (bound->method.is_object() && bound->method.ref_type() == RefType::Strong) {
            Object* child = bound->method.as_object();
            if (child) {
                RC::release(vm, child);
            }
        }
    } else if (obj->type == ObjectType::BuiltinMethod) {
        auto* method = static_cast<BuiltinMethodObject*>(obj);
        if (method->target) {
            RC::release(vm, method->target);
        }
    } else if (obj->type == ObjectType::StructInstance) {
        auto* inst = static_cast<StructInstanceObject*>(obj);
        for (auto& [key, value] : inst->fields) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        // Release the struct type reference (retained when instance was created)
        if (inst->struct_type && !inst->struct_type->rc.is_dead) {
            RC::release(vm, inst->struct_type);
        }
    } else if (obj->type == ObjectType::Struct) {
        auto* st = static_cast<StructObject*>(obj);
        for (auto& [key, value] : st->methods) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (auto& [key, value] : st->static_methods) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (auto& [key, value] : st->static_properties) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (const auto& property : st->properties) {
            if (property.default_value.is_object() &&
                property.default_value.ref_type() == RefType::Strong) {
                Object* child = property.default_value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
            if (property.will_set_observer.is_object() && property.will_set_observer.ref_type() == RefType::Strong) {
                Object* child = property.will_set_observer.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
            if (property.did_set_observer.is_object() && property.did_set_observer.ref_type() == RefType::Strong) {
                Object* child = property.did_set_observer.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (const auto& cp : st->computed_properties) {
            if (cp.getter.is_object() && cp.getter.ref_type() == RefType::Strong) {
                Object* child = cp.getter.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
            if (cp.setter.is_object() && cp.setter.ref_type() == RefType::Strong) {
                Object* child = cp.setter.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::EnumCase) {
        auto* ec = static_cast<EnumCaseObject*>(obj);
        for (auto& value : ec->associated_values) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        // Release the enum type reference (retained when case was created)
        if (ec->enum_type && !ec->enum_type->rc.is_dead) {
            RC::release(vm, ec->enum_type);
        }
    } else if (obj->type == ObjectType::Enum) {
        auto* en = static_cast<EnumObject*>(obj);
        for (auto& [key, value] : en->methods) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (auto& [key, value] : en->cases) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
        for (const auto& cp : en->computed_properties) {
            if (cp.getter.is_object() && cp.getter.ref_type() == RefType::Strong) {
                Object* child = cp.getter.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
            if (cp.setter.is_object() && cp.setter.ref_type() == RefType::Strong) {
                Object* child = cp.setter.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Function) {
        auto* func = static_cast<FunctionObject*>(obj);
        for (auto& value : func->param_defaults) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Tuple) {
        auto* tuple = static_cast<TupleObject*>(obj);
        for (auto& value : tuple->elements) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && !child->rc.is_dead) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Closure) {
        auto* closure = static_cast<ClosureObject*>(obj);
        // Release the function
        if (closure->function) {
            RC::release(vm, closure->function);
        }
        // Release upvalues
        for (auto* upvalue : closure->upvalues) {
            if (upvalue && !upvalue->rc.is_dead) {
                RC::release(vm, upvalue);
            }
        }
    } else if (obj->type == ObjectType::Upvalue) {
        auto* upvalue = static_cast<UpvalueObject*>(obj);
        // Release the closed value if it holds an object
        if (upvalue->closed.is_object() && upvalue->closed.ref_type() == RefType::Strong) {
            Object* child = upvalue->closed.as_object();
            if (child && !child->rc.is_dead) {
                RC::release(vm, child);
            }
        }
    }
}

// ---- Strong retain ----
void RC::retain(Object* obj) {
    if (!obj) return;

    int32_t old_count = obj->rc.strong_count.fetch_add(1, std::memory_order_acq_rel);

    SS_DEBUG_RC("RETAIN %p [%s] rc: %d -> %d",
                obj, object_type_name(obj->type), old_count, old_count + 1);
}

void RC::adopt(Object* obj) {
    if (!obj) return;

    // Adopt is used when transferring ownership without incrementing refcount
    // The creator's reference becomes the new owner's reference
    int32_t count = obj->rc.strong_count.load(std::memory_order_acquire);
    SS_DEBUG_RC("ADOPT %p [%s] rc: %d -> %d",
                obj, object_type_name(obj->type), count, count);
}

// ---- Strong release ----
void RC::release(VM* vm, Object* obj) {
    if (!obj) return;

    if (vm) {
        vm->record_rc_operation();
    }

    int32_t old_count = obj->rc.strong_count.load(std::memory_order_acquire);
    int32_t new_count = obj->rc.strong_count.fetch_sub(1, std::memory_order_acq_rel) - 1;

    SS_DEBUG_RC("RELEASE %p [%s] rc: %d -> %d",
                obj, object_type_name(obj->type), old_count, new_count);

    if (new_count == 0) {
        // Mark object as logically dead immediately
        obj->rc.is_dead = true;

        // 1. Execute deinit (if instance)
        execute_deinit_if_needed(vm, obj);

        // 2. Nil out weak references
        nil_weak_refs(obj);

        // 3. Release children (recursive)
        release_children(vm, obj);

        // 4. Remove from VM's object list
        if (vm) {
            vm->remove_from_objects_list(obj);
            vm->record_deallocation(*obj);
        }

        // 5. Delete object immediately
        SS_DEBUG_RC("DEALLOCATE %p [%s]", obj, object_type_name(obj->type));
        delete obj;
        
    } else if (new_count < 0) {
        fprintf(stderr, "ERROR: Object %p [%s] has negative refcount: %d\n",
                obj, object_type_name(obj->type), new_count);
        abort();
    }
}

// ---- Weak retain ----
void RC::weak_retain(Object* obj, Value* weak_slot) {
    if (!obj || !weak_slot) return;

    int32_t old_count = obj->rc.weak_count.fetch_add(1, std::memory_order_acq_rel);
    obj->rc.weak_refs.insert(weak_slot);

    SS_DEBUG_RC("WEAK_RETAIN %p weak_rc: %d -> %d",
                obj, old_count, old_count + 1);
}

// ---- Weak release ----
void RC::weak_release(Object* obj, Value* weak_slot) {
    if (!obj || !weak_slot) return;

    // If object is already dead, just clean up the tracking set
    if (obj->rc.is_dead) {
        obj->rc.weak_refs.erase(weak_slot);
        obj->rc.weak_count.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    int32_t old_count = obj->rc.weak_count.load(std::memory_order_acquire);
    int32_t new_count = obj->rc.weak_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
    obj->rc.weak_refs.erase(weak_slot);

    SS_DEBUG_RC("WEAK_RELEASE %p weak_rc: %d -> %d",
                obj, old_count, new_count);

    if (new_count < 0) {
        fprintf(stderr, "ERROR: Object %p [%s] has negative weak refcount: %d\n",
                obj, object_type_name(obj->type), new_count);
        abort();
    }
}

} // namespace swiftscript
