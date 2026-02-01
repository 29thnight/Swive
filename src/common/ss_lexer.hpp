#pragma once

#include "ss_token.hpp"

namespace swiftscript {

class Lexer {
public:
    explicit Lexer(std::string_view source);

    Token next_token();
    std::vector<Token> tokenize_all();

private:
    std::string_view source_;
    uint32_t start_{0};
    uint32_t current_{0};
    uint32_t line_{1};
    uint32_t column_{1};
    uint32_t token_column_{1};

    char advance();
    char peek() const;
    char peek_next() const;
    bool match(char expected);
    bool is_at_end() const;

    Token make_token(TokenType type);
    Token make_token_at(TokenType type, uint32_t start, uint32_t end, uint32_t line, uint32_t column);
    Token error_token(const char* message);

    Token scan_number();
    Token scan_string();
    Token scan_interpolated_string_segment();
    Token scan_identifier();

    void skip_whitespace();

    static bool is_digit(char c);
    static bool is_alpha(char c);
    static bool is_alpha_numeric(char c);

    std::vector<Token> pending_tokens_;
    bool in_interpolated_string_{false};
    bool in_interpolation_{false};
    uint32_t interpolation_depth_{0};
};

} // namespace swiftscript
