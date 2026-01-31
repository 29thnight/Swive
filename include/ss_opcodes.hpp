// OpCode handlers implementation
#pragma once
#include <stdexcept>

namespace swiftscript {

    // Opcodes
    enum class OpCode : uint8_t {
#define X(name) name,
#include "ss_opcodes.def"
#undef X
    };
} // namespace swiftscript
