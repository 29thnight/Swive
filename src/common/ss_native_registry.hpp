// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ss_native_registry.hpp
 * @brief Native C++ binding registry.
 *
 * Provides NativeRegistry for registering C++ functions and types,
 * NativeObject for wrapping C++ objects, and NativeTypeInfo for metadata.
 */

#pragma once

#include "ss_value.hpp"
#include <functional>
#include <span>
#include <unordered_map>
#include <string>
#include <vector>

namespace swive {

// Forward declarations
class VM;
class NativeObject;

// ============================================================================
// Native Function Types
// ============================================================================

// Standalone native function: (VM&, args) -> Value
using NativeFunction = std::function<Value(VM&, std::span<Value>)>;

// Native method bound to an object: (VM&, native_ptr, args) -> Value
using NativeMethod = std::function<Value(VM&, void*, std::span<Value>)>;

// Property getter: (VM&, native_ptr) -> Value
using NativeGetter = std::function<Value(VM&, void*)>;

// Property setter: (VM&, native_ptr, value) -> void
using NativeSetter = std::function<void(VM&, void*, Value)>;

// Constructor: () -> void*
using NativeConstructor = std::function<void*()>;

// Destructor: (void*) -> void
using NativeDestructor = std::function<void(void*)>;

// ============================================================================
// Native Property Info
// ============================================================================

struct NativePropertyInfo {
    std::string name;
    NativeGetter getter;
    NativeSetter setter;  // nullptr if read-only

    bool is_read_only() const { return setter == nullptr; }
};

// ============================================================================
// Native Method Info
// ============================================================================

struct NativeMethodInfo {
    std::string name;
    NativeMethod func;
    int param_count{-1};  // -1 means variadic
};

// ============================================================================
// Native Type Info
// ============================================================================

struct NativeTypeInfo {
    std::string name;
    size_t size{0};

    // Lifecycle
    NativeConstructor constructor;  // Creates new instance
    NativeDestructor destructor;    // Destroys instance

    // Members
    std::unordered_map<std::string, NativePropertyInfo> properties;
    std::unordered_map<std::string, NativeMethodInfo> methods;

    // Type characteristics
    bool is_value_type{false};  // true for struct-like types (copy semantics)

    // Check if property exists
    bool has_property(const std::string& name) const {
        return properties.find(name) != properties.end();
    }

    // Check if method exists
    bool has_method(const std::string& name) const {
        return methods.find(name) != methods.end();
    }

    // Get property info (returns nullptr if not found)
    const NativePropertyInfo* get_property(const std::string& name) const {
        auto it = properties.find(name);
        return it != properties.end() ? &it->second : nullptr;
    }

    // Get method info (returns nullptr if not found)
    const NativeMethodInfo* get_method(const std::string& name) const {
        auto it = methods.find(name);
        return it != methods.end() ? &it->second : nullptr;
    }
};

// ============================================================================
// Native Registry (Singleton)
// ============================================================================

class NativeRegistry {
public:
    // Get the singleton instance
    static NativeRegistry& instance();

    // ========== Function Registration ==========

    // Register a standalone native function
    void register_function(const std::string& name, NativeFunction func);

    // Find a registered function (returns nullptr if not found)
    NativeFunction* find_function(const std::string& name);

    // Check if a function is registered
    bool has_function(const std::string& name) const;

    // Unregister a function
    void unregister_function(const std::string& name);

    // ========== Type Registration ==========

    // Register a native type
    void register_type(const std::string& name, NativeTypeInfo info);

    // Find a registered type (returns nullptr if not found)
    NativeTypeInfo* find_type(const std::string& name);

    // Check if a type is registered
    bool has_type(const std::string& name) const;

    // Unregister a type
    void unregister_type(const std::string& name);

    // ========== Utility ==========

    // Clear all registrations
    void clear();

    // Get all registered function names
    std::vector<std::string> get_function_names() const;

    // Get all registered type names
    std::vector<std::string> get_type_names() const;

    // Get statistics
    size_t function_count() const { return functions_.size(); }
    size_t type_count() const { return types_.size(); }

private:
    // Private constructor for singleton
    NativeRegistry() = default;

    // Prevent copying
    NativeRegistry(const NativeRegistry&) = delete;
    NativeRegistry& operator=(const NativeRegistry&) = delete;

    // Storage
    std::unordered_map<std::string, NativeFunction> functions_;
    std::unordered_map<std::string, NativeTypeInfo> types_;
};

// ============================================================================
// Convenience Macros
// ============================================================================

// Register a simple native function
#define SS_REGISTER_NATIVE_FUNCTION(name, func) \
    swive::NativeRegistry::instance().register_function(name, func)

// Register a native type
#define SS_REGISTER_NATIVE_TYPE(name, info) \
    swive::NativeRegistry::instance().register_type(name, info)

} // namespace swive
