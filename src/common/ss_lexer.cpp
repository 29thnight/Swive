#include "pch.h"
#include "ss_lexer.hpp"

namespace swiftscript {

Lexer::Lexer(std::string_view source)
    : source_(source) {}

// ---- Character helpers ----

bool Lexer::is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool Lexer::is_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

bool Lexer::is_alpha_numeric(char c) {
    return is_alpha(c) || is_digit(c);
}

bool Lexer::is_at_end() const {
    return current_ >= source_.size();
}

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source_[current_];
}

char Lexer::peek_next() const {
    if (current_ + 1 >= source_.size()) return '\0';
    return source_[current_ + 1];
}

char Lexer::advance() {
    char c = source_[current_++];
    column_++;
    return c;
}

bool Lexer::match(char expected) {
    if (is_at_end()) return false;
    if (source_[current_] != expected) return false;
    current_++;
    column_++;
    return true;
}

// ---- Token constructors ----

Token Lexer::make_token(TokenType type) {
    std::string_view lexeme = source_.substr(start_, current_ - start_);
    return Token(type, lexeme, line_, token_column_, start_);
}

Token Lexer::make_token_at(TokenType type, uint32_t start, uint32_t end, uint32_t line, uint32_t column) {
    std::string_view lexeme = source_.substr(start, end - start);
    return Token(type, lexeme, line, column, start);
}

Token Lexer::error_token(const char* message) {
    return Token(TokenType::Error, std::string_view(message), line_, token_column_, start_);
}

// ---- Whitespace & comments ----

void Lexer::skip_whitespace() {
    for (;;) {
        if (is_at_end()) return;
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                line_++;
                column_ = 0;
                advance();
                break;
            case '/':
                if (peek_next() == '/') {
                    // Line comment
                    while (!is_at_end() && peek() != '\n') advance();
                } else if (peek_next() == '*') {
                    // Block comment
                    advance(); advance(); // consume /*
                    while (!is_at_end()) {
                        if (peek() == '\n') { line_++; column_ = 0; }
                        if (peek() == '*' && peek_next() == '/') {
                            advance(); advance(); // consume */
                            break;
                        }
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

// ---- Scanning ----

Token Lexer::scan_number() {
    while (is_digit(peek())) advance();

    bool is_float = false;
    if (peek() == '.' && is_digit(peek_next())) {
        is_float = true;
        advance(); // consume '.'
        while (is_digit(peek())) advance();
    }

    Token token = make_token(is_float ? TokenType::Float : TokenType::Integer);
    if (is_float) {
        token.value.float_value = std::stod(std::string(token.lexeme));
    } else {
        token.value.int_value = std::stoll(std::string(token.lexeme));
    }
    return token;
}

Token Lexer::scan_string() {
    const bool allow_interpolation = !in_interpolation_;
    const uint32_t string_start = start_;
    const uint32_t string_line = line_;
    const uint32_t string_column = token_column_;
    uint32_t segment_start = current_;
    uint32_t segment_line = line_;
    uint32_t segment_column = column_;

    while (!is_at_end() && peek() != '"') {
        if (allow_interpolation && peek() == '\\' && peek_next() == '(') {
            const uint32_t interp_start = current_;
            const uint32_t interp_line = line_;
            const uint32_t interp_column = column_;
            std::vector<Token> tokens;
            tokens.push_back(make_token_at(TokenType::InterpolatedStringStart, string_start, string_start + 1, string_line, string_column));
            if (segment_start < current_) {
                tokens.push_back(make_token_at(TokenType::StringSegment, segment_start, current_, segment_line, segment_column));
            }
            advance(); // consume '\'
            advance(); // consume '('
            tokens.push_back(make_token_at(TokenType::InterpolationStart, interp_start, interp_start + 2, interp_line, interp_column));
            in_interpolated_string_ = true;
            in_interpolation_ = true;
            interpolation_depth_ = 1;
            pending_tokens_.insert(pending_tokens_.end(), tokens.begin() + 1, tokens.end());
            return tokens.front();
        }
        if (peek() == '\n') { line_++; column_ = 0; }
        if (peek() == '\\') {
            advance(); // escape sequence
            if (!is_at_end()) {
                advance();
            }
            continue;
        }
        advance();
    }

    if (is_at_end()) {
        return error_token("Unterminated string");
    }

    advance(); // closing quote
    return make_token(TokenType::String);
}

Token Lexer::scan_interpolated_string_segment() {
    uint32_t segment_start = current_;
    uint32_t segment_line = line_;
    uint32_t segment_column = column_;

    while (!is_at_end() && peek() != '"') {
        if (peek() == '\\' && peek_next() == '(') {
            const uint32_t interp_start = current_;
            const uint32_t interp_line = line_;
            const uint32_t interp_column = column_;
            advance(); // consume '\'
            advance(); // consume '('
            in_interpolation_ = true;
            interpolation_depth_ = 1;
            if (segment_start < interp_start) {
                Token segment = make_token_at(TokenType::StringSegment, segment_start, interp_start, segment_line, segment_column);
                pending_tokens_.push_back(make_token_at(TokenType::InterpolationStart, interp_start, interp_start + 2, interp_line, interp_column));
                return segment;
            }
            return make_token_at(TokenType::InterpolationStart, interp_start, interp_start + 2, interp_line, interp_column);
        }
        if (peek() == '\n') { line_++; column_ = 0; }
        if (peek() == '\\') {
            advance();
            if (!is_at_end()) {
                advance();
            }
            continue;
        }
        advance();
    }

    if (is_at_end()) {
        return error_token("Unterminated string");
    }

    const uint32_t quote_start = current_;
    const uint32_t quote_line = line_;
    const uint32_t quote_column = column_;
    advance(); // closing quote
    in_interpolated_string_ = false;
    if (segment_start < quote_start) {
        Token segment = make_token_at(TokenType::StringSegment, segment_start, quote_start, segment_line, segment_column);
        pending_tokens_.push_back(make_token_at(TokenType::InterpolatedStringEnd, quote_start, quote_start + 1, quote_line, quote_column));
        return segment;
    }
    return make_token_at(TokenType::InterpolatedStringEnd, quote_start, quote_start + 1, quote_line, quote_column);
}

Token Lexer::scan_identifier() {
    while (is_alpha_numeric(peek())) advance();

    std::string_view text = source_.substr(start_, current_ - start_);
    TokenType type = TokenUtils::keyword_type(text);
    return make_token(type);
}

// ---- Main scan ----

Token Lexer::next_token() {
    if (!pending_tokens_.empty()) {
        Token token = pending_tokens_.front();
        pending_tokens_.erase(pending_tokens_.begin());
        return token;
    }

    if (in_interpolated_string_ && !in_interpolation_) {
        return scan_interpolated_string_segment();
    }

    skip_whitespace();

    start_ = current_;
    token_column_ = column_;

    if (is_at_end()) {
        return make_token(TokenType::Eof);
    }

    char c = advance();

    // Numbers
    if (is_digit(c)) return scan_number();

    // Identifiers / keywords
    if (is_alpha(c)) return scan_identifier();

    // Operators and delimiters
    switch (c) {
        case '(':
            if (in_interpolation_) {
                interpolation_depth_++;
            }
            return make_token(TokenType::LeftParen);
        case ')':
            if (in_interpolation_ && interpolation_depth_ > 0) {
                interpolation_depth_--;
                if (interpolation_depth_ == 0) {
                    in_interpolation_ = false;
                    return make_token(TokenType::InterpolationEnd);
                }
            }
            return make_token(TokenType::RightParen);
        case '{': return make_token(TokenType::LeftBrace);
        case '}': return make_token(TokenType::RightBrace);
        case '[': return make_token(TokenType::LeftBracket);
        case ']': return make_token(TokenType::RightBracket);
        case ',': return make_token(TokenType::Comma);
        case ';': return make_token(TokenType::Semicolon);
        case ':': return make_token(TokenType::Colon);
        case '~': return make_token(TokenType::BitwiseNot);
        case '^':
            return make_token(match('=') ? TokenType::XorEqual : TokenType::BitwiseXor);

        case '.':
            if (match('.')) {
                // Check for ... (inclusive) or ..< (exclusive)
                if (match('.')) {
                    return make_token(TokenType::RangeInclusive);
                } else if (match('<')) {
                    return make_token(TokenType::RangeExclusive);
                } else {
                    return make_token(TokenType::Range);
                }
            }
            return make_token(TokenType::Dot);

        case '+':
            return make_token(match('=') ? TokenType::PlusEqual : TokenType::Plus);
        case '-':
            if (match('>')) return make_token(TokenType::Arrow);
            return make_token(match('=') ? TokenType::MinusEqual : TokenType::Minus);
        case '*':
            return make_token(match('=') ? TokenType::StarEqual : TokenType::Star);
        case '/':
            return make_token(match('=') ? TokenType::SlashEqual : TokenType::Slash);
        case '%':
            return make_token(match('=') ? TokenType::PercentEqual : TokenType::Percent);

        case '=':
            return make_token(match('=') ? TokenType::EqualEqual : TokenType::Equal);
        case '!':
            return make_token(match('=') ? TokenType::NotEqual : TokenType::Not);

        case '<':
            if (match('<')) {
                return make_token(match('=') ? TokenType::LeftShiftEqual : TokenType::LeftShift);
            }
            return make_token(match('=') ? TokenType::LessEqual : TokenType::Less);
        case '>':
            if (match('>')) {
                return make_token(match('=') ? TokenType::RightShiftEqual : TokenType::RightShift);
            }
            return make_token(match('=') ? TokenType::GreaterEqual : TokenType::Greater);

        case '&':
            if (match('&')) return make_token(TokenType::And);
            return make_token(match('=') ? TokenType::AndEqual : TokenType::BitwiseAnd);
        case '|':
            if (match('|')) return make_token(TokenType::Or);
            return make_token(match('=') ? TokenType::OrEqual : TokenType::BitwiseOr);

        case '?':
            if (match('?')) return make_token(TokenType::NilCoalesce);
            if (match('.')) return make_token(TokenType::OptionalChain);
            return make_token(TokenType::Question);

        case '"':
            return scan_string();

        default:
            return error_token("Unexpected character");
    }
}

std::vector<Token> Lexer::tokenize_all() {
    std::vector<Token> tokens;
    for (;;) {
        Token token = next_token();
        tokens.push_back(token);
        if (token.type == TokenType::Eof || token.type == TokenType::Error) break;
    }
    return tokens;
}

} // namespace swiftscript
