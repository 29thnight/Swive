#include "pch.h"
#include "ss_chunk.hpp"

namespace swiftscript {

void Assembly::write(uint8_t byte, uint32_t line) {
    auto& body = ensure_primary_body();
    body.bytecode.push_back(byte);
    body.line_info.push_back(line);
    code.push_back(byte);
    lines.push_back(line);
}

void Assembly::write_op(OpCode op, uint32_t line) {
    write(static_cast<uint8_t>(op), line);
}

size_t Assembly::add_constant(Value value) {
    constants.push_back(value);
    global_constant_pool.push_back(constants.back());
    return constants.size() - 1;
}

size_t Assembly::add_string(const std::string& str) {
    // �̹� �����ϴ� ���ڿ����� Ȯ��
    for (size_t i = 0; i < strings.size(); ++i) {
        if (strings[i] == str) {
            return i;
        }
    }
    strings.push_back(str);
    return strings.size() - 1;
}

size_t Assembly::add_function(FunctionPrototype proto) {
    functions.push_back(std::move(proto));
    return functions.size() - 1;
}

size_t Assembly::add_protocol(std::shared_ptr<Protocol> protocol) {
    protocols.push_back(std::move(protocol));
    return protocols.size() - 1;
}

size_t Assembly::emit_jump(OpCode op, uint32_t line) {
    write_op(op, line);
    // �÷��̽�Ȧ���� 0xFFFF �ۼ�
    write(0xFF, line);
    write(0xFF, line);
    return bytecode().size() - 2;
}

void Assembly::patch_jump(size_t offset) {
    // ���� ���ɾ� ���ĺ��� ���� ��ġ������ �Ÿ� ���
    size_t jump = bytecode().size() - offset - 2;
    
    if (jump > 0xFFFF) {
        throw std::runtime_error("Too much code to jump over");
    }

    code[offset] = (jump >> 8) & 0xFF;
    code[offset + 1] = jump & 0xFF;
    if (!method_bodies.empty()) {
        auto& body = method_bodies.front();
        body.bytecode[offset] = (jump >> 8) & 0xFF;
        body.bytecode[offset + 1] = jump & 0xFF;
    }
}

const std::vector<uint8_t>& Assembly::bytecode() const {
    if (!method_definitions.empty()) {
        body_idx idx = method_definitions.front().body_ptr;
        if (idx < method_bodies.size()) {
            return method_bodies[idx].bytecode;
        }
    }
    if (!method_bodies.empty()) {
        return method_bodies.front().bytecode;
    }
    return code;
}

const std::vector<uint32_t>& Assembly::line_info() const {
    if (!method_definitions.empty()) {
        body_idx idx = method_definitions.front().body_ptr;
        if (idx < method_bodies.size()) {
            return method_bodies[idx].line_info;
        }
    }
    if (!method_bodies.empty()) {
        return method_bodies.front().line_info;
    }
    return lines;
}

const std::vector<Value>& Assembly::constant_pool() const {
    if (!global_constant_pool.empty()) {
        return global_constant_pool;
    }
    return constants;
}

size_t Assembly::code_size() const {
    return bytecode().size();
}

MethodBody& Assembly::ensure_primary_body() {
    if (method_bodies.empty()) {
        MethodBody body{};
        body.bytecode = code;
        body.line_info = lines;
        method_bodies.push_back(std::move(body));
    }
    return method_bodies.front();
}

void Assembly::disassemble(const std::string& name) const {
    std::cout << "== " << name << " ==\n";
    const auto& code_view = bytecode();
    for (size_t offset = 0; offset < code_view.size();) {
        offset = disassemble_instruction(offset);
    }
}

size_t Assembly::disassemble_instruction(size_t offset) const {
    const auto& code_view = bytecode();
    const auto& line_view = line_info();
    std::cout << std::setw(4) << std::setfill('0') << offset << " ";
    
    if (offset > 0 && offset < line_view.size() && line_view[offset] == line_view[offset - 1]) {
        std::cout << "   | ";
    } else {
        const uint32_t line = offset < line_view.size() ? line_view[offset] : 0;
        std::cout << std::setw(4) << line << " ";
    }
    
    OpCode instruction = static_cast<OpCode>(code_view[offset]);
    
    switch (instruction) {
        case OpCode::OP_CONSTANT:
            return constant_instruction("OP_CONSTANT", offset);
        case OpCode::OP_STRING:
            return string_instruction("OP_STRING", offset);
        case OpCode::OP_NIL:
            return simple_instruction("OP_NIL", offset);
        case OpCode::OP_TRUE:
            return simple_instruction("OP_TRUE", offset);
        case OpCode::OP_FALSE:
            return simple_instruction("OP_FALSE", offset);
        case OpCode::OP_POP:
            return simple_instruction("OP_POP", offset);
        case OpCode::OP_ADD:
            return simple_instruction("OP_ADD", offset);
        case OpCode::OP_SUBTRACT:
            return simple_instruction("OP_SUBTRACT", offset);
        case OpCode::OP_MULTIPLY:
            return simple_instruction("OP_MULTIPLY", offset);
        case OpCode::OP_DIVIDE:
            return simple_instruction("OP_DIVIDE", offset);
        case OpCode::OP_MODULO:
            return simple_instruction("OP_MODULO", offset);
        case OpCode::OP_NEGATE:
            return simple_instruction("OP_NEGATE", offset);
        case OpCode::OP_BITWISE_NOT:
            return simple_instruction("OP_BITWISE_NOT", offset);
        case OpCode::OP_EQUAL:
            return simple_instruction("OP_EQUAL", offset);
        case OpCode::OP_NOT_EQUAL:
            return simple_instruction("OP_NOT_EQUAL", offset);
        case OpCode::OP_LESS:
            return simple_instruction("OP_LESS", offset);
        case OpCode::OP_GREATER:
            return simple_instruction("OP_GREATER", offset);
        case OpCode::OP_LESS_EQUAL:
            return simple_instruction("OP_LESS_EQUAL", offset);
        case OpCode::OP_GREATER_EQUAL:
            return simple_instruction("OP_GREATER_EQUAL", offset);
        case OpCode::OP_NOT:
            return simple_instruction("OP_NOT", offset);
        case OpCode::OP_AND:
            return simple_instruction("OP_AND", offset);
        case OpCode::OP_OR:
            return simple_instruction("OP_OR", offset);
        case OpCode::OP_GET_GLOBAL:
            return short_instruction("OP_GET_GLOBAL", offset);
        case OpCode::OP_SET_GLOBAL:
            return short_instruction("OP_SET_GLOBAL", offset);
        case OpCode::OP_GET_LOCAL:
            return short_instruction("OP_GET_LOCAL", offset);
        case OpCode::OP_SET_LOCAL:
            return short_instruction("OP_SET_LOCAL", offset);
        case OpCode::OP_JUMP:
            return jump_instruction("OP_JUMP", 1, offset);
        case OpCode::OP_JUMP_IF_FALSE:
            return jump_instruction("OP_JUMP_IF_FALSE", 1, offset);
        case OpCode::OP_JUMP_IF_NIL:
            return jump_instruction("OP_JUMP_IF_NIL", 1, offset);
        case OpCode::OP_LOOP:
            return jump_instruction("OP_LOOP", -1, offset);
        case OpCode::OP_FUNCTION:
            return short_instruction("OP_FUNCTION", offset);
        case OpCode::OP_CLASS:
            return string_instruction("OP_CLASS", offset);
        case OpCode::OP_METHOD:
            return string_instruction("OP_METHOD", offset);
        case OpCode::OP_DEFINE_PROPERTY:
            return property_instruction("OP_DEFINE_PROPERTY", offset);
        case OpCode::OP_INHERIT:
            return simple_instruction("OP_INHERIT", offset);
        case OpCode::OP_CALL:
            return short_instruction("OP_CALL", offset);
        case OpCode::OP_CALL_NAMED: {
            uint16_t arg_count = (code_view[offset + 1] << 8) | code_view[offset + 2];
            std::cout << std::setw(16) << std::left << "OP_CALL_NAMED" << " "
                      << std::setw(4) << arg_count << "\n";
            return offset + 3 + arg_count * 2;
        }
        case OpCode::OP_RETURN:
            return simple_instruction("OP_RETURN", offset);
        case OpCode::OP_GET_PROPERTY:
            return short_instruction("OP_GET_PROPERTY", offset);
        case OpCode::OP_SET_PROPERTY:
            return short_instruction("OP_SET_PROPERTY", offset);
        case OpCode::OP_SUPER:
            return string_instruction("OP_SUPER", offset);
        case OpCode::OP_OPTIONAL_CHAIN:
            return short_instruction("OP_OPTIONAL_CHAIN", offset);
        case OpCode::OP_UNWRAP:
            return simple_instruction("OP_UNWRAP", offset);
        case OpCode::OP_NIL_COALESCE:
            return simple_instruction("OP_NIL_COALESCE", offset);
        case OpCode::OP_RANGE_INCLUSIVE:
            return simple_instruction("OP_RANGE_INCLUSIVE", offset);
        case OpCode::OP_RANGE_EXCLUSIVE:
            return simple_instruction("OP_RANGE_EXCLUSIVE", offset);
        case OpCode::OP_ARRAY:
            return short_instruction("OP_ARRAY", offset);
        case OpCode::OP_DICT:
            return short_instruction("OP_DICT", offset);
        case OpCode::OP_GET_SUBSCRIPT:
            return simple_instruction("OP_GET_SUBSCRIPT", offset);
        case OpCode::OP_SET_SUBSCRIPT:
            return simple_instruction("OP_SET_SUBSCRIPT", offset);
        case OpCode::OP_GET_UPVALUE:
            return short_instruction("OP_GET_UPVALUE", offset);
        case OpCode::OP_SET_UPVALUE:
            return short_instruction("OP_SET_UPVALUE", offset);
        case OpCode::OP_CLOSE_UPVALUE:
            return simple_instruction("OP_CLOSE_UPVALUE", offset);
        case OpCode::OP_CLOSURE:
            return short_instruction("OP_CLOSURE", offset);
        case OpCode::OP_READ_LINE:
            return simple_instruction("OP_READ_LINE", offset);
        case OpCode::OP_PRINT:
            return simple_instruction("OP_PRINT", offset);
        case OpCode::OP_STRUCT:
            return string_instruction("OP_STRUCT", offset);
        case OpCode::OP_STRUCT_METHOD: {
            uint16_t str_idx = (code_view[offset + 1] << 8) | code_view[offset + 2];
            uint8_t is_mutating = code_view[offset + 3];
            std::cout << std::setw(16) << std::left << "OP_STRUCT_METHOD" << " "
                      << std::setw(4) << str_idx << " ("
                      << (is_mutating ? "mutating" : "non-mutating") << ")\n";
            return offset + 4;
        }
        case OpCode::OP_COPY_VALUE:
            return simple_instruction("OP_COPY_VALUE", offset);
        case OpCode::OP_ENUM:
            return string_instruction("OP_ENUM", offset);
        case OpCode::OP_ENUM_CASE:
        {
            uint16_t str_idx = (code_view[offset + 1] << 8) | code_view[offset + 2];
            uint8_t assoc_count = code_view[offset + 3];
            std::cout << std::setw(16) << std::left << "OP_ENUM_CASE" << " "
                      << std::setw(4) << str_idx << " (assoc " << static_cast<int>(assoc_count) << ")\n";
            return offset + 4 + assoc_count * 2;
        }
        case OpCode::OP_MATCH_ENUM_CASE:
            return string_instruction("OP_MATCH_ENUM_CASE", offset);
        case OpCode::OP_GET_ASSOCIATED:
            return short_instruction("OP_GET_ASSOCIATED", offset);
        case OpCode::OP_PROTOCOL:
            return short_instruction("OP_PROTOCOL", offset);
        case OpCode::OP_DEFINE_GLOBAL:
            return short_instruction("OP_DEFINE_GLOBAL", offset);
        case OpCode::OP_HALT:
            return simple_instruction("OP_HALT", offset);
        default:
            std::cout << "Unknown opcode " << static_cast<int>(instruction) << "\n";
            return offset + 1;
    }
}

size_t Assembly::simple_instruction(const char* name, size_t offset) const {
    std::cout << name << "\n";
    return offset + 1;
}

size_t Assembly::constant_instruction(const char* name, size_t offset) const {
    const auto& code_view = bytecode();
    const uint16_t constant = static_cast<uint16_t>((code_view[offset + 1] << 8) | code_view[offset + 2]);
    std::cout << std::setw(16) << std::left << name << " " 
              << std::setw(4) << constant << " '";
    
    const auto& pool = constant_pool();
    if (constant < pool.size()) {
        const Value& val = pool[constant];
        if (val.is_int()) {
            std::cout << val.as_int();
        } else if (val.is_float()) {
            std::cout << val.as_float();
        } else if (val.is_bool()) {
            std::cout << (val.as_bool() ? "true" : "false");
        } else if (val.is_null()) {
            std::cout << "nil";
        } else {
            std::cout << "<value>";
        }
    }
    
    std::cout << "'\n";
    return offset + 3;
}

size_t Assembly::string_instruction(const char* name, size_t offset) const {
    const auto& code_view = bytecode();
    uint16_t str_idx = (code_view[offset + 1] << 8) | code_view[offset + 2];
    std::cout << std::setw(16) << std::left << name << " " 
              << std::setw(4) << str_idx << " '";
    
    if (str_idx < strings.size()) {
        std::cout << strings[str_idx];
    }
    
    std::cout << "'\n";
    return offset + 3;
}

size_t Assembly::short_instruction(const char* name, size_t offset) const {
    const auto& code_view = bytecode();
    uint16_t value = (code_view[offset + 1] << 8) | code_view[offset + 2];
    std::cout << std::setw(16) << std::left << name << " " << value << "\n";
    return offset + 3;
}

size_t Assembly::jump_instruction(const char* name, int sign, size_t offset) const {
    const auto& code_view = bytecode();
    uint16_t jump = (code_view[offset + 1] << 8) | code_view[offset + 2];
    std::cout << std::setw(16) << std::left << name << " " 
              << std::setw(4) << offset << " -> " 
              << (offset + 3 + sign * jump) << "\n";
    return offset + 3;
}

size_t Assembly::property_instruction(const char* name, size_t offset) const {
    const auto& code_view = bytecode();
    uint16_t str_idx = (code_view[offset + 1] << 8) | code_view[offset + 2];
    uint8_t flags = code_view[offset + 3];
    bool is_let = (flags & 0x1) != 0;
    std::cout << std::setw(16) << std::left << name << " "
              << std::setw(4) << str_idx << " ("
              << (is_let ? "let" : "var") << ")\n";
    return offset + 4;
}

void Assembly::serialize(std::ostream& out) const
{
    // Header
    AssemblyFileHeader h{};
    h.magic = kMagicSSAS;
    h.verMajor = kVerMajor;
    h.verMinor = kVerMinor;
    WritePOD(out, h);

    // code / lines
    WriteVectorPOD(out, code);
    WriteVectorPOD(out, lines);

    // constants
    {
        uint32_t n = (uint32_t)constants.size();
        WritePOD(out, n);
        for (const Value& v : constants)
        {
            // ===== 반드시 구현 필요 =====
            // Value가 스스로 직렬화/역직렬화를 제공하도록
            v.serialize(out);
        }
    }

    // strings
    {
        uint32_t n = (uint32_t)strings.size();
        WritePOD(out, n);
        for (const auto& s : strings) WriteString(out, s);
    }

    // functions
    {
        uint32_t n = (uint32_t)functions.size();
        WritePOD(out, n);

        for (const auto& fn : functions)
        {
            WriteString(out, fn.name);

            // params / labels
            WritePOD(out, (uint32_t)fn.params.size());
            for (auto& p : fn.params) WriteString(out, p);

            WritePOD(out, (uint32_t)fn.param_labels.size());
            for (auto& p : fn.param_labels) WriteString(out, p);

            // defaults
            WritePOD(out, (uint32_t)fn.param_defaults.size());
            for (auto& d : fn.param_defaults)
            {
                WritePOD(out, (uint8_t)(d.has_default ? 1 : 0));
                if (d.has_default)
                {
                    d.value.serialize(out);

                    WritePOD(out, (uint8_t)(d.string_value.has_value() ? 1 : 0));
                    if (d.string_value) WriteString(out, *d.string_value);
                }
            }

            // upvalues
            WritePOD(out, (uint32_t)fn.upvalues.size());
            for (auto& u : fn.upvalues)
            {
                WritePOD(out, u.index);
                WritePOD(out, (uint8_t)(u.is_local ? 1 : 0));
            }

            WritePOD(out, (uint8_t)(fn.is_initializer ? 1 : 0));
            WritePOD(out, (uint8_t)(fn.is_override ? 1 : 0));

            // nested chunk
            WritePOD(out, (uint8_t)(fn.chunk ? 1 : 0));
            if (fn.chunk) fn.chunk->serialize(out);
        }
    }

    // protocols
    {
        uint32_t n = (uint32_t)protocols.size();
        WritePOD(out, n);

        for (const auto& pr : protocols)
        {
            WritePOD(out, (uint8_t)(pr ? 1 : 0));
            if (!pr) continue;

            WriteString(out, pr->name);

            // method req
            WritePOD(out, (uint32_t)pr->method_requirements.size());
            for (auto& m : pr->method_requirements)
            {
                WriteString(out, m.name);
                WritePOD(out, (uint32_t)m.param_names.size());
                for (auto& pn : m.param_names) WriteString(out, pn);
                WritePOD(out, (uint8_t)(m.is_mutating ? 1 : 0));
            }

            // property req
            WritePOD(out, (uint32_t)pr->property_requirements.size());
            for (auto& p : pr->property_requirements)
            {
                WriteString(out, p.name);
                WritePOD(out, (uint8_t)(p.has_getter ? 1 : 0));
                WritePOD(out, (uint8_t)(p.has_setter ? 1 : 0));
            }

            // inherited
            WritePOD(out, (uint32_t)pr->inherited_protocols.size());
            for (auto& ip : pr->inherited_protocols) WriteString(out, ip);
        }
    }

    if (kVerMinor >= 1)
    {
        WriteString(out, manifest.name);
        WritePOD(out, manifest.version_major);
        WritePOD(out, manifest.version_minor);

        {
            uint32_t n = (uint32_t)type_definitions.size();
            WritePOD(out, n);
            for (const auto& t : type_definitions)
            {
                WritePOD(out, t.name);
                WritePOD(out, t.namespace_name);
                WritePOD(out, t.flags);
                WritePOD(out, t.base_type);
                WritePOD(out, t.method_list.start);
                WritePOD(out, t.method_list.count);
                WritePOD(out, t.field_list.start);
                WritePOD(out, t.field_list.count);
                WriteVectorPOD(out, t.interfaces);
            }
        }

        {
            uint32_t n = (uint32_t)method_definitions.size();
            WritePOD(out, n);
            for (const auto& m : method_definitions)
            {
                WritePOD(out, m.name);
                WritePOD(out, m.flags);
                WritePOD(out, m.signature);
                WritePOD(out, m.body_ptr);
            }
        }

        {
            uint32_t n = (uint32_t)field_definitions.size();
            WritePOD(out, n);
            for (const auto& f : field_definitions)
            {
                WritePOD(out, f.name);
                WritePOD(out, f.flags);
                WritePOD(out, f.type);
            }
        }

        {
            uint32_t n = (uint32_t)property_definitions.size();
            WritePOD(out, n);
            for (const auto& p : property_definitions)
            {
                WritePOD(out, p.name);
                WritePOD(out, p.flags);
                WritePOD(out, p.type);
                WritePOD(out, p.getter);
                WritePOD(out, p.setter);
            }
        }

        {
            const auto& constants_for_pool = global_constant_pool.empty() ? constants : global_constant_pool;
            uint32_t n = (uint32_t)constants_for_pool.size();
            WritePOD(out, n);
            for (const Value& v : constants_for_pool)
            {
                v.serialize(out);
            }
        }

        WriteVectorPOD(out, signature_blob);

        {
            std::vector<MethodBody> bodies = method_bodies;
            if (bodies.empty())
            {
                MethodBody body{};
                body.bytecode = code;
                body.line_info = lines;
                bodies.push_back(std::move(body));
            }

            uint32_t n = (uint32_t)bodies.size();
            WritePOD(out, n);
            for (const auto& b : bodies)
            {
                WriteVectorPOD(out, b.bytecode);
                WriteVectorPOD(out, b.line_info);
                WritePOD(out, b.max_stack_depth);
            }
        }
    }
}

Assembly Assembly::deserialize(std::istream& in)
{
    // Header validate
    auto h = ReadPOD<AssemblyFileHeader>(in);
    if (h.magic != kMagicSSAS)
        throw std::runtime_error("Assembly::deserialize bad magic");
    if (h.verMajor != kVerMajor)
        throw std::runtime_error("Assembly::deserialize version mismatch");
    if (h.verMinor > kVerMinor)
        throw std::runtime_error("Assembly::deserialize unsupported version");

    Assembly c{};
    c.code = ReadVectorPOD<uint8_t>(in);
    c.lines = ReadVectorPOD<uint32_t>(in);

    // constants
    {
        uint32_t n = ReadPOD<uint32_t>(in);
        c.constants.reserve(n);
        for (uint32_t i = 0; i < n; ++i)
        {
            // ===== 반드시 구현 필요 =====
            c.constants.push_back(Value::deserialize(in));
        }
    }

    // strings
    {
        uint32_t n = ReadPOD<uint32_t>(in);
        c.strings.reserve(n);
        for (uint32_t i = 0; i < n; ++i) c.strings.push_back(ReadString(in));
    }

    // functions
    {
        uint32_t n = ReadPOD<uint32_t>(in);
        c.functions.reserve(n);

        for (uint32_t i = 0; i < n; ++i)
        {
            FunctionPrototype fn{};
            fn.name = ReadString(in);

            uint32_t pn = ReadPOD<uint32_t>(in);
            fn.params.reserve(pn);
            for (uint32_t k = 0; k < pn; ++k) fn.params.push_back(ReadString(in));

            uint32_t ln = ReadPOD<uint32_t>(in);
            fn.param_labels.reserve(ln);
            for (uint32_t k = 0; k < ln; ++k) fn.param_labels.push_back(ReadString(in));

            uint32_t dn = ReadPOD<uint32_t>(in);
            fn.param_defaults.resize(dn);
            for (uint32_t k = 0; k < dn; ++k)
            {
                auto& d = fn.param_defaults[k];
                d.has_default = ReadPOD<uint8_t>(in) != 0;
                if (d.has_default)
                {
                    d.value = Value::deserialize(in);

                    bool hasStr = ReadPOD<uint8_t>(in) != 0;
                    if (hasStr) d.string_value = ReadString(in);
                }
            }

            uint32_t uvn = ReadPOD<uint32_t>(in);
            fn.upvalues.reserve(uvn);
            for (uint32_t k = 0; k < uvn; ++k)
            {
                UpvalueInfo u{};
                u.index = ReadPOD<uint16_t>(in);
                u.is_local = ReadPOD<uint8_t>(in) != 0;
                fn.upvalues.push_back(u);
            }

            fn.is_initializer = ReadPOD<uint8_t>(in) != 0;
            fn.is_override = ReadPOD<uint8_t>(in) != 0;

            bool hasAssembly = ReadPOD<uint8_t>(in) != 0;
            if (hasAssembly)
            {
                fn.chunk = std::make_shared<Assembly>(Assembly::deserialize(in));
            }

            c.functions.push_back(std::move(fn));
        }
    }

    // protocols
    {
        uint32_t n = ReadPOD<uint32_t>(in);
        c.protocols.reserve(n);

        for (uint32_t i = 0; i < n; ++i)
        {
            bool has = ReadPOD<uint8_t>(in) != 0;
            if (!has) { c.protocols.push_back(nullptr); continue; }

            auto pr = std::make_shared<Protocol>();
            pr->name = ReadString(in);

            uint32_t mn = ReadPOD<uint32_t>(in);
            pr->method_requirements.reserve(mn);
            for (uint32_t k = 0; k < mn; ++k)
            {
                ProtocolMethodReq m{};
                m.name = ReadString(in);
                uint32_t pnn = ReadPOD<uint32_t>(in);
                m.param_names.reserve(pnn);
                for (uint32_t j = 0; j < pnn; ++j) m.param_names.push_back(ReadString(in));
                m.is_mutating = ReadPOD<uint8_t>(in) != 0;
                pr->method_requirements.push_back(std::move(m));
            }

            uint32_t pn = ReadPOD<uint32_t>(in);
            pr->property_requirements.reserve(pn);
            for (uint32_t k = 0; k < pn; ++k)
            {
                ProtocolPropertyReq p{};
                p.name = ReadString(in);
                p.has_getter = ReadPOD<uint8_t>(in) != 0;
                p.has_setter = ReadPOD<uint8_t>(in) != 0;
                pr->property_requirements.push_back(std::move(p));
            }

            uint32_t inh = ReadPOD<uint32_t>(in);
            pr->inherited_protocols.reserve(inh);
            for (uint32_t k = 0; k < inh; ++k) pr->inherited_protocols.push_back(ReadString(in));

            c.protocols.push_back(std::move(pr));
        }
    }

    if (h.verMinor >= 1)
    {
        c.manifest.name = ReadString(in);
        c.manifest.version_major = ReadPOD<uint16_t>(in);
        c.manifest.version_minor = ReadPOD<uint16_t>(in);

        {
            uint32_t n = ReadPOD<uint32_t>(in);
            c.type_definitions.reserve(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                TypeDef t{};
                t.name = ReadPOD<string_idx>(in);
                t.namespace_name = ReadPOD<string_idx>(in);
                t.flags = ReadPOD<uint32_t>(in);
                t.base_type = ReadPOD<type_idx>(in);
                t.method_list.start = ReadPOD<uint32_t>(in);
                t.method_list.count = ReadPOD<uint32_t>(in);
                t.field_list.start = ReadPOD<uint32_t>(in);
                t.field_list.count = ReadPOD<uint32_t>(in);
                t.interfaces = ReadVectorPOD<type_idx>(in);
                c.type_definitions.push_back(std::move(t));
            }
        }

        {
            uint32_t n = ReadPOD<uint32_t>(in);
            c.method_definitions.reserve(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                MethodDef m{};
                m.name = ReadPOD<string_idx>(in);
                m.flags = ReadPOD<uint32_t>(in);
                m.signature = ReadPOD<signature_idx>(in);
                m.body_ptr = ReadPOD<body_idx>(in);
                c.method_definitions.push_back(std::move(m));
            }
        }

        {
            uint32_t n = ReadPOD<uint32_t>(in);
            c.field_definitions.reserve(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                FieldDef f{};
                f.name = ReadPOD<string_idx>(in);
                f.flags = ReadPOD<uint32_t>(in);
                f.type = ReadPOD<type_idx>(in);
                c.field_definitions.push_back(std::move(f));
            }
        }

        {
            uint32_t n = ReadPOD<uint32_t>(in);
            c.property_definitions.reserve(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                PropertyDef p{};
                p.name = ReadPOD<string_idx>(in);
                p.flags = ReadPOD<uint32_t>(in);
                p.type = ReadPOD<type_idx>(in);
                p.getter = ReadPOD<method_idx>(in);
                p.setter = ReadPOD<method_idx>(in);
                c.property_definitions.push_back(std::move(p));
            }
        }

        {
            uint32_t n = ReadPOD<uint32_t>(in);
            c.global_constant_pool.reserve(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                c.global_constant_pool.push_back(Value::deserialize(in));
            }
        }

        c.signature_blob = ReadVectorPOD<uint8_t>(in);

        {
            uint32_t n = ReadPOD<uint32_t>(in);
            c.method_bodies.reserve(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                MethodBody body{};
                body.bytecode = ReadVectorPOD<uint8_t>(in);
                body.line_info = ReadVectorPOD<uint32_t>(in);
                body.max_stack_depth = ReadPOD<uint32_t>(in);
                c.method_bodies.push_back(std::move(body));
            }
        }

        if (c.global_constant_pool.empty())
        {
            c.global_constant_pool = c.constants;
        }

        if (c.method_bodies.empty())
        {
            MethodBody body{};
            body.bytecode = c.code;
            body.line_info = c.lines;
            c.method_bodies.push_back(std::move(body));
        }
    }

    return c;
}

void Assembly::expand_to_assembly() {
    if (manifest.name.empty()) {
        manifest.name = "Main";
    }

    if (global_constant_pool.empty() && !constants.empty()) {
        global_constant_pool = constants;
    }

    if (method_bodies.empty()) {
        MethodBody body{};
        body.bytecode = code;
        body.line_info = lines;
        method_bodies.push_back(std::move(body));
    }

    if (method_definitions.empty()) {
        MethodDef entry{};
        entry.name = static_cast<string_idx>(add_string(manifest.name));
        entry.flags = static_cast<uint32_t>(MethodFlags::Static);
        entry.signature = 0;
        entry.body_ptr = 0;
        method_definitions.push_back(entry);
    }
}


} // namespace swiftscript
