// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file module_resolver.cpp
 * @brief LSP module resolver implementation.
 *
 * Implements module name normalization and path resolution
 * for import statements in the LSP environment.
 */

#include "pch.h"
#include "module_resolver.hpp"

static bool Exists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

std::string ProjectModuleResolver::NormalizeModuleName(std::string s) {
    // import foo.bar -> foo/bar
    for (auto& ch : s) {
        if (ch == '.') ch = '/';
        if (ch == '\\') ch = '/';
    }
    // strip quotes if someone still uses import "math"
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    // strip extension if provided
    if (s.size() >= 3 && s.substr(s.size() - 3) == ".ss") {
        s.resize(s.size() - 3);
    }
    return s;
}

bool ProjectModuleResolver::Resolve(const std::string& moduleName, std::filesystem::path& outPath, std::string& outError) {
    const std::string key = NormalizeModuleName(moduleName);

    if (auto it = resolve_cache_.find(key); it != resolve_cache_.end()) {
        outPath = it->second;
        return true;
    }

    const std::filesystem::path rel(key);

    for (const auto& root : roots_) {
        // root/<module>.ss
        {
            auto cand = (root / rel);
            cand += ".ss";
            if (Exists(cand)) {
                outPath = cand.lexically_normal();
                resolve_cache_[key] = outPath;
                return true;
            }
        }
        // root/<module>/index.ss
        {
            auto cand = (root / rel / "index.ss");
            if (Exists(cand)) {
                outPath = cand.lexically_normal();
                resolve_cache_[key] = outPath;
                return true;
            }
        }
    }

    outError = "module not found in ImportRoots: " + key;
    return false;
}
