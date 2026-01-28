#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace swiftscript {

// Token types
enum class TokenType {
    // End of file
    Eof,
    Error,
    Comment,
    
    // Literals
    Integer,
    Float,
    String,
    True,
    False,
    Null,
    
    // Identifiers
    Identifier,
    
    // Keywords
    Func,
    Class,
    Struct,
    Enum,
    Protocol,
    Extension,
    Var,
    Let,
    Weak,        // weak keyword
    Unowned,     // unowned keyword
    Nil,         // nil keyword
    Guard,       // guard keyword
    If,
    Else,
    Switch,
    Case,
    Default,
    For,
    While,
    Repeat,
    Break,
    Continue,
    Return,
    In,
    Import,
    Public,
    Private,
    Internal,
    Static,
    Override,
    Init,
    Deinit,
    Self,
    Super,
    
    // Operators
    Plus,           // +
    Minus,          // -
    Star,           // *
    Slash,          // /
    Percent,        // %
    Equal,          // =
    PlusEqual,      // +=
    MinusEqual,     // -=
    StarEqual,      // *=
    SlashEqual,     // /=
    EqualEqual,     // ==
    NotEqual,       // !=
    Less,           // <
    Greater,        // >
    LessEqual,      // <=
    GreaterEqual,   // >=
    And,            // &&
    Or,             // ||
    Not,            // !
    BitwiseAnd,     // &
    BitwiseOr,      // |
    BitwiseXor,     // ^
    BitwiseNot,     // ~
    LeftShift,      // <<
    RightShift,     // >>
    Question,       // ?
    NilCoalesce,    // ??
    OptionalChain,  // ?.
    Colon,          // :
    Arrow,          // ->
    
    // Delimiters
    LeftParen,      // (
    RightParen,     // )
    LeftBrace,      // {
    RightBrace,     // }
    LeftBracket,    // [
    RightBracket,   // ]
    Comma,          // ,
    Dot,            // .
    Semicolon,      // ;
    
    // Range operators
    Range,          // ..
    RangeInclusive, // ...
};

// Token structure
struct Token {
    TokenType type;
    std::string_view lexeme;
    uint32_t line;
    uint32_t column;
    uint32_t position;  // Byte offset from start
    
    // For literals
    union {
        int64_t int_value;
        double float_value;
    } value;
    
    Token() 
        : type(TokenType::Eof), line(0), column(0), position(0) {
        value.int_value = 0;
    }
    
    Token(TokenType t, std::string_view lex, uint32_t ln, uint32_t col, uint32_t pos)
        : type(t), lexeme(lex), line(ln), column(col), position(pos) {
        value.int_value = 0;
    }
    
    bool is_keyword() const;
    bool is_operator() const;
    bool is_literal() const;
    std::string to_string() const;
};

// Token utilities
class TokenUtils {
public:
    static const char* token_type_name(TokenType type);
    static bool is_assignment_operator(TokenType type);
    static bool is_comparison_operator(TokenType type);
    static bool is_binary_operator(TokenType type);
    static bool is_unary_operator(TokenType type);
    static int operator_precedence(TokenType type);
    
    // Check if string is a keyword
    static TokenType keyword_type(std::string_view str);
    static bool is_keyword(std::string_view str);
};

} // namespace swiftscript
