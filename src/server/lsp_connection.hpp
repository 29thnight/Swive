// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file lsp_connection.hpp
 * @brief JSON-RPC connection handler.
 *
 * Manages stdin/stdout communication using LSP's Content-Length
 * framing protocol for JSON-RPC messages.
 */

#pragma once
#include <string>
#include <functional>

class JsonRpcConnection {
public:
    /*using OnMessageFn = std::function<void(const std::string& json)>;*/
    using OnMessageFn = std::function<void(const std::string& json, JsonRpcConnection& self)>;

    explicit JsonRpcConnection(OnMessageFn onMessage) : onMessage_(std::move(onMessage)) {}

    void Run();                      // blocking loop
    void Send(const std::string& json); // send raw json (already serialized)

private:
    OnMessageFn onMessage_;

    bool ReadMessage(std::string& outJson);
    static bool ReadLine(std::string& outLine);
    static bool ReadBytes(size_t n, std::string& out);
};
