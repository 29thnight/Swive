#include "ss_chunk.hpp"
#include <iostream>
#include <iomanip>

namespace swiftscript {

void Chunk::write(uint8_t byte, uint32_t line) {
    code.push_back(byte);
    lines.push_back(line);
}

void Chunk::write_op(OpCode op, uint32_t line) {
    write(static_cast<uint8_t>(op), line);
}

size_t Chunk::add_constant(Value value) {
    constants.push_back(value);
    return constants.size() - 1;
}

size_t Chunk::add_string(const std::string& str) {
    // �̹� �����ϴ� ���ڿ����� Ȯ��
    for (size_t i = 0; i < strings.size(); ++i) {
        if (strings[i] == str) {
            return i;
        }
    }
    strings.push_back(str);
    return strings.size() - 1;
}

size_t Chunk::add_function(FunctionPrototype proto) {
    functions.push_back(std::move(proto));
    return functions.size() - 1;
}

size_t Chunk::add_protocol(std::shared_ptr<Protocol> protocol) {
    protocols.push_back(std::move(protocol));
    return protocols.size() - 1;
}

size_t Chunk::emit_jump(OpCode op, uint32_t line) {
    write_op(op, line);
    // �÷��̽�Ȧ���� 0xFFFF �ۼ�
    write(0xFF, line);
    write(0xFF, line);
    return code.size() - 2;
}

void Chunk::patch_jump(size_t offset) {
    // ���� ���ɾ� ���ĺ��� ���� ��ġ������ �Ÿ� ���
    size_t jump = code.size() - offset - 2;
    
    if (jump > 0xFFFF) {
        throw std::runtime_error("Too much code to jump over");
    }
    
    code[offset] = (jump >> 8) & 0xFF;
    code[offset + 1] = jump & 0xFF;
}

void Chunk::disassemble(const std::string& name) const {
    std::cout << "== " << name << " ==\n";
    for (size_t offset = 0; offset < code.size();) {
        offset = disassemble_instruction(offset);
    }
}

size_t Chunk::disassemble_instruction(size_t offset) const {
    std::cout << std::setw(4) << std::setfill('0') << offset << " ";
    
    if (offset > 0 && lines[offset] == lines[offset - 1]) {
        std::cout << "   | ";
    } else {
        std::cout << std::setw(4) << lines[offset] << " ";
    }
    
    OpCode instruction = static_cast<OpCode>(code[offset]);
    
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
            uint16_t arg_count = (code[offset + 1] << 8) | code[offset + 2];
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
        case OpCode::OP_PRINT:
            return simple_instruction("OP_PRINT", offset);
        case OpCode::OP_STRUCT:
            return string_instruction("OP_STRUCT", offset);
        case OpCode::OP_STRUCT_METHOD: {
            uint16_t str_idx = (code[offset + 1] << 8) | code[offset + 2];
            uint8_t is_mutating = code[offset + 3];
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
            uint16_t str_idx = (code[offset + 1] << 8) | code[offset + 2];
            uint8_t assoc_count = code[offset + 3];
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

size_t Chunk::simple_instruction(const char* name, size_t offset) const {
    std::cout << name << "\n";
    return offset + 1;
}

size_t Chunk::constant_instruction(const char* name, size_t offset) const {
    uint16_t constant = (code[offset + 1] << 8) | code[offset + 2];
    std::cout << std::setw(16) << std::left << name << " " 
              << std::setw(4) << constant << " '";
    
    if (constant < constants.size()) {
        const Value& val = constants[constant];
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

size_t Chunk::string_instruction(const char* name, size_t offset) const {
    uint16_t str_idx = (code[offset + 1] << 8) | code[offset + 2];
    std::cout << std::setw(16) << std::left << name << " " 
              << std::setw(4) << str_idx << " '";
    
    if (str_idx < strings.size()) {
        std::cout << strings[str_idx];
    }
    
    std::cout << "'\n";
    return offset + 3;
}

size_t Chunk::short_instruction(const char* name, size_t offset) const {
    uint16_t value = (code[offset + 1] << 8) | code[offset + 2];
    std::cout << std::setw(16) << std::left << name << " " << value << "\n";
    return offset + 3;
}

size_t Chunk::jump_instruction(const char* name, int sign, size_t offset) const {
    uint16_t jump = (code[offset + 1] << 8) | code[offset + 2];
    std::cout << std::setw(16) << std::left << name << " " 
              << std::setw(4) << offset << " -> " 
              << (offset + 3 + sign * jump) << "\n";
    return offset + 3;
}

size_t Chunk::property_instruction(const char* name, size_t offset) const {
    uint16_t str_idx = (code[offset + 1] << 8) | code[offset + 2];
    uint8_t flags = code[offset + 3];
    bool is_let = (flags & 0x1) != 0;
    std::cout << std::setw(16) << std::left << name << " "
              << std::setw(4) << str_idx << " ("
              << (is_let ? "let" : "var") << ")\n";
    return offset + 4;
}

} // namespace swiftscript
