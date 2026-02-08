// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ssproject_loader.cpp
 * @brief SSProject loader implementation.
 *
 * Parses .ssproject XML files and resolves entry points
 * and import root paths for the language server.
 */

#include "pch.h"
#include "ssproject_loader.hpp"
#include <fstream>
#include <string>
#include <vector>

static bool ReadAllText(const std::filesystem::path& p, std::string& out, std::string& err) {
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) { err = "cannot open: " + p.string(); return false; }
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

static bool ExtractTag(const std::string& xml, const std::string& tag, std::string& out) {
    const std::string open = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    auto a = xml.find(open);
    if (a == std::string::npos) return false;
    a += open.size();
    auto b = xml.find(close, a);
    if (b == std::string::npos) return false;
    out = xml.substr(a, b - a);
    return true;
}

static void ExtractRepeatedTags(const std::string& xml, const std::string& tag, std::vector<std::string>& out) {
    const std::string open = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    size_t pos = 0;
    while (true) {
        auto a = xml.find(open, pos);
        if (a == std::string::npos) break;
        a += open.size();
        auto b = xml.find(close, a);
        if (b == std::string::npos) break;
        out.push_back(xml.substr(a, b - a));
        pos = b + close.size();
    }
}

bool FindFirstSSProject(const std::filesystem::path& rootDir, std::filesystem::path& outProject) {
    if (!std::filesystem::exists(rootDir)) return false;

    for (auto it = std::filesystem::recursive_directory_iterator(rootDir);
        it != std::filesystem::recursive_directory_iterator();
        ++it) {
        if (!it->is_regular_file()) continue;
        if (it->path().extension() == ".ssproject") {
            outProject = it->path();
            return true;
        }
    }
    return false;
}

bool LoadSSProjectInfo(const std::filesystem::path& ssproject, SSProjectInfo& out, std::string& err) {
    std::string xml;
    if (!ReadAllText(ssproject, xml, err)) return false;

    out.project_file = ssproject;
    out.project_dir = ssproject.parent_path();

    std::string entry;
    if (!ExtractTag(xml, "Entry", entry)) {
        err = "missing <Entry>...</Entry>";
        return false;
    }
    out.entry_file = (out.project_dir / std::filesystem::path(entry)).lexically_normal();

    out.import_roots.clear();
    std::string rootsBlock;
    if (ExtractTag(xml, "ImportRoots", rootsBlock)) {
        std::vector<std::string> roots;
        ExtractRepeatedTags(rootsBlock, "Root", roots);
        for (auto& r : roots) {
            out.import_roots.push_back((out.project_dir / std::filesystem::path(r)).lexically_normal());
        }
    }
    else {
        // default: project_dir
        out.import_roots.push_back(out.project_dir);
    }

    return true;
}
