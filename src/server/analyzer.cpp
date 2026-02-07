#include "stdafx.h"
#include "analyzer.hpp"
#include "module_resolver.hpp"
#include "lsp_utils.hpp"

#include <set>

// SwiftScript headers
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_ast.hpp"
#include "ss_compiler.hpp"      // IModuleResolver
#include "ss_type_checker.hpp"  // TypeChecker

using swiftscript::Lexer;
using swiftscript::Parser;
using swiftscript::ParseError;
using swiftscript::TokenType;
using swiftscript::StmtKind;
using swiftscript::ImportStmt;
using swiftscript::TypeChecker;
using swiftscript::TypeCheckError;
using swiftscript::IModuleResolver;

// ─── IModuleResolver adapter ────────────────────────────────

namespace {

class LspModuleResolver : public IModuleResolver {
public:
    explicit LspModuleResolver(ProjectModuleResolver* resolver)
        : resolver_(resolver) {}

    bool ResolveAndLoad(const std::string& module_name,
                        std::string& out_full_path,
                        std::string& out_source,
                        std::string& out_error) override
    {
        if (!resolver_) {
            out_error = "No module resolver configured";
            return false;
        }

        std::filesystem::path resolved;
        if (!resolver_->Resolve(module_name, resolved, out_error)) {
            return false;
        }

        out_full_path = resolved.string();

        std::ifstream ifs(resolved, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            out_error = "Cannot open module file: " + resolved.string();
            return false;
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        out_source = oss.str();
        return true;
    }

private:
    ProjectModuleResolver* resolver_;
};

} // anonymous namespace

// ─── Helpers ─────────────────────────────────────────────────

void Analyzer::PushDiag(std::vector<DiagnosticItem>& out,
    const std::string& uri,
    int line0, int col0, int endCol0,
    DiagnosticItem::Severity severity,
    const std::string& msg) {
    DiagnosticItem d;
    d.uri = uri;
    d.line = line0;
    d.col = col0;
    d.endLine = line0;
    d.endCol = endCol0;
    d.severity = severity;
    d.message = msg;
    out.push_back(std::move(d));
}

std::string Analyzer::GetLine(const std::string& text, int line0) {
    int cur = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();

        if (cur == line0) {
            // Strip trailing \r if present
            size_t end = eol;
            if (end > pos && text[end - 1] == '\r') --end;
            return text.substr(pos, end - pos);
        }

        pos = eol + 1;
        ++cur;
    }
    return "";
}

int Analyzer::GetLineTrimmedLength(const std::string& text, int line0) {
    std::string line = GetLine(text, line0);
    // Trim trailing whitespace
    int len = (int)line.size();
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) --len;
    return len > 0 ? len : 1; // At least 1 to have a visible underline
}

std::string Analyzer::ExtractQuotedSymbol(const std::string& msg) {
    // Look for 'symbol' pattern (single quotes)
    auto q1 = msg.find('\'');
    if (q1 != std::string::npos) {
        auto q2 = msg.find('\'', q1 + 1);
        if (q2 != std::string::npos && q2 > q1 + 1) {
            return msg.substr(q1 + 1, q2 - q1 - 1);
        }
    }
    return "";
}

bool Analyzer::FindSymbolInLine(const std::string& lineText, const std::string& symbol,
    int& outCol, int& outEndCol) {
    if (symbol.empty()) return false;

    auto pos = lineText.find(symbol);
    if (pos == std::string::npos) return false;

    outCol = (int)pos;
    outEndCol = (int)(pos + symbol.size());
    return true;
}

// ─── Analyzer::Analyze ──────────────────────────────────────

void Analyzer::Analyze(const std::string& docUri,
    const std::string& text,
    std::vector<DiagnosticItem>& out) {
    out.clear();

    // 1) Lexer
    Lexer lexer(text);
    auto tokens = lexer.tokenize_all();

    for (const auto& tk : tokens) {
        if (tk.type == TokenType::Error) {
            const int line0 = (tk.line > 0) ? (int)tk.line - 1 : 0;
            const int col0 = (tk.column > 0) ? (int)tk.column - 1 : 0;
            // Lexer tokens have column info; underline at least the token position
            int endCol = col0 + std::max((int)tk.lexeme.size(), 1);
            PushDiag(out, docUri, line0, col0, endCol,
                DiagnosticItem::Severity::Error, std::string(tk.lexeme));
        }
    }

    // 2) Parser
    std::vector<swiftscript::StmtPtr> program;
    try {
        Parser parser(std::move(tokens));
        program = parser.parse();
    }
    catch (const ParseError& e) {
        const int line0 = (e.line > 0) ? (int)e.line - 1 : 0;
        const int col0 = (e.column > 0) ? (int)e.column - 1 : 0;
        // Parser errors have column; underline rest of line from error position
        int lineLen = GetLineTrimmedLength(text, line0);
        int endCol = std::max(col0 + 1, lineLen);
        PushDiag(out, docUri, line0, col0, endCol,
            DiagnosticItem::Severity::Error, std::string(e.what()));
        return;
    }
    catch (const std::exception& e) {
        PushDiag(out, docUri, 0, 0, 1,
            DiagnosticItem::Severity::Error, std::string("Parser exception: ") + e.what());
        return;
    }

    // 3) Import resolve check
    if (resolver_) {
        for (const auto& st : program) {
            if (!st) continue;
            if (st->kind != StmtKind::Import) continue;

            auto* imp = static_cast<ImportStmt*>(st.get());
            std::filesystem::path resolved;
            std::string err;
            if (!resolver_->Resolve(imp->module_path, resolved, err)) {
                const int line0 = (imp->line > 0) ? (int)imp->line - 1 : 0;
                // Try to find the module name in the source line
                std::string lineText = GetLine(text, line0);
                int col0 = 0, endCol = GetLineTrimmedLength(text, line0);
                FindSymbolInLine(lineText, imp->module_path, col0, endCol);
                PushDiag(out, docUri, line0, col0, endCol,
                    DiagnosticItem::Severity::Error,
                    "Import error: " + err + " (import " + imp->module_path + ")");
            }
        }
    }

    // 4) Type Checker — semantic analysis
    try {
        LspModuleResolver lspResolver(resolver_);

        TypeChecker checker;
        if (!base_directory_.empty()) {
            checker.set_base_directory(base_directory_);
        }
        checker.set_module_resolver(&lspResolver);
        checker.check_no_throw(program);

        // Collect type errors
        for (const auto& err : checker.errors()) {
            const int line0 = (err.line() > 0) ? (int)err.line() - 1 : 0;

            // Try to locate the symbol mentioned in the error on that line
            std::string lineText = GetLine(text, line0);
            std::string symbol = ExtractQuotedSymbol(err.message());

            int col0 = 0;
            int endCol = GetLineTrimmedLength(text, line0);

            if (!symbol.empty()) {
                // Found a quoted symbol — try to locate it in the source line
                FindSymbolInLine(lineText, symbol, col0, endCol);
            }

            PushDiag(out, docUri, line0, col0, endCol,
                DiagnosticItem::Severity::Error, err.message());
        }

        // Collect warnings
        for (const auto& warn : checker.warnings()) {
            int warnLine = 0;
            std::string warnMsg = warn;

            const std::string prefix = "Warning (line ";
            auto pos = warn.find(prefix);
            if (pos != std::string::npos) {
                auto numStart = pos + prefix.size();
                auto numEnd = warn.find(')', numStart);
                if (numEnd != std::string::npos) {
                    try {
                        int parsedLine = std::stoi(warn.substr(numStart, numEnd - numStart));
                        warnLine = (parsedLine > 0) ? parsedLine - 1 : 0;
                    }
                    catch (...) {}
                    auto msgStart = warn.find(": ", numEnd);
                    if (msgStart != std::string::npos) {
                        warnMsg = warn.substr(msgStart + 2);
                    }
                }
            }
            else if (warn.rfind("Warning: ", 0) == 0) {
                warnMsg = warn.substr(9);
            }

            int endCol = GetLineTrimmedLength(text, warnLine);
            std::string symbol = ExtractQuotedSymbol(warnMsg);
            int col0 = 0;
            if (!symbol.empty()) {
                std::string lineText = GetLine(text, warnLine);
                FindSymbolInLine(lineText, symbol, col0, endCol);
            }

            PushDiag(out, docUri, warnLine, col0, endCol,
                DiagnosticItem::Severity::Warning, warnMsg);
        }
    }
    catch (const std::exception& e) {
        PushDiag(out, docUri, 0, 0, 1,
            DiagnosticItem::Severity::Error, std::string("Type check exception: ") + e.what());
    }
}

// ─── Analyzer::ComputeSemanticTokens ─────────────────────────

void Analyzer::ComputeSemanticTokens(const std::string& text,
    std::vector<SemanticToken>& out) {
    out.clear();

    // 1) Lex to get all tokens with position info
    Lexer lexer(text);
    auto tokens = lexer.tokenize_all();

    // 2) Parse + TypeCheck to populate symbol tables
    //    (we do a full check_no_throw so the checker's known_types_ etc. are populated)
    std::vector<swiftscript::StmtPtr> program;
    TypeChecker checker;
    bool checkerReady = false;
    try {
        // We need to keep the source alive for string_view tokens,
        // but tokenize_all already copied what we need.
        // Re-lex for parsing (parser consumes tokens).
        Lexer lexer2(text);
        auto tokens2 = lexer2.tokenize_all();

        Parser parser(std::move(tokens2));
        program = parser.parse();

        LspModuleResolver lspResolver(resolver_);
        if (!base_directory_.empty()) {
            checker.set_base_directory(base_directory_);
        }
        checker.set_module_resolver(&lspResolver);
        checker.check_no_throw(program);
        checkerReady = true;
    }
    catch (...) {
        // If parse or check fails, we still emit tokens for keywords/literals/comments
        // checkerReady stays false — we just won't classify identifiers via TypeChecker
    }

    // 3) Build a set of error symbols with their line numbers
    //    so we can mark only truly unresolved identifiers as red.
    //    Key = (0-based line, symbol name)
    std::set<std::pair<int, std::string>> errorSymbols;
    if (checkerReady) {
        for (const auto& err : checker.errors()) {
            const int errLine0 = (err.line() > 0) ? (int)err.line() - 1 : 0;
            std::string sym = ExtractQuotedSymbol(err.message());
            if (!sym.empty()) {
                errorSymbols.insert({ errLine0, sym });
            }
        }
    }

    // 4) Walk through tokens and classify each one
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& tk = tokens[i];
        if (tk.type == TokenType::Eof) break;

        // Token line/col are 1-based in the lexer, LSP semantic tokens need 0-based
        const int line0 = (tk.line > 0) ? (int)tk.line - 1 : 0;
        const int col0  = (tk.column > 0) ? (int)tk.column - 1 : 0;
        const int len   = (int)tk.lexeme.size();
        if (len <= 0) continue;

        SemanticTokenType semType;
        bool emit = false;

        switch (tk.type) {
        // ── Comments ──
        case TokenType::Comment:
            semType = SemanticTokenType::Comment;
            emit = true;
            break;

        // ── Keywords ──
        case TokenType::Func:
        case TokenType::Class:
        case TokenType::Struct:
        case TokenType::Enum:
        case TokenType::Protocol:
        case TokenType::Extension:
        case TokenType::Attribute:
        case TokenType::Var:
        case TokenType::Let:
        case TokenType::Weak:
        case TokenType::Unowned:
        case TokenType::Nil:
        case TokenType::Guard:
        case TokenType::If:
        case TokenType::Else:
        case TokenType::Switch:
        case TokenType::Case:
        case TokenType::Default:
        case TokenType::For:
        case TokenType::While:
        case TokenType::Repeat:
        case TokenType::Break:
        case TokenType::Continue:
        case TokenType::Return:
        case TokenType::In:
        case TokenType::Import:
        case TokenType::Public:
        case TokenType::Private:
        case TokenType::Internal:
        case TokenType::Fileprivate:
        case TokenType::Static:
        case TokenType::Override:
        case TokenType::Init:
        case TokenType::Deinit:
        case TokenType::Self:
        case TokenType::Super:
        case TokenType::Mutating:
        case TokenType::Get:
        case TokenType::Set:
        case TokenType::WillSet:
        case TokenType::DidSet:
        case TokenType::Lazy:
        case TokenType::As:
        case TokenType::Is:
        case TokenType::Where:
        case TokenType::Try:
        case TokenType::Catch:
        case TokenType::Throw:
        case TokenType::Throws:
        case TokenType::Do:
            semType = SemanticTokenType::Keyword;
            emit = true;
            break;

        // ── true / false ──
        case TokenType::True:
        case TokenType::False:
        case TokenType::Null:
            semType = SemanticTokenType::Keyword;
            emit = true;
            break;

        // ── Numeric literals ──
        case TokenType::Integer:
        case TokenType::Float:
            semType = SemanticTokenType::Number;
            emit = true;
            break;

        // ── String literals ──
        case TokenType::String:
        case TokenType::InterpolatedStringStart:
        case TokenType::StringSegment:
        case TokenType::InterpolatedStringEnd:
            semType = SemanticTokenType::String;
            emit = true;
            break;

        // ── Identifiers ──
        case TokenType::Identifier: {
            std::string name(tk.lexeme);

            // Known type name (persists after check_no_throw via known_types_)
            if (checkerReady && checker.is_known_type(name)) {
                semType = SemanticTokenType::Type;
                emit = true;
            }
            // Module name after 'import'
            else if (i > 0 && tokens[i - 1].type == TokenType::Import) {
                semType = SemanticTokenType::Namespace;
                emit = true;
            }
            // Error symbol: TypeChecker reported an error mentioning this symbol on this line
            else if (!errorSymbols.empty() && errorSymbols.count({ line0, name })) {
                semType = SemanticTokenType::Unresolve;
                emit = true;
            }
            // Everything else: don't emit — let TextMate grammar handle it
            break;
        }

        default:
            break;
        }

        if (emit) {
            SemanticToken st;
            st.line = line0;
            st.col = col0;
            st.length = len;
            st.type = semType;
            st.modifiers = 0;
            out.push_back(st);
        }
    }
}
