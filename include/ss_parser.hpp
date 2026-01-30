#pragma once

#include "ss_token.hpp"
#include "ss_ast.hpp"
#include <vector>
#include <string>
#include <stdexcept>

namespace swiftscript {

class ParseError : public std::runtime_error {
public:
    uint32_t line;
    uint32_t column;
    ParseError(const std::string& msg, uint32_t ln, uint32_t col)
        : std::runtime_error(msg), line(ln), column(col) {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::vector<StmtPtr> parse();

private:
    std::vector<Token> tokens_;
    size_t current_{0};

    // ---- Statement parsers ----
    StmtPtr declaration();
    StmtPtr var_declaration();
    StmtPtr func_declaration();
    StmtPtr class_declaration();
    StmtPtr struct_declaration();  // Struct declaration parser
    StmtPtr enum_declaration();    // Enum declaration parser
    StmtPtr protocol_declaration(); // Protocol declaration parser
    StmtPtr extension_declaration(); // Extension declaration parser
    StmtPtr import_declaration();  // Import statement parser
    std::unique_ptr<VarDeclStmt> parse_variable_decl(bool is_let);
    StmtPtr statement();
    StmtPtr if_statement();
    StmtPtr guard_statement();
    StmtPtr while_statement();
    StmtPtr repeat_while_statement();
    StmtPtr for_in_statement();
    StmtPtr switch_statement();
    StmtPtr break_statement();
    StmtPtr continue_statement();
    StmtPtr return_statement();
    StmtPtr throw_statement();
    StmtPtr print_statement();
    std::unique_ptr<BlockStmt> block();
    StmtPtr expression_statement();

    // ---- Expression parsers (precedence climbing) ----
    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr ternary();
    ExprPtr nil_coalesce();
    ExprPtr or_expr();
    ExprPtr and_expr();
    ExprPtr bitwise_or();
    ExprPtr bitwise_xor();
    ExprPtr bitwise_and();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr type_check_cast();
    ExprPtr shift();
    ExprPtr addition();
    ExprPtr multiplication();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary();
    ExprPtr closure_expression();
    ParamDecl parse_param(bool allow_default);
    std::vector<ParamDecl> parse_param_list(bool allow_default);
    PatternPtr parse_pattern();

    // ---- Type annotation ----
    TypeAnnotation parse_type_annotation();

    // ---- Utilities ----
    const Token& advance();
    const Token& peek() const;
    const Token& previous() const;
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token& consume(TokenType type, const std::string& message);
    [[noreturn]] void error(const Token& token, const std::string& message);
    bool is_at_end() const;
};

} // namespace swiftscript
