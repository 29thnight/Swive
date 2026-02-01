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

    // Bytecode assembly
    struct Assembly {
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

        void disassemble(const std::string& name) const;
        size_t disassemble_instruction(size_t offset) const;

		void serialize(std::ostream& out) const;
		static Assembly deserialize(std::istream& in);

    private:
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
        constexpr uint16_t kVerMinor = 0;
    }
}
