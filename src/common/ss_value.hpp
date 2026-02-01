#pragma once

#include "ss_core.hpp"

namespace swiftscript {

// Value types (unboxed - stored directly in Value)
using Int = int64_t;
using Float = double;
using Bool = bool;

class VM;
struct Assembly;

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

    void serialize(std::ostream& out) const;
    static Value deserialize(std::istream& in);
    
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
    std::vector<std::string> param_labels;
    std::vector<Value> param_defaults;
    std::vector<bool> param_has_default;
    std::shared_ptr<Assembly> chunk;
    bool is_initializer{false};
    bool is_override{false};

    FunctionObject(std::string function_name,
                   std::vector<std::string> function_params,
                   std::vector<std::string> function_param_labels,
                   std::vector<Value> function_param_defaults,
                   std::vector<bool> function_param_has_default,
                   std::shared_ptr<Assembly> function_chunk,
                   bool initializer,
                   bool override_flag = false);

    std::string to_string() const override;
    size_t memory_size() const override;
};

class ClassObject : public Object {
public:
    std::string name;
    std::unordered_map<std::string, Value> methods; // closures
    std::unordered_map<std::string, Value> static_methods;  // static methods
    std::unordered_map<std::string, Value> static_properties;  // static properties
    struct PropertyInfo {
        std::string name;
        Value default_value;
        bool is_let{false};
        bool is_lazy{false};
        Value lazy_initializer;  // Closure to call on first access (if is_lazy)
        Value will_set_observer;  // Function value (or null if no observer)
        Value did_set_observer;   // Function value (or null if no observer)
    };
    struct ComputedPropertyInfo {
        std::string name;
        Value getter;  // Function value
        Value setter;  // Function value (or nil if read-only)
    };
    std::vector<PropertyInfo> properties;
    std::vector<ComputedPropertyInfo> computed_properties;
    ClassObject* superclass{nullptr};

    explicit ClassObject(std::string n)
        : Object(ObjectType::Class), name(std::move(n)) {}

    std::string to_string() const override {
        return "<class " + name + ">";
    }

    size_t memory_size() const override {
        size_t total = sizeof(ClassObject) + name.capacity();
        for (const auto& [k, v] : methods) {
            total += k.capacity();
            total += sizeof(Value);
        }
        for (const auto& [k, v] : static_methods) {
            total += k.capacity();
            total += sizeof(Value);
        }
        for (const auto& [k, v] : static_properties) {
            total += k.capacity();
            total += sizeof(Value);
        }
        for (const auto& prop : properties) {
            total += prop.name.capacity();
            total += sizeof(Value);
        }
        for (const auto& comp_prop : computed_properties) {
            total += comp_prop.name.capacity();
            total += sizeof(Value) * 2;
        }
        return total;
    }
};

class InstanceObject : public Object {
public:
    ClassObject* klass;
    std::unordered_map<std::string, Value> fields;

    explicit InstanceObject(ClassObject* k)
        : Object(ObjectType::Instance), klass(k) {}

    std::string to_string() const override {
        return "<" + (klass ? klass->name : std::string("instance")) + " instance>";
    }

    size_t memory_size() const override {
        size_t total = sizeof(InstanceObject);
        for (const auto& [k, v] : fields) {
            total += k.capacity();
            total += sizeof(Value);
        }
        return total;
    }
};

// Upvalue for captured variables in closures
class UpvalueObject : public Object {
public:
    Value* location;      // Points to stack slot or closed value
    Value closed;         // Holds value after variable goes out of scope
    UpvalueObject* next_upvalue;  // Linked list of open upvalues
    
    explicit UpvalueObject(Value* slot)
        : Object(ObjectType::Upvalue), location(slot), closed(Value::null()), next_upvalue(nullptr) {}
    
    std::string to_string() const override {
        return "<upvalue>";
    }
    
    size_t memory_size() const override {
        return sizeof(UpvalueObject);
    }
};

// Closure = Function + captured upvalues
class ClosureObject : public Object {
public:
    FunctionObject* function;
    std::vector<UpvalueObject*> upvalues;

    explicit ClosureObject(FunctionObject* fn)
        : Object(ObjectType::Closure), function(fn) {}

    std::string to_string() const override {
        return function ? function->to_string() : "<closure>";
    }

    size_t memory_size() const override {
        return sizeof(ClosureObject) + upvalues.capacity() * sizeof(UpvalueObject*);
    }
};

// Struct type definition (similar to ClassObject but for value types)
class StructObject : public Object {
public:
    std::string name;
    std::unordered_map<std::string, Value> methods;  // closures (may include mutating flag info)
    std::unordered_map<std::string, Value> static_methods;  // static methods
    std::unordered_map<std::string, Value> static_properties;  // static properties
    struct PropertyInfo {
        std::string name;
        Value default_value;
        bool is_let{false};
        bool is_lazy{false};
        Value will_set_observer;  // Function value (or null if no observer)
        Value did_set_observer;   // Function value (or null if no observer)
    };
    struct ComputedPropertyInfo {
        std::string name;
        Value getter;  // Function value
        Value setter;  // Function value (or null if read-only)
    };
    std::vector<PropertyInfo> properties;
    std::vector<ComputedPropertyInfo> computed_properties;
    std::unordered_map<std::string, bool> mutating_methods;  // method_name -> is_mutating

    explicit StructObject(std::string n)
        : Object(ObjectType::Struct), name(std::move(n)) {}

    std::string to_string() const override {
        return "<struct " + name + ">";
    }

    size_t memory_size() const override {
        size_t total = sizeof(StructObject) + name.capacity();
        for (const auto& [k, v] : methods) {
            total += k.capacity();
            total += sizeof(Value);
        }
        for (const auto& [k, v] : static_methods) {
            total += k.capacity();
            total += sizeof(Value);
        }
        for (const auto& [k, v] : static_properties) {
            total += k.capacity();
            total += sizeof(Value);
        }
        for (const auto& prop : properties) {
            total += prop.name.capacity();
            total += sizeof(Value);
        }
        for (const auto& comp_prop : computed_properties) {
            total += comp_prop.name.capacity();
            total += sizeof(Value) * 2;
        }
        return total;
    }
};

// Struct instance (value type - should be copied on assignment)
class StructInstanceObject : public Object {
public:
    StructObject* struct_type;
    std::unordered_map<std::string, Value> fields;

    explicit StructInstanceObject(StructObject* s)
        : Object(ObjectType::StructInstance), struct_type(s) {}

    // Deep copy for value semantics
    StructInstanceObject* deep_copy(VM& vm) const;

    std::string to_string() const override {
        return "<" + (struct_type ? struct_type->name : std::string("struct")) + " instance>";
    }

    size_t memory_size() const override {
        size_t total = sizeof(StructInstanceObject);
        for (const auto& [k, v] : fields) {
            total += k.capacity();
            total += sizeof(Value);
        }
        return total;
    }
};

// Enum type definition (similar to StructObject)
class EnumObject : public Object {
public:
    struct ComputedPropertyInfo {
        std::string name;
        Value getter;  // Function/Closure
        Value setter;  // Function/Closure or null (enum computed properties are read-only)
    };

    std::string name;
    std::unordered_map<std::string, Value> methods;  // closures
    std::unordered_map<std::string, Value> cases;    // case_name -> EnumCaseObject
    std::vector<ComputedPropertyInfo> computed_properties;

    explicit EnumObject(std::string n)
        : Object(ObjectType::Enum), name(std::move(n)) {}

    std::string to_string() const override {
        return "<enum " + name + ">";
    }

    size_t memory_size() const override {
        size_t total = sizeof(EnumObject) + name.capacity();
        for (const auto& [k, v] : methods) {
            total += k.capacity();
            total += sizeof(Value);
        }
        for (const auto& [k, v] : cases) {
            total += k.capacity();
            total += sizeof(Value);
        }
        for (const auto& prop : computed_properties) {
            total += prop.name.capacity();
            total += sizeof(Value) * 2;  // getter + setter
        }
        return total;
    }
};

// Enum case instance
class EnumCaseObject : public Object {
public:
    EnumObject* enum_type;
    std::string case_name;
    Value raw_value;  // Optional raw value (Int, String, etc.)
    std::vector<Value> associated_values;  // For associated values
    std::vector<std::string> associated_labels;  // Labels for associated values

    EnumCaseObject(EnumObject* e, std::string name)
        : Object(ObjectType::EnumCase), enum_type(e), case_name(std::move(name)), raw_value(Value::null()) {}

    std::string to_string() const override {
        std::string result;
        if (enum_type) {
            result = enum_type->name + "." + case_name;
        } else {
            result = case_name;
        }
        
        // Include associated values if present
        if (!associated_values.empty()) {
            result += "(";
            for (size_t i = 0; i < associated_values.size(); ++i) {
                if (i > 0) result += ", ";
                result += associated_values[i].to_string();
            }
            result += ")";
        }
        
        return result;
    }

    size_t memory_size() const override {
        size_t total = sizeof(EnumCaseObject) + case_name.capacity();
        total += associated_values.capacity() * sizeof(Value);
        for (const auto& label : associated_labels) {
            total += label.capacity();
        }
        return total;
    }
};

// Protocol object - defines interface requirements
class ProtocolObject : public Object {
public:
    std::string name;
    std::vector<std::string> method_requirements;    // Method names that must be implemented
    std::vector<std::string> property_requirements;  // Property names that must be provided

    explicit ProtocolObject(std::string n)
        : Object(ObjectType::Protocol), name(std::move(n)) {}

    std::string to_string() const override {
        return "<protocol " + name + ">";
    }

    size_t memory_size() const override {
        size_t total = sizeof(ProtocolObject) + name.capacity();
        for (const auto& method : method_requirements) {
            total += method.capacity();
        }
        for (const auto& prop : property_requirements) {
            total += prop.capacity();
        }
        return total;
    }
};

class BoundMethodObject : public Object {
public:
    Object* receiver;  // InstanceObject* or StructInstanceObject*
    Value method; // closure/function value
    bool is_mutating;  // True for mutating struct methods

    BoundMethodObject(Object* recv, Value m, bool mutating = false)
        : Object(ObjectType::BoundMethod), receiver(recv), method(m), is_mutating(mutating) {
    }

    // Convenience constructor for InstanceObject
    BoundMethodObject(InstanceObject* recv, Value m, bool mutating = false)
        : Object(ObjectType::BoundMethod), receiver(static_cast<Object*>(recv)), method(m), is_mutating(mutating) {
    }

    // Convenience constructor for StructInstanceObject
    BoundMethodObject(StructInstanceObject* recv, Value m, bool mutating = false)
        : Object(ObjectType::BoundMethod), receiver(static_cast<Object*>(recv)), method(m), is_mutating(mutating) {
    }

    std::string to_string() const override {
        return "<bound method>";
    }

    size_t memory_size() const override {
        return sizeof(BoundMethodObject);
    }
};

class BuiltinMethodObject : public Object {
public:
    Object* target;
    std::string method_name;

    BuiltinMethodObject(Object* t, std::string name)
        : Object(ObjectType::BuiltinMethod), target(t), method_name(std::move(name)) {
    }

    std::string to_string() const override {
        return "<builtin method '" + method_name + "'>";
    }

    size_t memory_size() const override {
        return sizeof(BuiltinMethodObject) + method_name.capacity();
    }
};

// Tuple object - immutable collection of named or indexed values
class TupleObject : public Object {
public:
    std::vector<Value> elements;
    std::vector<std::optional<std::string>> labels;  // Optional labels for each element

    TupleObject() : Object(ObjectType::Tuple) {}

    explicit TupleObject(std::vector<Value> elems)
        : Object(ObjectType::Tuple), elements(std::move(elems)) {
        labels.resize(elements.size());  // All unlabeled by default
    }

    TupleObject(std::vector<Value> elems, std::vector<std::optional<std::string>> lbls)
        : Object(ObjectType::Tuple), elements(std::move(elems)), labels(std::move(lbls)) {}

    std::string to_string() const override {
        std::string result = "(";
        for (size_t i = 0; i < elements.size(); ++i) {
            if (i > 0) result += ", ";
            if (labels[i].has_value()) {
                result += labels[i].value() + ": ";
            }
            result += elements[i].to_string();
        }
        result += ")";
        return result;
    }

    size_t memory_size() const override {
        size_t total = sizeof(TupleObject);
        total += elements.capacity() * sizeof(Value);
        for (const auto& label : labels) {
            if (label.has_value()) {
                total += label->capacity();
            }
        }
        return total;
    }

    // Get element by index
    Value get(size_t index) const {
        if (index < elements.size()) {
            return elements[index];
        }
        return Value::null();
    }

    // Get element by label
    Value get(const std::string& label) const {
        for (size_t i = 0; i < labels.size(); ++i) {
            if (labels[i].has_value() && labels[i].value() == label) {
                return elements[i];
            }
        }
        return Value::null();
    }

    // Check if tuple has a specific label
    bool has_label(const std::string& label) const {
        for (const auto& lbl : labels) {
            if (lbl.has_value() && lbl.value() == label) {
                return true;
            }
        }
        return false;
    }
};

// Verify size constraint
static_assert(sizeof(Value) == 16, "Value must be exactly 16 bytes");

} // namespace swiftscript
