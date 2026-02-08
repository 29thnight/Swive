// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ss_opcodes.hpp
 * @brief Opcode enumeration and name lookup.
 *
 * Defines OpCode enum using X-macro from ss_opcodes.def.
 * Provides opcode_name() for disassembly and debugging.
 */

#pragma once
#include <stdexcept>

namespace swive {

    // Opcodes
    enum class OpCode : uint8_t {
#define X(name) name,
#include "ss_opcodes.def"
#undef X
    };
} // namespace swive
