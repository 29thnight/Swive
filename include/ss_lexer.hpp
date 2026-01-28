#pragma once

#include "ss_token.hpp"
#include <string>
#include <string_view>
#include <vector>

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
    Token error_token(const char* message);

    Token scan_number();
    Token scan_string();
    Token scan_identifier();

    void skip_whitespace();

    static bool is_digit(char c);
    static bool is_alpha(char c);
    static bool is_alpha_numeric(char c);
};

} // namespace swiftscript
