#pragma once

#include "ss_value.hpp"
#include "ss_opcodes.hpp"

namespace swiftscript {

struct Assembly;

// Upvalue descriptor for compilation
struct UpvalueInfo {
    uint16_t index;    // Index in enclosing scope (local or upvalue)
    bool is_local;     // true = captures local, false = captures upvalue
};

struct FunctionPrototype {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> param_labels;
    struct ParamDefaultValue {
        bool has_default{false};
        Value value;
        std::optional<std::string> string_value;
    };
    std::vector<ParamDefaultValue> param_defaults;
    std::shared_ptr<Assembly> chunk;
    std::vector<UpvalueInfo> upvalues;  // Captured variables info
    bool is_initializer{false};
    bool is_override{false};
};

// Protocol method requirement
struct ProtocolMethodReq {
    std::string name;
    std::vector<std::string> param_names;
    bool is_mutating{false};
};

// Protocol property requirement
struct ProtocolPropertyReq {
    std::string name;
    bool has_getter{true};
    bool has_setter{false};
};

// Protocol definition
struct Protocol {
    std::string name;
    std::vector<ProtocolMethodReq> method_requirements;
    std::vector<ProtocolPropertyReq> property_requirements;
    std::vector<std::string> inherited_protocols;
};

struct AssemblyManifest {
    std::string name;
    uint16_t version_major{1};
    uint16_t version_minor{0};
};

using string_idx = uint32_t;
using type_idx = uint32_t;
using method_idx = uint32_t;
using field_idx = uint32_t;
using signature_idx = uint32_t;
using body_idx = uint32_t;

struct Range {
    uint32_t start{0};
    uint32_t count{0};
};

enum class TypeFlags : uint32_t {
    Public = 1u << 0,
    Private = 1u << 1,
    Class = 1u << 2,
    Struct = 1u << 3,
    Enum = 1u << 4,
    Interface = 1u << 5,
    Final = 1u << 6,
    Abstract = 1u << 7
};

enum class MethodFlags : uint32_t {
    Static = 1u << 0,
    Virtual = 1u << 1,
    Override = 1u << 2,
    Mutating = 1u << 3
};

enum class FieldFlags : uint32_t {
    Public = 1u << 0,
    Private = 1u << 1,
    Static = 1u << 2,
    Mutable = 1u << 3
};

enum class PropertyFlags : uint32_t {
    Public = 1u << 0,
    Private = 1u << 1,
    Static = 1u << 2
};

struct TypeDef {
    string_idx name{0};
    string_idx namespace_name{0};
    uint32_t flags{0};
    type_idx base_type{0};
    Range method_list{};
    Range field_list{};
    Range property_list{};
    std::vector<type_idx> interfaces;
};

struct MethodDef {
    string_idx name{0};
    uint32_t flags{0};
    signature_idx signature{0};
    body_idx body_ptr{0};
};

struct FieldDef {
    string_idx name{0};
    uint32_t flags{0};
    type_idx type{0};
};

struct PropertyDef {
    string_idx name{0};
    uint32_t flags{0};
    type_idx type{0};
    method_idx getter{0};
    method_idx setter{0};
};

struct MethodBody {
    std::vector<uint8_t> bytecode;
    std::vector<uint32_t> line_info;
    uint32_t max_stack_depth{0};
};

// Bytecode assembly
struct Assembly {
    AssemblyManifest manifest;
    std::vector<TypeDef> type_definitions;
    std::vector<MethodDef> method_definitions;
    std::vector<FieldDef> field_definitions;
    std::vector<PropertyDef> property_definitions;
    std::vector<Value> global_constant_pool;
    std::vector<uint8_t> signature_blob;
    std::vector<MethodBody> method_bodies;

    std::vector<uint8_t> code;
    std::vector<uint32_t> lines;
    std::vector<Value> constants;
    std::vector<std::string> strings;
    std::vector<FunctionPrototype> functions;
    std::vector<std::shared_ptr<Protocol>> protocols;

    void write(uint8_t byte, uint32_t line);
    void write_op(OpCode op, uint32_t line);

    size_t add_constant(Value value);
    size_t add_string(const std::string& str);
    size_t add_function(FunctionPrototype proto);
    size_t add_protocol(std::shared_ptr<Protocol> protocol);

    size_t emit_jump(OpCode op, uint32_t line);
    void patch_jump(size_t offset);

    const std::vector<uint8_t>& bytecode() const;
    const std::vector<uint32_t>& line_info() const;
    const std::vector<Value>& constant_pool() const;
    size_t code_size() const;

    void disassemble(const std::string& name) const;
    size_t disassemble_instruction(size_t offset) const;

	void serialize(std::ostream& out) const;
	static Assembly deserialize(std::istream& in);
    void expand_to_assembly();

private:
    MethodBody& ensure_primary_body();
    size_t simple_instruction(const char* name, size_t offset) const;
    size_t constant_instruction(const char* name, size_t offset) const;
    size_t string_instruction(const char* name, size_t offset) const;
    size_t short_instruction(const char* name, size_t offset) const;
    size_t jump_instruction(const char* name, int sign, size_t offset) const;
    size_t property_instruction(const char* name, size_t offset) const;
};

} // namespace swiftscript


namespace swiftscript {

    namespace
    {
        // ---- IO helpers ----
        template<class T>
        void WritePOD(std::ostream& out, const T& v)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            out.write(reinterpret_cast<const char*>(&v), sizeof(T));
            if (!out) throw std::runtime_error("Assembly::serialize write failed");
        }

        template<class T>
        T ReadPOD(std::istream& in)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            T v{};
            in.read(reinterpret_cast<char*>(&v), sizeof(T));
            if (!in) throw std::runtime_error("Assembly::deserialize read failed");
            return v;
        }

        void WriteBytes(std::ostream& out, const void* data, size_t size)
        {
            out.write(reinterpret_cast<const char*>(data), (std::streamsize)size);
            if (!out) throw std::runtime_error("Assembly::serialize write bytes failed");
        }

        void ReadBytes(std::istream& in, void* data, size_t size)
        {
            in.read(reinterpret_cast<char*>(data), (std::streamsize)size);
            if (!in) throw std::runtime_error("Assembly::deserialize read bytes failed");
        }

        void WriteString(std::ostream& out, const std::string& s)
        {
            uint32_t n = (uint32_t)s.size();
            WritePOD(out, n);
            if (n) WriteBytes(out, s.data(), n);
        }

        std::string ReadString(std::istream& in)
        {
            uint32_t n = ReadPOD<uint32_t>(in);
            std::string s;
            s.resize(n);
            if (n) ReadBytes(in, s.data(), n);
            return s;
        }

        template<class T>
        void WriteVectorPOD(std::ostream& out, const std::vector<T>& v)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            uint32_t n = (uint32_t)v.size();
            WritePOD(out, n);
            if (n) WriteBytes(out, v.data(), sizeof(T) * (size_t)n);
        }

        template<class T>
        std::vector<T> ReadVectorPOD(std::istream& in)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            uint32_t n = ReadPOD<uint32_t>(in);
            std::vector<T> v;
            v.resize(n);
            if (n) ReadBytes(in, v.data(), sizeof(T) * (size_t)n);
            return v;
        }

        // ---- file header ----
        struct AssemblyFileHeader
        {
            uint32_t magic;      // 'SSAS'
            uint16_t verMajor;   // 1
            uint16_t verMinor;   // 0
        };

        constexpr uint32_t kMagicSSAS = 0x53415353; // 'SSAS' little-endian
        constexpr uint16_t kVerMajor = 1;
        constexpr uint16_t kVerMinor = 2;
    }
}
