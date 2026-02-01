#include "pch.h"
#include "ss_token.hpp"

namespace swiftscript {

bool Token::is_keyword() const {
    return type >= TokenType::Func && type <= TokenType::Do;
}

bool Token::is_operator() const {
    return type >= TokenType::Plus && type <= TokenType::Arrow;
}

bool Token::is_literal() const {
    return type == TokenType::Integer ||
           type == TokenType::Float ||
           type == TokenType::String ||
           type == TokenType::True ||
           type == TokenType::False ||
           type == TokenType::Null;
}

std::string Token::to_string() const {
    std::string result = TokenUtils::token_type_name(type);
    if (!lexeme.empty()) {
        result += " '" + std::string(lexeme) + "'";
    }
    result += " at line " + std::to_string(line) + ":" + std::to_string(column);
    return result;
}

const char* TokenUtils::token_type_name(TokenType type) {
    static constexpr std::array<std::string_view,
        static_cast<size_t>(TokenType::RangeExclusive) + 1> kTokenTypeNames = {
        "EOF",
        "ERROR",
        "COMMENT",
        "INTEGER",
        "FLOAT",
        "STRING",
        "INTERPOLATED_STRING_START",
        "STRING_SEGMENT",
        "INTERPOLATED_STRING_END",
        "INTERPOLATION_START",
        "INTERPOLATION_END",
        "TRUE",
        "FALSE",
        "NULL",
        "IDENTIFIER",
        "FUNC",
        "CLASS",
        "STRUCT",
        "ENUM",
        "PROTOCOL",
        "EXTENSION",
        "ATTRIBUTE",
        "VAR",
        "LET",
        "WEAK",
        "UNOWNED",
        "NIL",
        "GUARD",
        "IF",
        "ELSE",
        "SWITCH",
        "CASE",
        "DEFAULT",
        "FOR",
        "WHILE",
        "REPEAT",
        "BREAK",
        "CONTINUE",
        "RETURN",
        "IN",
        "IMPORT",
        "PUBLIC",
        "PRIVATE",
        "INTERNAL",
        "FILEPRIVATE",
        "STATIC",
        "OVERRIDE",
        "INIT",
        "DEINIT",
        "SELF",
        "SUPER",
        "MUTATING",
        "GET",
        "SET",
        "WILLSET",
        "DIDSET",
        "LAZY",
        "AS",
        "IS",
        "WHERE",
        "TRY",
        "CATCH",
        "THROW",
        "THROWS",
        "DO",
        "PLUS",
        "MINUS",
        "STAR",
        "SLASH",
        "PERCENT",
        "EQUAL",
        "PLUS_EQUAL",
        "MINUS_EQUAL",
        "STAR_EQUAL",
        "SLASH_EQUAL",
        "PERCENT_EQUAL",
        "AND_EQUAL",
        "OR_EQUAL",
        "XOR_EQUAL",
        "LEFT_SHIFT_EQUAL",
        "RIGHT_SHIFT_EQUAL",
        "EQUAL_EQUAL",
        "NOT_EQUAL",
        "LESS",
        "GREATER",
        "LESS_EQUAL",
        "GREATER_EQUAL",
        "AND",
        "OR",
        "NOT",
        "BITWISE_AND",
        "BITWISE_OR",
        "BITWISE_XOR",
        "BITWISE_NOT",
        "LEFT_SHIFT",
        "RIGHT_SHIFT",
        "QUESTION",
        "NIL_COALESCE",
        "OPTIONAL_CHAIN",
        "COLON",
        "ARROW",
        "LEFT_PAREN",
        "RIGHT_PAREN",
        "LEFT_BRACE",
        "RIGHT_BRACE",
        "LEFT_BRACKET",
        "RIGHT_BRACKET",
        "COMMA",
        "DOT",
        "SEMICOLON",
        "RANGE",
        "RANGE_INCLUSIVE",
        "RANGE_EXCLUSIVE",
    };

    const auto index = static_cast<size_t>(type);
    if (index >= kTokenTypeNames.size()) {
        return "UNKNOWN_TOKEN";
    }
    return kTokenTypeNames[index].data();
}

TokenType TokenUtils::keyword_type(std::string_view str) {
    static const std::unordered_map<std::string_view, TokenType> keywords = {
        {"func", TokenType::Func},
        {"class", TokenType::Class},
        {"struct", TokenType::Struct},
        {"enum", TokenType::Enum},
        {"protocol", TokenType::Protocol},
        {"extension", TokenType::Extension},
        {"attribute", TokenType::Attribute},
        {"var", TokenType::Var},
        {"let", TokenType::Let},
        {"weak", TokenType::Weak},
        {"unowned", TokenType::Unowned},
        {"nil", TokenType::Nil},
        {"guard", TokenType::Guard},
        {"if", TokenType::If},
        {"else", TokenType::Else},
        {"switch", TokenType::Switch},
        {"case", TokenType::Case},
        {"default", TokenType::Default},
        {"for", TokenType::For},
        {"while", TokenType::While},
        {"repeat", TokenType::Repeat},
        {"break", TokenType::Break},
        {"continue", TokenType::Continue},
        {"return", TokenType::Return},
        {"in", TokenType::In},
        {"import", TokenType::Import},
        {"public", TokenType::Public},
        {"private", TokenType::Private},
        {"internal", TokenType::Internal},
        {"fileprivate", TokenType::Fileprivate},
        {"static", TokenType::Static},
        {"override", TokenType::Override},
        {"init", TokenType::Init},
        {"deinit", TokenType::Deinit},
        {"self", TokenType::Self},
        {"super", TokenType::Super},
        {"mutating", TokenType::Mutating},
        {"get", TokenType::Get},
        {"set", TokenType::Set},
        {"willSet", TokenType::WillSet},
        {"didSet", TokenType::DidSet},
        {"lazy", TokenType::Lazy},
        {"as", TokenType::As},
        {"is", TokenType::Is},
        {"where", TokenType::Where},
        {"try", TokenType::Try},
        {"catch", TokenType::Catch},
        {"throw", TokenType::Throw},
        {"throws", TokenType::Throws},
        {"do", TokenType::Do},
        {"true", TokenType::True},
        {"false", TokenType::False},
        {"null", TokenType::Null},
    };
    
    auto it = keywords.find(str);
    return it != keywords.end() ? it->second : TokenType::Identifier;
}

bool TokenUtils::is_keyword(std::string_view str) {
    return keyword_type(str) != TokenType::Identifier;
}

bool TokenUtils::is_assignment_operator(TokenType type) {
    return type == TokenType::Equal ||
           type == TokenType::PlusEqual ||
           type == TokenType::MinusEqual ||
           type == TokenType::StarEqual ||
           type == TokenType::SlashEqual ||
           type == TokenType::PercentEqual ||
           type == TokenType::AndEqual ||
           type == TokenType::OrEqual ||
           type == TokenType::XorEqual ||
           type == TokenType::LeftShiftEqual ||
           type == TokenType::RightShiftEqual;
}

bool TokenUtils::is_comparison_operator(TokenType type) {
    return type == TokenType::EqualEqual ||
           type == TokenType::NotEqual ||
           type == TokenType::Less ||
           type == TokenType::Greater ||
           type == TokenType::LessEqual ||
           type == TokenType::GreaterEqual;
}

bool TokenUtils::is_binary_operator(TokenType type) {
    return (type >= TokenType::Plus && type <= TokenType::NilCoalesce) ||
           type == TokenType::Question ||
           type == TokenType::NilCoalesce;
}

bool TokenUtils::is_unary_operator(TokenType type) {
    return type == TokenType::Minus ||
           type == TokenType::Not ||
           type == TokenType::BitwiseNot;
}

int TokenUtils::operator_precedence(TokenType type) {
    switch (type) {
        // Highest precedence
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent:
            return 13;
            
        case TokenType::Plus:
        case TokenType::Minus:
            return 12;
            
        case TokenType::LeftShift:
        case TokenType::RightShift:
            return 11;
            
        case TokenType::Less:
        case TokenType::Greater:
        case TokenType::LessEqual:
        case TokenType::GreaterEqual:
            return 9;
            
        case TokenType::EqualEqual:
        case TokenType::NotEqual:
            return 8;
            
        case TokenType::BitwiseAnd:
            return 7;
            
        case TokenType::BitwiseXor:
            return 6;
            
        case TokenType::BitwiseOr:
            return 5;
            
        case TokenType::And:
            return 4;
            
        case TokenType::Or:
            return 3;
            
        case TokenType::Question:  // Ternary
        case TokenType::NilCoalesce:  // ??
            return 2;
            
        case TokenType::Equal:
        case TokenType::PlusEqual:
        case TokenType::MinusEqual:
        case TokenType::StarEqual:
        case TokenType::SlashEqual:
        case TokenType::PercentEqual:
        case TokenType::AndEqual:
        case TokenType::OrEqual:
        case TokenType::XorEqual:
        case TokenType::LeftShiftEqual:
        case TokenType::RightShiftEqual:
            return 1;
            
        default:
            return 0;
    }
}

} // namespace swiftscript
