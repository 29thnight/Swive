#pragma once
#include "ss_project.hpp"
#include "ss_project_resolver.hpp"
#include "ss_vm.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_compiler.hpp"

namespace swiftscript {

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

} // namespace swiftscript
