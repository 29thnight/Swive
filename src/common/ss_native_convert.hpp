#pragma once

#include "ss_value.hpp"
#include "ss_native_registry.hpp"
#include <type_traits>
#include <stdexcept>

namespace swiftscript {

// Forward declaration
class VM;

// ============================================================================
// Type Conversion Exceptions
// ============================================================================

class NativeConversionError : public std::runtime_error {
public:
    explicit NativeConversionError(const std::string& msg)
        : std::runtime_error("Native conversion error: " + msg) {}
};

// ============================================================================
// C++ Type -> Value Conversion (to_value)
// ============================================================================

// Primary template (will fail for unsupported types)
template<typename T, typename = void>
struct ToValue {
    static Value convert(VM& vm, const T& value) {
        static_assert(sizeof(T) == 0, "No to_value conversion defined for this type");
        return Value::null();
    }
};

// Specialization for void (returns null)
template<>
struct ToValue<void> {
    static Value convert(VM& vm) {
        return Value::null();
    }
};

// Specialization for bool
template<>
struct ToValue<bool> {
    static Value convert(VM& vm, bool value) {
        return Value::from_bool(value);
    }
};

// Specialization for integral types (int, long, int64_t, etc.)
template<typename T>
struct ToValue<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static Value convert(VM& vm, T value) {
        return Value::from_int(static_cast<Int>(value));
    }
};

// Specialization for floating point types (float, double)
template<typename T>
struct ToValue<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static Value convert(VM& vm, T value) {
        return Value::from_float(static_cast<Float>(value));
    }
};

// Specialization for std::string
template<>
struct ToValue<std::string> {
    static Value convert(VM& vm, const std::string& value);
};

// Specialization for const char*
template<>
struct ToValue<const char*> {
    static Value convert(VM& vm, const char* value);
};

// Specialization for Value (pass-through)
template<>
struct ToValue<Value> {
    static Value convert(VM& vm, const Value& value) {
        return value;
    }
};

// Convenience function
//template<typename T>
//Value to_value(VM& vm, const T& value) {
//    return ToValue<std::decay_t<T>>::convert(vm, value);
//}

// src\common\ss_native_convert.hpp
template<typename T>
Value to_value(VM& vm, T&& value) {
    using Decayed = std::decay_t<T>;
    return ToValue<Decayed>::convert(vm, std::forward<T>(value));
}

// Overload for void return
inline Value to_value_void(VM& vm) {
    return Value::null();
}

// ============================================================================
// Value -> C++ Type Conversion (from_value)
// ============================================================================

// Primary template (will fail for unsupported types)
template<typename T, typename = void>
struct FromValue {
    static T convert(const Value& value) {
        static_assert(sizeof(T) == 0, "No from_value conversion defined for this type");
        return T{};
    }
};

// Specialization for bool
template<>
struct FromValue<bool> {
    static bool convert(const Value& value) {
        if (value.is_bool()) {
            return value.as_bool();
        }
        if (value.is_int()) {
            return value.as_int() != 0;
        }
        if (value.is_null()) {
            return false;
        }
        throw NativeConversionError("Cannot convert to bool");
    }
};

// Specialization for integral types
template<typename T>
struct FromValue<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    static T convert(const Value& value) {
        if (value.is_int()) {
            return static_cast<T>(value.as_int());
        }
        if (value.is_float()) {
            return static_cast<T>(value.as_float());
        }
        throw NativeConversionError("Cannot convert to integer type");
    }
};

// Specialization for floating point types
template<typename T>
struct FromValue<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static T convert(const Value& value) {
        if (value.is_float()) {
            return static_cast<T>(value.as_float());
        }
        if (value.is_int()) {
            return static_cast<T>(value.as_int());
        }
        throw NativeConversionError("Cannot convert to floating point type");
    }
};

// Specialization for std::string
template<>
struct FromValue<std::string> {
    static std::string convert(const Value& value);
};

// Specialization for Value (pass-through)
template<>
struct FromValue<Value> {
    static Value convert(const Value& value) {
        return value;
    }
};

// Convenience function
template<typename T>
T from_value(const Value& value) {
    return FromValue<std::decay_t<T>>::convert(value);
}

// ============================================================================
// Native Object Pointer Extraction
// ============================================================================

// Extract native pointer from a Value (must be a NativeObject)
template<typename T>
T* from_native_value(const Value& value) {
    if (!value.is_object()) {
        throw NativeConversionError("Value is not an object");
    }

    Object* obj = value.as_object();
    if (!obj || obj->type != ObjectType::Native) {
        throw NativeConversionError("Value is not a native object");
    }

    NativeObject* native_obj = static_cast<NativeObject*>(obj);
    return static_cast<T*>(native_obj->native_ptr);
}

// Safe extraction (returns nullptr on failure)
template<typename T>
T* try_from_native_value(const Value& value) {
    if (!value.is_object()) {
        return nullptr;
    }

    Object* obj = value.as_object();
    if (!obj || obj->type != ObjectType::Native) {
        return nullptr;
    }

    NativeObject* native_obj = static_cast<NativeObject*>(obj);
    return static_cast<T*>(native_obj->native_ptr);
}

// Get NativeObject from Value
inline NativeObject* get_native_object(const Value& value) {
    if (!value.is_object()) {
        return nullptr;
    }

    Object* obj = value.as_object();
    if (!obj || obj->type != ObjectType::Native) {
        return nullptr;
    }

    return static_cast<NativeObject*>(obj);
}

// ============================================================================
// Function Traits (for method binding)
// ============================================================================

// Primary template
template<typename T>
struct FunctionTraits;

// Specialization for function pointers
template<typename Ret, typename... Args>
struct FunctionTraits<Ret(*)(Args...)> {
    using return_type = Ret;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);

    template<size_t N>
    using arg = std::tuple_element_t<N, args_tuple>;
};

// Specialization for member function pointers
template<typename Ret, typename Class, typename... Args>
struct FunctionTraits<Ret(Class::*)(Args...)> {
    using return_type = Ret;
    using class_type = Class;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);

    template<size_t N>
    using arg = std::tuple_element_t<N, args_tuple>;
};

// Specialization for const member function pointers
template<typename Ret, typename Class, typename... Args>
struct FunctionTraits<Ret(Class::*)(Args...) const> {
    using return_type = Ret;
    using class_type = Class;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);

    template<size_t N>
    using arg = std::tuple_element_t<N, args_tuple>;
};

// Specialization for std::function
template<typename Ret, typename... Args>
struct FunctionTraits<std::function<Ret(Args...)>> {
    using return_type = Ret;
    using args_tuple = std::tuple<Args...>;
    static constexpr size_t arity = sizeof...(Args);

    template<size_t N>
    using arg = std::tuple_element_t<N, args_tuple>;
};

// ============================================================================
// Method Invocation Helpers
// ============================================================================

namespace detail {

// Helper to invoke a member function with arguments from span
template<typename Ret, typename Class, typename... Args, size_t... Is>
Ret invoke_method_impl(Class* obj, Ret(Class::*method)(Args...),
                       std::span<Value> args, std::index_sequence<Is...>) {
    return (obj->*method)(from_value<std::decay_t<Args>>(args[Is])...);
}

template<typename Ret, typename Class, typename... Args, size_t... Is>
Ret invoke_method_impl(Class* obj, Ret(Class::*method)(Args...) const,
                       std::span<Value> args, std::index_sequence<Is...>) {
    return (obj->*method)(from_value<std::decay_t<Args>>(args[Is])...);
}

// Helper to invoke a static function with arguments from span
template<typename Ret, typename... Args, size_t... Is>
Ret invoke_static_impl(Ret(*func)(Args...),
                       std::span<Value> args, std::index_sequence<Is...>) {
    return func(from_value<std::decay_t<Args>>(args[Is])...);
}

} // namespace detail

// Invoke member method and convert result to Value
template<typename Class, typename Ret, typename... Args>
Value invoke_method(VM& vm, Class* obj, Ret(Class::*method)(Args...), std::span<Value> args) {
    if (args.size() < sizeof...(Args)) {
        throw NativeConversionError("Not enough arguments for method call");
    }

    if constexpr (std::is_void_v<Ret>) {
        detail::invoke_method_impl(obj, method, args, std::index_sequence_for<Args...>{});
        return Value::null();
    } else {
        Ret result = detail::invoke_method_impl(obj, method, args, std::index_sequence_for<Args...>{});
        return to_value(vm, result);
    }
}

// Invoke const member method and convert result to Value
template<typename Class, typename Ret, typename... Args>
Value invoke_method(VM& vm, Class* obj, Ret(Class::*method)(Args...) const, std::span<Value> args) {
    if (args.size() < sizeof...(Args)) {
        throw NativeConversionError("Not enough arguments for method call");
    }

    if constexpr (std::is_void_v<Ret>) {
        detail::invoke_method_impl(obj, method, args, std::index_sequence_for<Args...>{});
        return Value::null();
    } else {
        Ret result = detail::invoke_method_impl(obj, method, args, std::index_sequence_for<Args...>{});
        return to_value(vm, result);
    }
}

// Invoke static function and convert result to Value
template<typename Ret, typename... Args>
Value invoke_static(VM& vm, Ret(*func)(Args...), std::span<Value> args) {
    if (args.size() < sizeof...(Args)) {
        throw NativeConversionError("Not enough arguments for function call");
    }

    if constexpr (std::is_void_v<Ret>) {
        detail::invoke_static_impl(func, args, std::index_sequence_for<Args...>{});
        return Value::null();
    } else {
        Ret result = detail::invoke_static_impl(func, args, std::index_sequence_for<Args...>{});
        return to_value(vm, result);
    }
}

} // namespace swiftscript
