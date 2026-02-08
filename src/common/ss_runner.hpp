// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ss_runner.hpp
 * @brief High-level script execution utilities.
 *
 * Provides SSRunner helper functions to compile and execute
 * SwiftScript source code or project files in a single call.
 */

#pragma once
#include <string>
#include <fstream>
#include "ss_core.hpp"
#include "ss_project.hpp"
#include "ss_project_resolver.hpp"
#include "ss_vm.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_compiler.hpp"

namespace swive {

// interpret() function - compiles and executes source code in one step
inline Value Interpret(VM& vm, const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize_all();
    Parser parser(std::move(tokens));
    auto program = parser.parse();
    Compiler compiler;
    Assembly chunk = compiler.compile(program);
    return vm.execute(chunk);
}

inline Value RunProject(VM& vm, const SSProject& proj) {
    // entry load
    std::string source;
    // ...
    {
         std::ifstream f(proj.entry_file, std::ios::binary);
         if (!f.is_open()) throw std::runtime_error("cannot open entry: " + proj.entry_file.string());
         source.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    // parse
    Lexer lexer(source);
    auto tokens = lexer.tokenize_all();
    Parser parser(std::move(tokens));
    auto program = parser.parse();

    // compile with resolver
    ProjectModuleResolver resolver(proj.import_roots);

    Compiler compiler;
    compiler.set_base_directory(proj.project_dir.string());
    compiler.set_module_resolver(&resolver);

    Assembly chunk = compiler.compile(program);
    return vm.execute(chunk);
}

} // namespace swive
