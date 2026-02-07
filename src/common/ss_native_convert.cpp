#include "pch.h"
#include "ss_native_convert.hpp"
#include "ss_vm.hpp"

namespace swiftscript {

// ============================================================================
// String Conversion Implementations
// ============================================================================

Value ToValue<std::string>::convert(VM& vm, const std::string& value) {
    // Create a new StringObject and return it as a Value
    StringObject* str_obj = vm.allocate_object<StringObject>(value);
    return Value::from_object(str_obj);
}

Value ToValue<const char*>::convert(VM& vm, const char* value) {
    if (value == nullptr) {
        return Value::null();
    }
    StringObject* str_obj = vm.allocate_object<StringObject>(std::string(value));
    return Value::from_object(str_obj);
}

std::string FromValue<std::string>::convert(const Value& value) {
    if (value.is_null()) {
        return "";
    }

    if (!value.is_object()) {
        // Try to convert primitive types to string
        return value.to_string();
    }

    Object* obj = value.as_object();
    if (obj->type == ObjectType::String) {
        return static_cast<StringObject*>(obj)->data;
    }

    // For other object types, use their to_string method
    return obj->to_string();
}

} // namespace swiftscript
