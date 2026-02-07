#pragma once
#include <string>
#include <vector>
#include <filesystem>

class ProjectModuleResolver;

struct DiagnosticItem {
    std::string uri;     // document uri
    int line = 0;        // 0-based for LSP
    int col = 0;         // 0-based
    int endLine = 0;
    int endCol = 1;
    enum class Severity { Error = 1, Warning = 2, Info = 3, Hint = 4 } severity = Severity::Error;
    std::string message;
};

// LSP Semantic Token types (indices into the legend)
enum class SemanticTokenType : int {
    Type = 0,         // class/struct/enum/protocol names
    Function = 1,     // function calls & declarations
    Variable = 2,     // variables (let/var)
    Parameter = 3,    // function parameters
    Property = 4,     // member properties
    Method = 5,       // member methods (.foo())
    Keyword = 6,      // language keywords
    String = 7,       // string literals
    Number = 8,       // numeric literals
    Enum = 9,         // enum types
    EnumMember = 10,  // enum cases
    Namespace = 11,   // modules (import X)
    Comment = 12,     // comments
    Unresolve = 13,   // undefined/unresolved symbols (shown in red)
};

struct SemanticToken {
    int line = 0;       // 0-based
    int col = 0;        // 0-based
    int length = 0;
    SemanticTokenType type = SemanticTokenType::Variable;
    int modifiers = 0;  // bitmask (unused for now)
};

class Analyzer {
public:
    explicit Analyzer(ProjectModuleResolver* resolver = nullptr)
        : resolver_(resolver) {
    }

    void SetResolver(ProjectModuleResolver* resolver) { resolver_ = resolver; }
    void SetBaseDirectory(const std::string& dir) { base_directory_ = dir; }

    // Analyze a single document text
    void Analyze(const std::string& docUri,
        const std::string& text,
        std::vector<DiagnosticItem>& out);

    // Compute semantic tokens for a document
    void ComputeSemanticTokens(const std::string& text,
        std::vector<SemanticToken>& out);

private:
    ProjectModuleResolver* resolver_{ nullptr };
    std::string base_directory_;

    static void PushDiag(std::vector<DiagnosticItem>& out,
        const std::string& uri,
        int line0, int col0, int endCol0,
        DiagnosticItem::Severity severity,
        const std::string& msg);

    // Get a specific line from source text (0-based line index)
    static std::string GetLine(const std::string& text, int line0);

    // Get the length of a specific line (0-based), trimming trailing whitespace
    static int GetLineTrimmedLength(const std::string& text, int line0);

    // Try to extract a quoted symbol name like 'foo' from an error message
    static std::string ExtractQuotedSymbol(const std::string& msg);

    // Find the column and end-column of a symbol within a source line
    static bool FindSymbolInLine(const std::string& lineText, const std::string& symbol,
        int& outCol, int& outEndCol);
};
