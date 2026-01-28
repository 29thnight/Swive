#include "ss_value.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <limits>

namespace swiftscript {

namespace {
bool nearly_equal(Float a, Float b) {
    const Float diff = std::fabs(a - b);
    const Float scale = std::max({Float{1.0}, std::fabs(a), std::fabs(b)});
    const Float eps = std::numeric_limits<Float>::epsilon();
    return diff <= eps * scale;
}
} // namespace

std::string Value::to_string() const {
    switch (type_) {
        case Type::Null:
            return "null";
        case Type::Undefined:
            return "undefined";
        case Type::Bool:
            return data_.bool_val ? "true" : "false";
        case Type::Int:
            return std::to_string(data_.int_val);
        case Type::Float: {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << data_.float_val;
            return oss.str();
        }
        case Type::Object:
            if (data_.object_val) {
                return data_.object_val->to_string();
            }
            return "null";
    }
    return "unknown";
}

bool Value::equals(const Value& other) const {
    if (type_ != other.type_) {
        // Special case: int and float comparison
        if ((is_int() && other.is_float()) || (is_float() && other.is_int())) {
            Float a = is_float() ? data_.float_val : static_cast<Float>(data_.int_val);
            Float b = other.is_float() ? other.data_.float_val : static_cast<Float>(other.data_.int_val);
            return nearly_equal(a, b);
        }
        return false;
    }
    
    switch (type_) {
        case Type::Null:
        case Type::Undefined:
            return true;
        case Type::Bool:
            return data_.bool_val == other.data_.bool_val;
        case Type::Int:
            return data_.int_val == other.data_.int_val;
        case Type::Float:
            return nearly_equal(data_.float_val, other.data_.float_val);
        case Type::Object:
            // Object equality is identity
            return data_.object_val == other.data_.object_val;
    }
    return false;
}

std::string ListObject::to_string() const {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << elements[i].to_string();
    }
    oss << "]";
    return oss.str();
}

std::string MapObject::to_string() const {
    std::ostringstream oss;
    oss << "{";
    size_t count = 0;
    for (const auto& [key, value] : entries) {
        if (count++ > 0) oss << ", ";
        oss << key << ": " << value.to_string();
    }
    oss << "}";
    return oss.str();
}

} // namespace swiftscript
