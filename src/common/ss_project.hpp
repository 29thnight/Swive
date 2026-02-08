// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ss_project.hpp
 * @brief Project file (.ssproject) structure.
 *
 * Defines SSProject struct containing project metadata:
 * entry file, import roots, and project directory paths.
 */

#pragma once

namespace swive {

struct SSProject {
    std::filesystem::path project_file;
    std::filesystem::path project_dir;

    std::filesystem::path entry_file;                 // e.g. Scripts/main.ss
    std::vector<std::filesystem::path> import_roots;  // e.g. Scripts, Libs
};

bool LoadSSProject(const std::filesystem::path& ssproject, SSProject& out, std::string& err);

} // namespace swive
