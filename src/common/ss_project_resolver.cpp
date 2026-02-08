// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ss_project_resolver.cpp
 * @brief Module resolver implementation.
 *
 * Implements ProjectModuleResolver::ResolveAndLoad() to find
 * and load imported modules from project import roots.
 */

#include "pch.h"
#include "ss_project_resolver.hpp"

namespace swive {

bool ProjectModuleResolver::ReadAllText(const std::filesystem::path& p, std::string& out, std::string& err) {
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) {
        err = "cannot open file: " + p.string();
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

bool ProjectModuleResolver::ResolveAndLoad(const std::string& module_name,
                                           std::string& out_full_path,
                                           std::string& out_source,
                                           std::string& out_error)
{
    // 1) resolve cache hit
    if (auto it = resolve_cache_.find(module_name); it != resolve_cache_.end()) {
        out_full_path = it->second;

        // source cache hit
        if (auto sit = source_cache_.find(out_full_path); sit != source_cache_.end()) {
            out_source = sit->second;
            return true;
        }

        // load source
        std::string err;
        if (!ReadAllText(out_full_path, out_source, err)) {
            out_error = err;
            return false;
        }
        source_cache_[out_full_path] = out_source;
        return true;
    }

    // 2) search in roots
    const std::filesystem::path rel = std::filesystem::path(module_name); // already "foo/bar" normalized

    for (const auto& root : roots_) {
        // (a) root/<module>.ss
        {
            auto cand = root / rel;
            cand += ".ss";
            if (std::filesystem::exists(cand)) {
                out_full_path = cand.string();
                resolve_cache_[module_name] = out_full_path;

                std::string err;
                if (!ReadAllText(cand, out_source, err)) {
                    out_error = err;
                    return false;
                }
                source_cache_[out_full_path] = out_source;
                return true;
            }
        }
        // (b) root/<module>/index.ss
        {
            auto cand = root / rel / "index.ss";
            if (std::filesystem::exists(cand)) {
                out_full_path = cand.string();
                resolve_cache_[module_name] = out_full_path;

                std::string err;
                if (!ReadAllText(cand, out_source, err)) {
                    out_error = err;
                    return false;
                }
                source_cache_[out_full_path] = out_source;
                return true;
            }
        }
    }

    out_error = "module not found in ImportRoots: " + module_name;
    return false;
}

} // namespace swive
