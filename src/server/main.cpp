// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file main.cpp
 * @brief LSP server entry point.
 *
 * Initializes stdin/stdout for JSON-RPC communication and
 * runs the Language Server Protocol server loop.
 */

#include "pch.h"
#include "lsp_connection.hpp"
#include "lsp_server.hpp"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

int main() {
#ifdef _WIN32
    // LSP requires exact "\r\n" in headers.
    // Windows text mode converts \n -> \r\n, which corrupts our \r\n to \r\r\n.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    LspServer server;

    JsonRpcConnection conn([&](const std::string& raw, JsonRpcConnection& connection) {
        bool hasResp = false;
        std::string resp = server.Handle(raw, hasResp);
        if (hasResp && !resp.empty()) {
            connection.Send(resp);
        }

        // flush notifications
        std::string out;
        while (server.PopOutgoing(out)) {
            connection.Send(out);
        }
    });

    conn.Run();
    return 0;
}
