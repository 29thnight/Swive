#pragma once

#include "ss_token.hpp"
#include "ss_ast.hpp"

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
        size_t current_{ 0 };

        void skip_comments();

        // ---- Statement parsers ----
        StmtPtr declaration();
        StmtPtr var_declaration();
        StmtPtr func_declaration();
        StmtPtr class_declaration(AccessLevel access_level = AccessLevel::Internal);
        StmtPtr struct_declaration(AccessLevel access_level = AccessLevel::Internal);  // Struct declaration parser
        StmtPtr enum_declaration(AccessLevel access_level = AccessLevel::Internal);    // Enum declaration parser
        StmtPtr protocol_declaration(AccessLevel access_level = AccessLevel::Internal); // Protocol declaration parser
        StmtPtr extension_declaration(AccessLevel access_level = AccessLevel::Internal); // Extension declaration parser
        StmtPtr attribute_declaration();
        StmtPtr import_declaration();  // Import statement parser
        std::unique_ptr<VarDeclStmt> parse_variable_decl(bool is_let);
        StmtPtr parse_tuple_destructuring(bool is_let, uint32_t line);
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
        std::vector<std::string> parse_generic_params();
        std::vector<GenericConstraint> parse_generic_constraints(const std::vector<std::string>& generic_params);
        std::vector<Attribute> parse_attributes();
        bool looks_like_attribute_list() const;
        bool is_attribute_target_token(TokenType type) const;

        // ---- Utilities ----
        const Token& advance();
        const Token& peek() const;
        const Token& previous() const;
        bool check(TokenType type) const;
        bool match(TokenType type);
        const Token& consume(TokenType type, const std::string& message);
        [[noreturn]] void error(const Token& token, const std::string& message);
        bool is_at_end() const;
        bool looks_like_generic_type_args() const;
        bool is_type_start_token(size_t index) const;
    };

} // namespace swiftscript
