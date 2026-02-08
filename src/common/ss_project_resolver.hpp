// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ss_project_resolver.hpp
 * @brief Module resolver for project imports.
 *
 * Implements IModuleResolver to resolve import statements
 * by searching configured import root directories.
 */

#pragma once
#include "ss_compiler.hpp"

namespace swive {

class ProjectModuleResolver : public IModuleResolver {
public:
    explicit ProjectModuleResolver(std::vector<std::filesystem::path> import_roots)
        : roots_(std::move(import_roots)) {}

    bool ResolveAndLoad(const std::string& module_name,
                        std::string& out_full_path,
                        std::string& out_source,
                        std::string& out_error) override;

private:
    std::vector<std::filesystem::path> roots_;

    // module_name -> resolved path string
    std::unordered_map<std::string, std::string> resolve_cache_;
    // resolved full_path -> source text
    std::unordered_map<std::string, std::string> source_cache_;

    static bool ReadAllText(const std::filesystem::path& p, std::string& out, std::string& err);
};

} // namespace swive
