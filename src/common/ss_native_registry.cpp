#include "pch.h"
#include "ss_native_registry.hpp"

namespace swive {

// ============================================================================
// NativeObject Implementation
// ============================================================================

NativeObject::NativeObject(void* ptr, const std::string& type)
    : Object(ObjectType::Native)
    , native_ptr(ptr)
    , type_name(type)
    , prevent_release(false)
    , type_info(nullptr) {
    // Try to get type info from registry
    type_info = NativeRegistry::instance().find_type(type_name);
}

NativeObject::NativeObject(void* ptr, const std::string& type, NativeTypeInfo* info)
    : Object(ObjectType::Native)
    , native_ptr(ptr)
    , type_name(type)
    , prevent_release(false)
    , type_info(info) {
}

NativeObject::~NativeObject() {
    if (native_ptr && !prevent_release) {
        // VM-owned: use type info destructor if available
        if (type_info && type_info->destructor) {
            type_info->destructor(native_ptr);
        }
        // If no destructor registered, we cannot safely delete the pointer
        // The caller is responsible for ensuring proper cleanup
    }
    else if (native_ptr && prevent_release && release_notify) {
        // Engine-owned: notify the engine that VM no longer references this object
        release_notify(release_notify_context, native_ptr,
                       type_name.c_str(), release_notify_user_data);
    }
    native_ptr = nullptr;
}

// ============================================================================
// NativeRegistry Implementation
// ============================================================================

NativeRegistry& NativeRegistry::instance() {
    static NativeRegistry instance;
    return instance;
}

// ========== Function Registration ==========

void NativeRegistry::register_function(const std::string& name, NativeFunction func) {
    functions_[name] = std::move(func);
}

NativeFunction* NativeRegistry::find_function(const std::string& name) {
    auto it = functions_.find(name);
    if (it != functions_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool NativeRegistry::has_function(const std::string& name) const {
    return functions_.find(name) != functions_.end();
}

void NativeRegistry::unregister_function(const std::string& name) {
    functions_.erase(name);
}

// ========== Type Registration ==========

void NativeRegistry::register_type(const std::string& name, NativeTypeInfo info) {
    info.name = name;  // Ensure name is set correctly
    types_[name] = std::move(info);
}

NativeTypeInfo* NativeRegistry::find_type(const std::string& name) {
    auto it = types_.find(name);
    if (it != types_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool NativeRegistry::has_type(const std::string& name) const {
    return types_.find(name) != types_.end();
}

void NativeRegistry::unregister_type(const std::string& name) {
    types_.erase(name);
}

// ========== Utility ==========

void NativeRegistry::clear() {
    functions_.clear();
    types_.clear();
}

std::vector<std::string> NativeRegistry::get_function_names() const {
    std::vector<std::string> names;
    names.reserve(functions_.size());
    for (const auto& [name, _] : functions_) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> NativeRegistry::get_type_names() const {
    std::vector<std::string> names;
    names.reserve(types_.size());
    for (const auto& [name, _] : types_) {
        names.push_back(name);
    }
    return names;
}

} // namespace swive
