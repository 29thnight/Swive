// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file lsp_connection.cpp
 * @brief JSON-RPC connection implementation.
 *
 * Implements message reading/writing with Content-Length headers
 * for LSP communication over stdin/stdout.
 */

#include "pch.h"
#include "lsp_connection.hpp"
#include <iostream>
#include <sstream>

void JsonRpcConnection::Run() {
    std::string json;
    while (ReadMessage(json)) {
        onMessage_(json, *this);
    }
}

void JsonRpcConnection::Send(const std::string& json) {
    std::ostringstream oss;
    oss << "Content-Length: " << json.size() << "\r\n\r\n" << json;
    std::cout << oss.str();
    std::cout.flush();
}

bool JsonRpcConnection::ReadMessage(std::string& outJson) {
    std::string line;
    size_t contentLen = 0;

    // Read headers
    while (ReadLine(line)) {
        // empty line indicates end of headers
        if (line == "\r\n" || line == "\n" || line.empty()) break;

        const std::string key = "Content-Length:";
        if (line.rfind(key, 0) == 0) {
            // after key might be spaces then number
            std::string rest = line.substr(key.size());
            // trim
            size_t i = 0;
            while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
            contentLen = static_cast<size_t>(std::stoul(rest.substr(i)));
        }
    }

    if (contentLen == 0) return false;
    return ReadBytes(contentLen, outJson);
}

bool JsonRpcConnection::ReadLine(std::string& outLine) {
    outLine.clear();
    if (!std::getline(std::cin, outLine)) return false;
    outLine.push_back('\n'); // normalize
    return true;
}

bool JsonRpcConnection::ReadBytes(size_t n, std::string& out) {
    out.resize(n);
    std::cin.read(out.data(), static_cast<std::streamsize>(n));
    return std::cin.gcount() == static_cast<std::streamsize>(n);
}
