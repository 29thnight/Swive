// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file lsp_utils.hpp
 * @brief LSP utility functions.
 *
 * URI encoding/decoding, path conversion utilities for
 * Language Server Protocol file handling.
 */

#pragma once
#include <string>
#include <filesystem>

namespace lsp {

    inline int hexval(char c) {
        if ('0' <= c && c <= '9') return c - '0';
        if ('a' <= c && c <= 'f') return 10 + (c - 'a');
        if ('A' <= c && c <= 'F') return 10 + (c - 'A');
        return -1;
    }

    inline std::string uri_decode(std::string s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size()) {
                int a = hexval(s[i + 1]);
                int b = hexval(s[i + 2]);
                if (a >= 0 && b >= 0) {
                    out.push_back(static_cast<char>((a << 4) | b));
                    i += 2;
                    continue;
                }
            }
            out.push_back(s[i]);
        }
        return out;
    }

    // file:///C:/path/to/file.ss  ->  C:\path\to\file.ss  (on Windows)
    // file:///home/u/a.ss -> /home/u/a.ss (on posix)
    inline std::filesystem::path UriToPath(const std::string& uri) {
        const std::string prefix = "file://";
        if (uri.rfind(prefix, 0) != 0) {
            return std::filesystem::path(uri);
        }

        std::string rest = uri.substr(prefix.size()); // could start with /C:/...
        // Some clients use file:///C:/..., so rest begins with ///C:/...
        // normalize: strip leading slashes but keep one for posix absolute
        // For Windows we want "C:/..."
        while (rest.size() >= 3 && rest[0] == '/' && rest[1] == '/' && rest[2] == '/') {
            rest.erase(rest.begin(), rest.begin() + 2); // "///C:/" -> "/C:/"
            break;
        }

        // decode percent-encoding
        rest = uri_decode(rest);

#ifdef _WIN32
        // Windows: "/C:/x/y" -> "C:/x/y"
        if (!rest.empty() && rest[0] == '/' && rest.size() >= 3 && std::isalpha((unsigned char)rest[1]) && rest[2] == ':') {
            rest.erase(rest.begin());
        }
        // convert forward slashes
        for (auto& ch : rest) if (ch == '/') ch = '\\';
#endif

        return std::filesystem::path(rest);
    }

    inline std::string PathToUri(const std::filesystem::path& p) {
        // MVP: just file URI without percent-encoding (works for ASCII paths)
        std::string s = p.lexically_normal().string();
#ifdef _WIN32
        // convert backslashes
        for (auto& ch : s) if (ch == '\\') ch = '/';
        // C:/... -> file:///C:/...
        return std::string("file:///") + s;
#else
        return std::string("file://") + s;
#endif
    }

} // namespace lsp
