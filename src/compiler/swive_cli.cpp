// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file swive_cli.cpp
 * @brief SwiftScript unified command-line interface.
 *
 * Provides build, run, and exec commands for compiling and executing
 * SwiftScript projects. Entry point for the swive executable.
 */

#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <variant>
#include <cstdint>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "ss_vm.hpp"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_project.hpp"
#include "ss_project_resolver.hpp"

using namespace swive;

namespace {

constexpr const char* VERSION = "0.1.0";

void print_usage() {
    std::cerr << R"(
swive - SwiftScript Unified CLI v)" << VERSION << R"(

Usage: swive <command> [options]

Commands:
  build <project.ssproject>   Compile project to .ssasm
      -c, --config <type>     Build configuration (Debug|Release) [default: Debug]
      -o, --output <path>     Output file path [default: bin/<config>/<project>.ssasm]

  run <file.ssasm>            Execute compiled bytecode
      --stats                 Print VM statistics after execution

  exec <project.ssproject>    Compile and run in one step
      -c, --config <type>     Build configuration (Debug|Release) [default: Debug]
      --stats                 Print VM statistics after execution

  version                     Show version information
  help                        Show this help message

Examples:
  swive build MyProject.ssproject
  swive build MyProject.ssproject -c Release
  swive run bin/Debug/MyProject.ssasm
  swive exec MyProject.ssproject -c Debug --stats
)";
}

void print_version() {
    std::cout << "swive version " << VERSION << "\n";
    std::cout << "SwiftScript Unified CLI\n";
}

// ============== Build ==============
int compile_project(const std::filesystem::path& project_path,
                    const std::string& build_type,
                    const std::filesystem::path& output_path) {
    SSProject project;
    std::string err;
    if (!LoadSSProject(project_path, project, err)) {
        std::cerr << "Error: Failed to load project: " << err << "\n";
        return 1;
    }

    // Load entry file
    std::string source;
    {
        std::ifstream f(project.entry_file, std::ios::binary);
        if (!f.is_open()) {
            std::cerr << "Error: Cannot open entry file: " << project.entry_file.string() << "\n";
            return 1;
        }
        source.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    // Parse
    Lexer lexer(source);
    auto tokens = lexer.tokenize_all();
    Parser parser(std::move(tokens));
    auto program = parser.parse();

    // Compile
    ProjectModuleResolver resolver(project.import_roots);
    Compiler compiler;
    compiler.set_base_directory(project.project_dir.string());
    compiler.set_module_resolver(&resolver);

    Assembly chunk = compiler.compile(program);

    // Validate
    for (const auto& v : chunk.constant_pool()) {
        if (v.type() == Value::Type::Object) {
            std::cerr << "Error: Object constant detected in constants pool!\n";
            return 1;
        }
    }

    // Create output directory
    std::filesystem::create_directories(output_path.parent_path());

    // Serialize
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: Cannot open output file: " << output_path.string() << "\n";
        return 1;
    }

    try {
        chunk.serialize(out);
    } catch (const std::exception& e) {
        std::cerr << "Error: Serialization failed: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Build (" << build_type << ") complete: " << output_path.string() << "\n";
    return 0;
}

int cmd_build(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: Missing project file\n";
        std::cerr << "Usage: swive build <project.ssproject> [options]\n";
        return 1;
    }

    std::filesystem::path project_path = argv[0];
    std::string build_type = "Debug";
    std::filesystem::path output_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            build_type = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    if (!std::filesystem::exists(project_path)) {
        std::cerr << "Error: Project file not found: " << project_path.string() << "\n";
        return 1;
    }

    if (project_path.extension() != ".ssproject") {
        std::cerr << "Error: Expected .ssproject file\n";
        return 1;
    }

    if (output_path.empty()) {
        output_path = project_path.parent_path() / "bin" / build_type /
                      project_path.filename().replace_extension(".ssasm");
    }

    try {
        return compile_project(project_path, build_type, output_path);
    } catch (const std::exception& e) {
        std::cerr << "Error: Compilation failed: " << e.what() << "\n";
        return 1;
    }
}

// ============== Run ==============
int run_assembly(const std::filesystem::path& ssasm_path, bool print_stats) {
    std::ifstream in(ssasm_path, std::ios::binary);
    if (!in) {
        std::cerr << "Error: Cannot open file: " << ssasm_path.string() << "\n";
        return 1;
    }

    Assembly chunk = Assembly::deserialize(in);
    VM vm;
    Value result = vm.execute(chunk);

    std::cout << "Result: " << result.to_string() << "\n";

    if (print_stats) {
        std::cout << "\n";
        vm.print_stats();
    }

    return 0;
}

int cmd_run(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: Missing .ssasm file\n";
        std::cerr << "Usage: swive run <file.ssasm> [options]\n";
        return 1;
    }

    std::filesystem::path ssasm_path = argv[0];
    bool print_stats = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stats") {
            print_stats = true;
        }
    }

    if (!std::filesystem::exists(ssasm_path)) {
        std::cerr << "Error: File not found: " << ssasm_path.string() << "\n";
        return 1;
    }

    if (ssasm_path.extension() != ".ssasm") {
        std::cerr << "Error: Expected .ssasm file\n";
        return 1;
    }

    try {
        return run_assembly(ssasm_path, print_stats);
    } catch (const std::exception& e) {
        std::cerr << "Error: Execution failed: " << e.what() << "\n";
        return 1;
    }
}

// ============== Exec (Build + Run) ==============
int cmd_exec(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Error: Missing project file\n";
        std::cerr << "Usage: swive exec <project.ssproject> [options]\n";
        return 1;
    }

    std::filesystem::path project_path = argv[0];
    std::string build_type = "Debug";
    bool print_stats = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            build_type = argv[++i];
        } else if (arg == "--stats") {
            print_stats = true;
        }
    }

    if (!std::filesystem::exists(project_path)) {
        std::cerr << "Error: Project file not found: " << project_path.string() << "\n";
        return 1;
    }

    if (project_path.extension() != ".ssproject") {
        std::cerr << "Error: Expected .ssproject file\n";
        return 1;
    }

    std::filesystem::path output_path = project_path.parent_path() / "bin" / build_type /
                                        project_path.filename().replace_extension(".ssasm");

    try {
        int build_result = compile_project(project_path, build_type, output_path);
        if (build_result != 0) {
            return build_result;
        }

        std::cout << "\n--- Running ---\n";
        return run_assembly(output_path, print_stats);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "build") {
        return cmd_build(argc - 2, argv + 2);
    } else if (command == "run") {
        return cmd_run(argc - 2, argv + 2);
    } else if (command == "exec") {
        return cmd_exec(argc - 2, argv + 2);
    } else if (command == "version" || command == "-v" || command == "--version") {
        print_version();
        return 0;
    } else if (command == "help" || command == "-h" || command == "--help") {
        print_usage();
        return 0;
    } else {
        std::cerr << "Error: Unknown command '" << command << "'\n";
        print_usage();
        return 1;
    }
}
