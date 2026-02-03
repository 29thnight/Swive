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

// ---- Helper: recursively release child objects in containers ----
void RC::release_children(VM* vm, Object* obj, std::unordered_set<Object*>& deleted_set) {
    if (obj->type == ObjectType::List) {
        auto* list = static_cast<ListObject*>(obj);
        for (auto& elem : list->elements) {
            if (elem.is_object() && elem.ref_type() == RefType::Strong) {
                Object* child = elem.as_object();
                if (child && deleted_set.find(child) == deleted_set.end()) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Map) {
        auto* map = static_cast<MapObject*>(obj);
        for (auto& [key, value] : map->entries) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && deleted_set.find(child) == deleted_set.end()) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Class) {
        auto* klass = static_cast<ClassObject*>(obj);
        for (auto& [key, value] : klass->methods) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && deleted_set.find(child) == deleted_set.end()) {
                    RC::release(vm, child);
                }
            }
        }
        for (const auto& property : klass->properties) {
            if (property.default_value.is_object() &&
                property.default_value.ref_type() == RefType::Strong) {
                Object* child = property.default_value.as_object();
                if (child && deleted_set.find(child) == deleted_set.end()) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::Instance) {
        auto* inst = static_cast<InstanceObject*>(obj);
        for (auto& [key, value] : inst->fields) {
            if (value.is_object() && value.ref_type() == RefType::Strong) {
                Object* child = value.as_object();
                if (child && deleted_set.find(child) == deleted_set.end()) {
                    RC::release(vm, child);
                }
            }
        }
    } else if (obj->type == ObjectType::BoundMethod) {
        auto* bound = static_cast<BoundMethodObject*>(obj);
        if (bound->receiver && deleted_set.find(bound->receiver) == deleted_set.end()) {
            RC::release(vm, bound->receiver);
        }
        if (bound->method.is_object() && bound->method.ref_type() == RefType::Strong) {
            Object* child = bound->method.as_object();
            if (child && deleted_set.find(child) == deleted_set.end()) {
                RC::release(vm, child);
            }
        }
    } else if (obj->type == ObjectType::BuiltinMethod) {
        auto* method = static_cast<BuiltinMethodObject*>(obj);
        if (method->target && deleted_set.find(method->target) == deleted_set.end()) {
            RC::release(vm, method->target);
        }
    }
}

// ---- Strong retain ----
void RC::retain(Object* obj) {
    if (!obj) return;

    bool had_creator_ref = obj->rc.has_creator_ref.exchange(false, std::memory_order_acq_rel);
    if (had_creator_ref) {
        int32_t count = obj->rc.strong_count.load(std::memory_order_acquire);
        SS_DEBUG_RC("RETAIN %p [%s] rc: %d -> %d (adopt)",
                    obj, object_type_name(obj->type), count, count);
        return;
    }

    int32_t old_count = obj->rc.strong_count.fetch_add(1, std::memory_order_acq_rel);

    SS_DEBUG_RC("RETAIN %p [%s] rc: %d -> %d",
                obj, object_type_name(obj->type), old_count, old_count + 1);
}

void RC::adopt(Object* obj) {
    if (!obj) return;

    bool had_creator_ref = obj->rc.has_creator_ref.exchange(false, std::memory_order_acq_rel);
    if (!had_creator_ref) {
        return;
    }

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
        // Mark object as logically dead immediately so weak refs can detect it
        obj->rc.is_dead = true;

        // Nil out weak references immediately (both paths)
        nil_weak_refs(obj);

        if (!vm) {
            // No VM context: release children and delete immediately
            std::unordered_set<Object*> deleted_set;
            deleted_set.insert(obj);
            release_children(nullptr, obj, deleted_set);
            delete obj;
            return;
        }

        // With VM context: add to deferred release list
        vm->add_deferred_release(obj);
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

// ---- Deferred release processing ----
void RC::process_deferred_releases(VM* vm) {
    auto& deferred = vm->get_deferred_releases();

    if (deferred.empty()) return;

    SS_DEBUG_RC("Processing %zu deferred releases", deferred.size());

    // Swap out the current deferred list so new deferrals during processing
    // go into a fresh list (reentrant safety)
    std::vector<Object*> to_process;
    to_process.swap(deferred);

    // Track already-deleted objects to prevent double-free from circular refs
    std::unordered_set<Object*> deleted_set;

    for (Object* obj : to_process) {
        if (deleted_set.find(obj) != deleted_set.end()) {
            continue;  // Already deleted via a child release
        }

        // Call deinit before releasing children
        if (obj->type == ObjectType::Instance) {
            auto* inst = static_cast<InstanceObject*>(obj);
            if (inst->klass) {
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
        }

        // Weak refs should already be nil'd in release(), but ensure cleanup
        nil_weak_refs(obj);

        // Release child objects
        deleted_set.insert(obj);
        release_children(vm, obj, deleted_set);

        SS_DEBUG_RC("DEALLOCATE %p [%s]", obj, object_type_name(obj->type));

        vm->remove_from_objects_list(obj);
        vm->record_deallocation(*obj);
        delete obj;
    }

    // If new objects were deferred during processing, they remain in
    // deferred_releases_ and will be picked up on the next cleanup cycle.
}

} // namespace swiftscript
