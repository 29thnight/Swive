// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file module_resolver.hpp
 * @brief LSP module resolver.
 *
 * Resolves import paths for the LSP analyzer, searching
 * configured import roots to find source files.
 */

#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

class ProjectModuleResolver {
public:
    explicit ProjectModuleResolver(std::vector<std::filesystem::path> roots = {})
        : roots_(std::move(roots)) {
    }

    void SetRoots(std::vector<std::filesystem::path> roots) {
        roots_ = std::move(roots);
        resolve_cache_.clear();
    }

    // moduleName: "math" or "foo/bar" (or "foo.bar" -> we normalize)
    bool Resolve(const std::string& moduleName, std::filesystem::path& outPath, std::string& outError);

private:
    std::vector<std::filesystem::path> roots_;
    std::unordered_map<std::string, std::filesystem::path> resolve_cache_;

    static std::string NormalizeModuleName(std::string s);
};
