#pragma once

#include "ss_value.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <stdexcept>

namespace swiftscript {

struct Chunk;

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
    std::shared_ptr<Chunk> chunk;
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

        // Bitwise operations
        OP_BITWISE_NOT,
        OP_BITWISE_AND,
        OP_BITWISE_OR,
        OP_BITWISE_XOR,
        OP_LEFT_SHIFT,
        OP_RIGHT_SHIFT,

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

        // Functions / Classes
        OP_FUNCTION,
        OP_CLOSURE,            // Create closure with upvalues
        OP_CLASS,              // Create class object
        OP_METHOD,             // Attach method to class on stack
        OP_DEFINE_PROPERTY,    // Register stored property metadata on class
        OP_DEFINE_COMPUTED_PROPERTY, // Register computed property with getter/setter
        OP_INHERIT,            // Link subclass to superclass
        OP_CALL,
        OP_CALL_NAMED,
        OP_RETURN,

        // Upvalues (for closures)
        OP_GET_UPVALUE,        // Get captured variable
        OP_SET_UPVALUE,        // Set captured variable
        OP_CLOSE_UPVALUE,      // Close upvalue when variable goes out of scope

        // Objects & members
        OP_GET_PROPERTY,
        OP_SET_PROPERTY,
        OP_SUPER,
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

        // Struct operations
        OP_STRUCT,             // Create struct type object
        OP_STRUCT_METHOD,      // Attach method to struct (with mutating flag)
        OP_COPY_VALUE,         // Deep copy struct instance for value semantics

        // Enum operations
        OP_ENUM,               // Create enum type object
        OP_ENUM_CASE,          // Define enum case
        OP_MATCH_ENUM_CASE,    // Check enum case name against a value
        OP_GET_ASSOCIATED,     // Get associated value by index

        // Protocol operations
        OP_PROTOCOL,           // Create protocol object
        OP_DEFINE_GLOBAL,      // Define global variable

        // Type operations
        OP_TYPE_CHECK,         // is operator: check if value is of type
        OP_TYPE_CAST,          // as operator: cast value to type
        OP_TYPE_CAST_OPTIONAL, // as? operator: optional cast
        OP_TYPE_CAST_FORCED,   // as! operator: forced cast

        // Error handling
        OP_THROW,              // throw error

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

    private:
        size_t simple_instruction(const char* name, size_t offset) const;
        size_t constant_instruction(const char* name, size_t offset) const;
        size_t string_instruction(const char* name, size_t offset) const;
        size_t short_instruction(const char* name, size_t offset) const;
        size_t jump_instruction(const char* name, int sign, size_t offset) const;
        size_t property_instruction(const char* name, size_t offset) const;
    };

} // namespace swiftscript
