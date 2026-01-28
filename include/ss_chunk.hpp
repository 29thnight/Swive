#pragma once

#include "ss_value.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

namespace swiftscript {

struct Chunk;

struct FunctionPrototype {
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<Chunk> chunk;
};

// Opcodes
enum class OpCode : uint8_t {
    // Constants & stack
    OP_CONSTANT,
    OP_STRING,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,

    // Arithmetic
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,
    OP_BITWISE_NOT,

    // Comparison
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_GREATER,
    OP_LESS_EQUAL,
    OP_GREATER_EQUAL,

    // Logic
    OP_NOT,
    OP_AND,
    OP_OR,

    // Variables
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL,

    // Control flow
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,

    // Functions
    OP_FUNCTION,
    OP_CALL,
    OP_RETURN,

    // Optional-specific
    OP_UNWRAP,
    OP_JUMP_IF_NIL,
    OP_NIL_COALESCE,
    OP_OPTIONAL_CHAIN,

    // Objects
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,

    // I/O
    OP_PRINT,

    // End
    OP_HALT,
};

// Chunk = compiled bytecode for one function/script
struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<std::string> strings;
    std::vector<FunctionPrototype> functions;
    std::vector<uint32_t> lines;

    size_t add_constant(Value val) {
        constants.push_back(val);
        return constants.size() - 1;
    }

    size_t add_string(const std::string& val) {
        for (size_t i = 0; i < strings.size(); ++i) {
            if (strings[i] == val) {
                return i;
            }
        }
        strings.push_back(val);
        return strings.size() - 1;
    }

    size_t add_function(FunctionPrototype proto) {
        functions.push_back(std::move(proto));
        return functions.size() - 1;
    }

    void write(uint8_t byte, uint32_t line) {
        code.push_back(byte);
        lines.push_back(line);
    }

    void write_op(OpCode op, uint32_t line) {
        write(static_cast<uint8_t>(op), line);
    }

    size_t emit_jump(OpCode op, uint32_t line) {
        write_op(op, line);
        write(0xff, line);
        write(0xff, line);
        return code.size() - 2;
    }

    void patch_jump(size_t offset) {
        size_t jump = code.size() - offset - 2;
        if (jump > UINT16_MAX) {
            throw std::runtime_error("Jump offset too large");
        }
        code[offset] = static_cast<uint8_t>((jump >> 8) & 0xff);
        code[offset + 1] = static_cast<uint8_t>(jump & 0xff);
    }
};

} // namespace swiftscript
