#pragma once

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
    InterpolatedStringStart,
    StringSegment,
    InterpolatedStringEnd,
    InterpolationStart,
    InterpolationEnd,
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
    Attribute,   // attribute keyword for custom attributes
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
    Fileprivate,  // fileprivate keyword
    Static,
    Override,
    Init,
    Deinit,
    Self,
    Super,
    Mutating,    // mutating keyword for struct methods
    Get,         // get keyword for computed properties
    Set,         // set keyword for computed properties
    WillSet,     // willSet keyword for property observers
    DidSet,      // didSet keyword for property observers
    Lazy,        // lazy keyword for lazy properties
    As,          // as keyword for type casting
    Is,          // is keyword for type checking
    Where,       // where keyword for filtering
    Try,         // try keyword for error handling
    Catch,       // catch keyword for error handling
    Throw,       // throw keyword for error handling
    Throws,      // throws keyword for function declaration
    Do,          // do keyword for try-catch blocks
    
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
    PercentEqual,   // %=
    AndEqual,       // &=
    OrEqual,        // |=
    XorEqual,       // ^=
    LeftShiftEqual, // <<=
    RightShiftEqual,// >>=
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
    RangeExclusive, // ..<
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
