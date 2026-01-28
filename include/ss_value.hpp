#pragma once

#include "ss_core.hpp"
#include <variant>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace swiftscript {

// Value types (unboxed - stored directly in Value)
using Int = int64_t;
using Float = double;
using Bool = bool;

class VM;
struct Chunk;

// Value class - 16 bytes on 64-bit systems
class Value {
public:
    // Value variants
    enum class Type : uint8_t {
        Null,
        Undefined,
        Bool,
        Int,
        Float,
        Object  // Pointer to heap object
    };
    
private:
    Type type_{Type::Null};
    RefType ref_type_{RefType::Strong};
    uint8_t padding_[6]{};  // Alignment padding
    
    union {
        Bool bool_val;
        Int int_val;
        Float float_val;
        Object* object_val;
    } data_{.int_val = 0};
    
public:
    // Constructors
    Value() : type_(Type::Null) {}
    
    static Value null() { return Value(); }
    static Value undefined() { 
        Value v; 
        v.type_ = Type::Undefined; 
        return v; 
    }
    
    static Value from_bool(Bool b) {
        Value v;
        v.type_ = Type::Bool;
        v.data_.int_val = 0;  // Zero union first
        v.data_.bool_val = b;
        return v;
    }

    static Value from_int(Int i) {
        Value v;
        v.type_ = Type::Int;
        v.data_.int_val = i;
        return v;
    }

    static Value from_float(Float f) {
        Value v;
        v.type_ = Type::Float;
        v.data_.int_val = 0;  // Zero union first
        v.data_.float_val = f;
        return v;
    }

    static Value from_object(Object* obj, RefType ref = RefType::Strong) {
        Value v;
        v.type_ = Type::Object;
        v.ref_type_ = ref;
        v.data_.int_val = 0;  // Zero union first
        v.data_.object_val = obj;
        return v;
    }

    // Check if weak/unowned reference target is still alive
    bool is_alive() const {
        if (!is_object() || data_.object_val == nullptr) return false;
        if (ref_type_ == RefType::Weak || ref_type_ == RefType::Unowned) {
            return !data_.object_val->rc.is_dead;
        }
        return true;
    }

    // Type checking
    bool is_null() const { return type_ == Type::Null; }
    bool is_undefined() const { return type_ == Type::Undefined; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_int() const { return type_ == Type::Int; }
    bool is_float() const { return type_ == Type::Float; }
    bool is_number() const { return is_int() || is_float(); }
    bool is_object() const { return type_ == Type::Object; }
    
    Type type() const { return type_; }
    RefType ref_type() const { return ref_type_; }
    
    // Value access (with assertions)
    Bool as_bool() const {
        SS_ASSERT(is_bool(), "Value is not a bool");
        return data_.bool_val;
    }
    
    Int as_int() const {
        SS_ASSERT(is_int(), "Value is not an int");
        return data_.int_val;
    }
    
    Float as_float() const {
        SS_ASSERT(is_float(), "Value is not a float");
        return data_.float_val;
    }
    
    Object* as_object() const {
        SS_ASSERT(is_object(), "Value is not an object");
        return data_.object_val;
    }
    
    // Safe access with default
    template<typename T>
    std::optional<T> try_as() const;
    
    // Conversion to string
    std::string to_string() const;
    
    // Equality
    bool equals(const Value& other) const;
    
    // For debugging
    static constexpr size_t size() { return sizeof(Value); }
};

// Template specializations for try_as
template<>
inline std::optional<Bool> Value::try_as<Bool>() const {
    if (is_bool()) return data_.bool_val;
    return std::nullopt;
}

template<>
inline std::optional<Int> Value::try_as<Int>() const {
    if (is_int()) return data_.int_val;
    return std::nullopt;
}

template<>
inline std::optional<Float> Value::try_as<Float>() const {
    if (is_float()) return data_.float_val;
    if (is_int()) return static_cast<Float>(data_.int_val);
    return std::nullopt;
}

template<>
inline std::optional<Object*> Value::try_as<Object*>() const {
    if (is_object()) return data_.object_val;
    return std::nullopt;
}

// Specific object types
class StringObject : public Object {
public:
    std::string data;
    
    explicit StringObject(std::string s) 
        : Object(ObjectType::String), data(std::move(s)) {}
    
    std::string to_string() const override { return data; }
    size_t memory_size() const override { 
        return sizeof(StringObject) + data.capacity(); 
    }
};

class ListObject : public Object {
public:
    std::vector<Value> elements;
    
    ListObject() : Object(ObjectType::List) {}
    
    void append(VM& vm, Value value);
    std::string to_string() const override;
    size_t memory_size() const override {
        return sizeof(ListObject) + elements.capacity() * sizeof(Value);
    }
};

class MapObject : public Object {
public:
    std::unordered_map<std::string, Value> entries;
    
    MapObject() : Object(ObjectType::Map) {}
    
    void insert(VM& vm, std::string key, Value value);
    std::string to_string() const override;
    size_t memory_size() const override {
        size_t total = sizeof(MapObject);
        for (const auto& [key, value] : entries) {
            total += sizeof(std::string) + sizeof(Value);
            total += key.capacity();
        }
        // Approximate unordered_map bucket overhead.
        total += entries.bucket_count() * sizeof(void*);
        return total;
    }
};

class FunctionObject : public Object {
public:
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<Chunk> chunk;

    FunctionObject(std::string function_name,
                   std::vector<std::string> function_params,
                   std::shared_ptr<Chunk> function_chunk);

    std::string to_string() const override;
    size_t memory_size() const override;
};

// Verify size constraint
static_assert(sizeof(Value) == 16, "Value must be exactly 16 bytes");

} // namespace swiftscript
