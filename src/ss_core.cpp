#include "ss_core.hpp"
#include "ss_value.hpp"
#include "ss_vm.hpp"

namespace swiftscript {

void RC::retain(Object* obj) {
    if (!obj) return;
    
    int32_t old_count = obj->rc.strong_count.fetch_add(1, std::memory_order_relaxed);
    
    const char* type_name = "Unknown";
    switch(obj->type) {
        case ObjectType::String: type_name = "String"; break;
        case ObjectType::List: type_name = "List"; break;
        case ObjectType::Map: type_name = "Map"; break;
        case ObjectType::Function: type_name = "Function"; break;
        case ObjectType::Closure: type_name = "Closure"; break;
        case ObjectType::Class: type_name = "Class"; break;
        case ObjectType::Instance: type_name = "Instance"; break;
        default: break;
    }
    
    SS_DEBUG_RC("RETAIN %p [%s] rc: %d -> %d", 
                obj, type_name, old_count, old_count + 1);
}

void RC::release(VM* vm, Object* obj) {
    if (!obj) return;

    if (vm) {
        vm->record_rc_operation();
    }
    
    int32_t old_count = obj->rc.strong_count.load(std::memory_order_relaxed);
    int32_t new_count = obj->rc.strong_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
    
    const char* type_name = "Unknown";
    switch(obj->type) {
        case ObjectType::String: type_name = "String"; break;
        case ObjectType::List: type_name = "List"; break;
        case ObjectType::Map: type_name = "Map"; break;
        default: break;
    }
    
    SS_DEBUG_RC("RELEASE %p [%s] rc: %d -> %d", 
                obj, type_name, old_count, new_count);
    
    if (new_count == 0) {
        if (!vm) {
            for (Value* weak_slot : obj->rc.weak_refs) {
                *weak_slot = Value::null();
            }
            obj->rc.weak_refs.clear();

            if (obj->type == ObjectType::List) {
                auto* list = static_cast<ListObject*>(obj);
                for (auto& elem : list->elements) {
                    if (elem.is_object() && elem.ref_type() == RefType::Strong) {
                        RC::release(nullptr, elem.as_object());
                    }
                }
            } else if (obj->type == ObjectType::Map) {
                auto* map = static_cast<MapObject*>(obj);
                for (auto& [key, value] : map->entries) {
                    if (value.is_object() && value.ref_type() == RefType::Strong) {
                        RC::release(nullptr, value.as_object());
                    }
                }
            }

            delete obj;
            return;
        }
        // RC reached zero - add to deferred release list
        vm->add_deferred_release(obj);
    } else if (new_count < 0) {
        // This should never happen
        fprintf(stderr, "ERROR: Object %p has negative refcount: %d\n", obj, new_count);
        abort();
    }
}

void RC::weak_retain(Object* obj, Value* weak_slot) {
    if (!obj) return;
    
    int32_t old_count = obj->rc.weak_count.fetch_add(1, std::memory_order_relaxed);
    obj->rc.weak_refs.insert(weak_slot);
    
    SS_DEBUG_RC("WEAK_RETAIN %p weak_rc: %d -> %d", 
                obj, old_count, old_count + 1);
}

void RC::weak_release(Object* obj, Value* weak_slot) {
    if (!obj) return;
    
    int32_t old_count = obj->rc.weak_count.load(std::memory_order_relaxed);
    int32_t new_count = obj->rc.weak_count.fetch_sub(1, std::memory_order_relaxed) - 1;
    obj->rc.weak_refs.erase(weak_slot);
    
    SS_DEBUG_RC("WEAK_RELEASE %p weak_rc: %d -> %d", 
                obj, old_count, new_count);

    if (new_count < 0) {
        fprintf(stderr, "ERROR: Object %p has negative weak refcount: %d\n", obj, new_count);
        abort();
    }
}

void RC::process_deferred_releases(VM* vm) {
    auto& deferred = vm->get_deferred_releases();
    
    if (deferred.empty()) return;
    
    SS_DEBUG_RC("Processing %zu deferred releases", deferred.size());
    
    for (Object* obj : deferred) {
        // Nil out all weak references
        for (Value* weak_slot : obj->rc.weak_refs) {
            *weak_slot = Value::null();
        }
        obj->rc.weak_refs.clear();
        
        // Recursively release child objects
        if (obj->type == ObjectType::List) {
            auto* list = static_cast<ListObject*>(obj);
            for (auto& elem : list->elements) {
                if (elem.is_object() && elem.ref_type() == RefType::Strong) {
                    RC::release(vm, elem.as_object());
                }
            }
        } else if (obj->type == ObjectType::Map) {
            auto* map = static_cast<MapObject*>(obj);
            for (auto& [key, value] : map->entries) {
                if (value.is_object() && value.ref_type() == RefType::Strong) {
                    RC::release(vm, value.as_object());
                }
            }
        }
        
        const char* type_name = "Unknown";
        switch(obj->type) {
            case ObjectType::String: type_name = "String"; break;
            case ObjectType::List: type_name = "List"; break;
            case ObjectType::Map: type_name = "Map"; break;
            default: break;
        }
        
        SS_DEBUG_RC("DEALLOCATE %p [%s]", obj, type_name);
        
        // Actually delete the object
        vm->remove_from_objects_list(obj);
        vm->record_deallocation(*obj);
        delete obj;
    }
    
    deferred.clear();
}

} // namespace swiftscript
