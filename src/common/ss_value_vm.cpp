// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ss_value_vm.cpp
 * @brief VM-dependent object operations.
 *
 * Implements object methods requiring VM access: ListObject::append,
 * DictObject operations, and StructInstanceObject::copy_with_property.
 */

#include "pch.h"
#include "ss_value.hpp"
#include "ss_vm.hpp"

namespace swive {

void ListObject::append(VM& vm, Value value) {
    elements.push_back(value);
    vm.record_allocation_delta(*this, memory_size());
}

void MapObject::insert(VM& vm, std::string key, Value value) {
    auto [it, inserted] = entries.emplace(std::move(key), value);
    if (!inserted) {
        it->second = value;
    }
    vm.record_allocation_delta(*this, memory_size());
}

// StructInstanceObject deep copy for value semantics
StructInstanceObject* StructInstanceObject::deep_copy(VM& vm) const {
    auto* copy = vm.allocate_object<StructInstanceObject>(struct_type);

    // Retain the struct type for the copy's lifetime
    RC::retain(struct_type);

    // Copy all fields
    for (const auto& [name, value] : fields) {
        // If field is also a struct instance, deep copy it too
        if (value.is_object() && value.as_object() &&
            value.as_object()->type == ObjectType::StructInstance) {
            auto* nested = static_cast<StructInstanceObject*>(value.as_object());
            auto* nested_copy = nested->deep_copy(vm);
            // No retain - allocate's rc:1 is transferred to field ownership
            copy->fields[name] = Value::from_object(nested_copy);
        } else {
            // Retain object values for storage in the copy (shared reference)
            if (value.is_object() && value.ref_type() == RefType::Strong && value.as_object()) {
                RC::retain(value.as_object());
            }
            copy->fields[name] = value;
        }
    }

    return copy;
}

} // namespace swive
