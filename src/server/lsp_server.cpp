#include "stdafx.h"
#include "lsp_server.hpp"
#include "lsp_utils.hpp"
#include <filesystem>

LspServer::LspServer()
    : resolver_({})
    , analyzer_(&resolver_) {
}

void LspServer::EnsureProjectLoaded(const json& initParams) {
    if (project_.has_value()) return;

    // rootUri or rootPath
    std::string rootUri;
    if (initParams.contains("rootUri") && initParams["rootUri"].is_string()) {
        rootUri = initParams["rootUri"].get<std::string>();
    }
    else if (initParams.contains("rootPath") && initParams["rootPath"].is_string()) {
        // older clients
        rootUri = initParams["rootPath"].get<std::string>();
        workspace_root_ = std::filesystem::path(rootUri);
    }

    if (!rootUri.empty()) {
        workspace_root_ = lsp::UriToPath(rootUri);
    }

    // Find .ssproject
    std::filesystem::path projPath;
    if (FindFirstSSProject(workspace_root_, projPath)) {
        SSProjectInfo info;
        std::string err;
        if (LoadSSProjectInfo(projPath, info, err)) {
            project_ = info;
            resolver_.SetRoots(info.import_roots);
            // Set base directory for TypeChecker module resolution
            analyzer_.SetBaseDirectory(projPath.parent_path().string());
        }
    }
}

nlohmann::json LspServer::MakeResponse(const json& req, const json& result) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = req["id"];
    resp["result"] = result;
    return resp;
}

nlohmann::json LspServer::MakeError(const json& req, int code, const std::string& message) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = req["id"];
    resp["error"] = { {"code", code}, {"message", message} };
    return resp;
}

nlohmann::json LspServer::MakeNotification(const std::string& method, const json& params) {
    json n;
    n["jsonrpc"] = "2.0";
    n["method"] = method;
    n["params"] = params;
    return n;
}

nlohmann::json LspServer::DiagnosticsToLsp(const std::vector<DiagnosticItem>& diags) {
    json arr = json::array();
    for (const auto& d : diags) {
        json item;
        item["range"] = {
            {"start", {{"line", d.line}, {"character", d.col}}},
            {"end",   {{"line", d.endLine}, {"character", d.endCol}}}
        };
        item["severity"] = (int)d.severity; // LSP uses 1..4
        item["source"] = "SwiftScript";
        item["message"] = d.message;
        arr.push_back(std::move(item));
    }
    return arr;
}

nlohmann::json LspServer::OnInitialize(const json& req) {
    // req["params"] contains rootUri, capabilities, etc.
    const json& params = req.value("params", json::object());
    EnsureProjectLoaded(params);

    json caps;
    // We request Full sync so didChange sends full text
    caps["textDocumentSync"] = 1; // TextDocumentSyncKind::Full

    // Semantic tokens provider (full document)
    {
        json legend;
        // Token types — must match SemanticTokenType enum order exactly
        legend["tokenTypes"] = json::array({
            "type",        // 0  Type
            "function",    // 1  Function
            "variable",    // 2  Variable
            "parameter",   // 3  Parameter
            "property",    // 4  Property
            "method",      // 5  Method
            "keyword",     // 6  Keyword
            "string",      // 7  String
            "number",      // 8  Number
            "enum",        // 9  Enum
            "enumMember",  // 10 EnumMember
            "namespace",   // 11 Namespace
            "comment",     // 12 Comment
            "unresolve",   // 13 Unresolve (undefined symbols)
        });
        legend["tokenModifiers"] = json::array({
            "declaration",
            "definition",
            "readonly",
            "static",
        });

        json semTok;
        semTok["legend"] = legend;
        semTok["full"] = true;
        // range is not supported yet
        caps["semanticTokensProvider"] = semTok;
    }

    json result;
    result["capabilities"] = caps;

    initialized_ = true;
    return result;
}

nlohmann::json LspServer::OnShutdown(const json& req) {
    running_ = false;
    json result = nullptr;
    return result;
}

void LspServer::OnDidOpen(const json& params) {
    const auto& td = params["textDocument"];
    std::string uri = td["uri"].get<std::string>();
    std::string text = td["text"].get<std::string>();
    int version = td.value("version", 0);

    docs_[uri] = Document{ uri, text, version };
    AnalyzeAndPublish(uri);
}

void LspServer::OnDidChange(const json& params) {
    const auto& td = params["textDocument"];
    std::string uri = td["uri"].get<std::string>();
    int version = td.value("version", 0);

    auto it = docs_.find(uri);
    if (it == docs_.end()) {
        docs_[uri] = Document{ uri, "", version };
        it = docs_.find(uri);
    }

    // Full sync: take last change.text as entire doc
    const auto& changes = params["contentChanges"];
    if (changes.is_array() && !changes.empty()) {
        const auto& last = changes.back();
        if (last.contains("text")) {
            it->second.text = last["text"].get<std::string>();
            it->second.version = version;
        }
    }

    AnalyzeAndPublish(uri);
}

void LspServer::OnDidSave(const json& params) {
    const auto& td = params["textDocument"];
    std::string uri = td["uri"].get<std::string>();
    // Some clients include text on save, but not required for Full sync
    AnalyzeAndPublish(uri);
}

void LspServer::OnDidClose(const json& params) {
    const auto& td = params["textDocument"];
    std::string uri = td["uri"].get<std::string>();

    docs_.erase(uri);

    // Clear diagnostics on close
    json p;
    p["uri"] = uri;
    p["diagnostics"] = json::array();
    outbox_.push_back(MakeNotification("textDocument/publishDiagnostics", p).dump());
}

nlohmann::json LspServer::OnSemanticTokensFull(const json& req) {
    const json& params = req.value("params", json::object());
    std::string uri = params["textDocument"]["uri"].get<std::string>();

    auto it = docs_.find(uri);
    if (it == docs_.end()) {
        // Document not open — return empty data
        json result;
        result["data"] = json::array();
        return result;
    }

    std::vector<SemanticToken> tokens;
    analyzer_.ComputeSemanticTokens(it->second.text, tokens);

    // Encode as LSP delta format:
    // Each token is encoded as 5 integers:
    //   deltaLine, deltaStartChar, length, tokenType, tokenModifiers
    json data = json::array();
    int prevLine = 0;
    int prevCol = 0;

    for (const auto& tok : tokens) {
        int deltaLine = tok.line - prevLine;
        int deltaStart = (deltaLine == 0) ? (tok.col - prevCol) : tok.col;

        data.push_back(deltaLine);
        data.push_back(deltaStart);
        data.push_back(tok.length);
        data.push_back(static_cast<int>(tok.type));
        data.push_back(tok.modifiers);

        prevLine = tok.line;
        prevCol = tok.col;
    }

    json result;
    result["data"] = data;
    return result;
}

void LspServer::AnalyzeAndPublish(const std::string& uri) {
    auto it = docs_.find(uri);
    if (it == docs_.end()) return;

    std::vector<DiagnosticItem> diags;
    analyzer_.Analyze(uri, it->second.text, diags);

    json p;
    p["uri"] = uri;
    p["diagnostics"] = DiagnosticsToLsp(diags);

    outbox_.push_back(MakeNotification("textDocument/publishDiagnostics", p).dump());
}

std::string LspServer::Handle(const std::string& rawJson, bool& outHasResponse) {
    outHasResponse = false;

    json msg;
    try {
        msg = json::parse(rawJson);
    }
    catch (...) {
        return "";
    }

    // request vs notification
    const bool hasId = msg.contains("id");
    const std::string method = msg.value("method", "");

    if (!method.empty()) {
        // notifications
        if (!hasId) {
            const json& params = msg.value("params", json::object());

            if (method == "initialized") {
                // no-op
            }
            else if (method == "textDocument/didOpen") {
                OnDidOpen(params);
            }
            else if (method == "textDocument/didChange") {
                OnDidChange(params);
            }
            else if (method == "textDocument/didSave") {
                OnDidSave(params);
            }
            else if (method == "textDocument/didClose") {
                OnDidClose(params);
            }
            else if (method == "exit") {
                running_ = false;
            }
            return "";
        }

        // requests
        if (method == "initialize") {
            outHasResponse = true;
            json result = OnInitialize(msg);
            return MakeResponse(msg, result).dump();
        }
        if (method == "shutdown") {
            outHasResponse = true;
            json result = OnShutdown(msg);
            return MakeResponse(msg, result).dump();
        }
        if (method == "textDocument/semanticTokens/full") {
            outHasResponse = true;
            json result = OnSemanticTokensFull(msg);
            return MakeResponse(msg, result).dump();
        }

        // unknown request
        outHasResponse = true;
        return MakeError(msg, -32601, "Method not found: " + method).dump();
    }

    return "";
}

bool LspServer::PopOutgoing(std::string& outJson) {
    if (outbox_.empty()) return false;
    outJson = std::move(outbox_.front());
    outbox_.erase(outbox_.begin());
    return true;
}
