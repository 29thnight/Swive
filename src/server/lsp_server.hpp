// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file lsp_server.hpp
 * @brief Language Server Protocol server.
 *
 * Handles LSP requests (initialize, textDocument/*, etc.) and
 * provides IDE features: diagnostics, completion, hover, go-to-definition.
 */

#pragma once
#include <string>
#include <unordered_map>
#include <optional>

#include <nlohmann/json.hpp>

#include "ssproject_loader.hpp"
#include "module_resolver.hpp"
#include "analyzer.hpp"

struct Document {
    std::string uri;
    std::string text;
    int version = 0;
};

class LspServer {
public:
    LspServer();

    // Handle one incoming JSON-RPC message, may return response JSON
    // outHasResponse: true if response needed
    std::string Handle(const std::string& rawJson, bool& outHasResponse);

    // Build queued notifications (publishDiagnostics etc.)
    bool PopOutgoing(std::string& outJson);

private:
    using json = nlohmann::json;

    bool running_ = true;
    bool initialized_ = false;

    std::filesystem::path workspace_root_;
    std::optional<SSProjectInfo> project_;

    std::unordered_map<std::string, Document> docs_; // uri -> doc

    ProjectModuleResolver resolver_;
    Analyzer analyzer_;

    // outgoing json queue
    std::vector<std::string> outbox_;

private:
    void EnsureProjectLoaded(const json& initParams);

    // handlers
    json OnInitialize(const json& req);
    json OnShutdown(const json& req);
    json OnSemanticTokensFull(const json& req);

    void OnDidOpen(const json& params);
    void OnDidChange(const json& params);
    void OnDidSave(const json& params);
    void OnDidClose(const json& params);

    void AnalyzeAndPublish(const std::string& uri);

    // helpers
    static json MakeResponse(const json& req, const json& result);
    static json MakeError(const json& req, int code, const std::string& message);
    static json MakeNotification(const std::string& method, const json& params);

    static json DiagnosticsToLsp(const std::vector<DiagnosticItem>& diags);
};
