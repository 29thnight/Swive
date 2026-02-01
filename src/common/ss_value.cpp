#include "pch.h"
#include "ss_value.hpp"
#include "ss_vm.hpp"
#include "ss_chunk.hpp"

namespace swiftscript {

namespace {
bool nearly_equal(Float a, Float b) {
    const Float diff = std::fabs(a - b);
    const Float scale = std::max({Float{1.0}, std::fabs(a), std::fabs(b)});
    constexpr Float eps = std::numeric_limits<Float>::epsilon();
    return diff <= eps * scale;
}
} // namespace

void Value::serialize(std::ostream& out) const
{
    const uint8_t type = static_cast<uint8_t>(type_);
    const uint8_t ref = static_cast<uint8_t>(ref_type_);

    WritePOD(out, type);
    WritePOD(out, ref);

    switch (type_)
    {
    case Type::Null:
    case Type::Undefined:
        return;

    case Type::Bool: {
        const uint8_t b = data_.bool_val ? 1 : 0;
        WritePOD(out, b);
        return;
    }

    case Type::Int:
        WritePOD(out, data_.int_val);
        return;

    case Type::Float:
        WritePOD(out, data_.float_val);
        return;

    case Type::Object:
        // Object* is a runtime address -> cannot be serialized safely
        throw std::runtime_error("Value::serialize: Object type is not serializable in Assembly constants");
    }

    throw std::runtime_error("Value::serialize: unknown Value::Type");
}

Value Value::deserialize(std::istream& in)
{
    const uint8_t typeU8 = ReadPOD<uint8_t>(in);
    const uint8_t refU8 = ReadPOD<uint8_t>(in);

    const Type t = static_cast<Type>(typeU8);
    const RefType ref = static_cast<RefType>(refU8);

    switch (t)
    {
    case Type::Null:
        return Value::null();

    case Type::Undefined:
        return Value::undefined();

    case Type::Bool: {
        const uint8_t b = ReadPOD<uint8_t>(in);
        return Value::from_bool(b != 0);
    }

    case Type::Int: {
        const Int v = ReadPOD<Int>(in);
        return Value::from_int(v);
    }

    case Type::Float: {
        const Float v = ReadPOD<Float>(in);
        return Value::from_float(v);
    }

    case Type::Object:
        // We wrote refType for forward-compat but still can't restore object graphs here.
        (void)ref;
        throw std::runtime_error("Value::deserialize: Object type is not supported in Assembly constants");
    }

    throw std::runtime_error("Value::deserialize: unknown Value::Type");
}

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
        case Type::Object: {
            // Object equality is usually identity
            if (data_.object_val == other.data_.object_val) {
                return true;
            }

            if (data_.object_val && other.data_.object_val &&
                data_.object_val->type == ObjectType::String &&
                other.data_.object_val->type == ObjectType::String) {
                auto* str_a = static_cast<StringObject*>(data_.object_val);
                auto* str_b = static_cast<StringObject*>(other.data_.object_val);
                return str_a->data == str_b->data;
            }
            
            // Special case: EnumCaseObject comparison by value
            if (data_.object_val && other.data_.object_val &&
                data_.object_val->type == ObjectType::EnumCase &&
                other.data_.object_val->type == ObjectType::EnumCase) {
                
                auto* case_a = static_cast<EnumCaseObject*>(data_.object_val);
                auto* case_b = static_cast<EnumCaseObject*>(other.data_.object_val);
                
                // Same enum type and same case name
                return case_a->enum_type == case_b->enum_type &&
                       case_a->case_name == case_b->case_name;
            }
            
            return false;
        }
    }
    return false;
}

std::string ListObject::to_string() const {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) oss << ", ";
        // Wrap string values in quotes for display
        if (elements[i].is_object() && elements[i].as_object() &&
            elements[i].as_object()->type == ObjectType::String) {
            oss << "\"" << elements[i].to_string() << "\"";
        } else {
            oss << elements[i].to_string();
        }
    }
    oss << "]";
    return oss.str();
}

void ListObject::append(VM& vm, Value value) {
    elements.push_back(value);
    vm.record_allocation_delta(*this, memory_size());
}

std::string MapObject::to_string() const {
    std::ostringstream oss;
    oss << "[";
    size_t count = 0;
    for (const auto& [key, value] : entries) {
        if (count++ > 0) oss << ", ";
        oss << "\"" << key << "\": ";
        // Wrap string values in quotes for display
        if (value.is_object() && value.as_object() &&
            value.as_object()->type == ObjectType::String) {
            oss << "\"" << value.to_string() << "\"";
        } else {
            oss << value.to_string();
        }
    }
    oss << "]";
    return oss.str();
}

void MapObject::insert(VM& vm, std::string key, Value value) {
    auto [it, inserted] = entries.emplace(std::move(key), value);
    if (!inserted) {
        it->second = value;
    }
    vm.record_allocation_delta(*this, memory_size());
}

FunctionObject::FunctionObject(std::string function_name,
                           std::vector<std::string> function_params,
                           std::vector<std::string> function_param_labels,
                           std::vector<Value> function_param_defaults,
                           std::vector<bool> function_param_has_default,
                           std::shared_ptr<Assembly> function_chunk,
                           bool initializer,
                           bool override_flag)
: Object(ObjectType::Function),
  name(std::move(function_name)),
  params(std::move(function_params)),
  param_labels(std::move(function_param_labels)),
  param_defaults(std::move(function_param_defaults)),
  param_has_default(std::move(function_param_has_default)),
  chunk(std::move(function_chunk)),
  is_initializer(initializer),
  is_override(override_flag) {}

std::string FunctionObject::to_string() const {
    if (name.empty()) {
        return "<func>";
    }
    return "<func " + name + ">";
}

size_t FunctionObject::memory_size() const {
    size_t total = sizeof(FunctionObject);
    total += name.capacity();
    for (const auto& param : params) {
        total += param.capacity();
    }
    for (const auto& label : param_labels) {
        total += label.capacity();
    }
    total += param_defaults.capacity() * sizeof(Value);
    total += param_has_default.capacity() * sizeof(bool);
    if (chunk) {
        total += chunk->code.capacity() * sizeof(uint8_t);
        total += chunk->constants.capacity() * sizeof(Value);
        total += chunk->strings.capacity() * sizeof(std::string);
        total += chunk->functions.capacity() * sizeof(FunctionPrototype);
    }
    return total;
}

// StructInstanceObject deep copy for value semantics
StructInstanceObject* StructInstanceObject::deep_copy(VM& vm) const {
    auto* copy = vm.allocate_object<StructInstanceObject>(struct_type);

    // Copy all fields
    for (const auto& [name, value] : fields) {
        // If field is also a struct instance, deep copy it too
        if (value.is_object() && value.as_object() &&
            value.as_object()->type == ObjectType::StructInstance) {
            auto* nested = static_cast<StructInstanceObject*>(value.as_object());
            auto* nested_copy = nested->deep_copy(vm);
            copy->fields[name] = Value::from_object(nested_copy);
        } else {
            copy->fields[name] = value;
        }
    }

    return copy;
}

} // namespace swiftscript
