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
        OP_JUMP_IF_NIL,
        OP_LOOP,

        // Functions
        OP_FUNCTION,
        OP_CALL,
        OP_RETURN,

        // Objects & members
        OP_GET_PROPERTY,
        OP_SET_PROPERTY,
        OP_OPTIONAL_CHAIN,

        // Optional handling
        OP_UNWRAP,
        OP_NIL_COALESCE,

        // Range operators
        OP_RANGE_INCLUSIVE,    // ... operator
        OP_RANGE_EXCLUSIVE,    // ..< operator

        // Collection operations
        OP_ARRAY,              // Create array from N elements on stack
        OP_DICT,               // Create dict from N key-value pairs on stack
        OP_GET_SUBSCRIPT,      // array[index] or dict[key]
        OP_SET_SUBSCRIPT,      // array[index] = value or dict[key] = value

        // Misc
        OP_PRINT,
        OP_HALT,
    };

    // Bytecode chunk
    struct Chunk {
        std::vector<uint8_t> code;
        std::vector<uint32_t> lines;
        std::vector<Value> constants;
        std::vector<std::string> strings;
        std::vector<FunctionPrototype> functions;

        void write(uint8_t byte, uint32_t line);
        void write_op(OpCode op, uint32_t line);

        size_t add_constant(Value value);
        size_t add_string(const std::string& str);
        size_t add_function(FunctionPrototype proto);

        size_t emit_jump(OpCode op, uint32_t line);
        void patch_jump(size_t offset);

        void disassemble(const std::string& name) const;
        size_t disassemble_instruction(size_t offset) const;

    private:
        size_t simple_instruction(const char* name, size_t offset) const;
        size_t constant_instruction(const char* name, size_t offset) const;
        size_t string_instruction(const char* name, size_t offset) const;
        size_t short_instruction(const char* name, size_t offset) const;
        size_t jump_instruction(const char* name, int sign, size_t offset) const;
    };

} // namespace swiftscript