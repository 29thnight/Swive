#include "pch.h"
#include "ss_parser.hpp"

namespace swiftscript {

    // ============================================================
    //  Constructor & entry point
    // ============================================================

    Parser::Parser(std::vector<Token> tokens)
        : tokens_(std::move(tokens)) {
    }

    std::vector<StmtPtr> Parser::parse() {
        std::vector<StmtPtr> statements;
        while (!is_at_end()) {
            skip_comments();
            if (is_at_end()) break;
            statements.push_back(declaration());
        }
        return statements;
    }

    // ============================================================
    //  Utilities
    // ============================================================

    const Token& Parser::advance() {
        if (!is_at_end()) ++current_;
        return previous();
    }

    const Token& Parser::peek() const {
        return tokens_[current_];
    }

    const Token& Parser::previous() const {
        return tokens_[current_ - 1];
    }

    bool Parser::check(TokenType type) const {
        if (is_at_end()) return false;
        return peek().type == type;
    }

    bool Parser::match(TokenType type) {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }

    void Parser::skip_comments() {
        while (!is_at_end() && peek().type == TokenType::Comment) {
            advance();
        }
    }

    const Token& Parser::consume(TokenType type, const std::string& message) {
        if (check(type)) return advance();
        error(peek(), message);
    }

    [[noreturn]] void Parser::error(const Token& token, const std::string& message) {
        std::ostringstream oss;
        oss << "[line " << token.line << ":" << token.column << "] Error";
        if (token.type == TokenType::Eof) {
            oss << " at end";
        }
        else {
            oss << " at '" << token.lexeme << "'";
        }
        oss << ": " << message;
        throw ParseError(oss.str(), token.line, token.column);
    }

    bool Parser::is_at_end() const {
        return peek().type == TokenType::Eof;
    }

    bool Parser::looks_like_generic_type_args() const {
        if (!check(TokenType::Less)) {
            return false;
        }

        size_t lookahead = current_ + 1;
        if (!is_type_start_token(lookahead)) {
            return false;
        }

        int depth = 0;
        for (size_t index = current_; index < tokens_.size(); ++index) {
            const TokenType type = tokens_[index].type;
            switch (type) {
            case TokenType::Less:
                ++depth;
                break;
            case TokenType::Greater:
                --depth;
                if (depth == 0) {
                    return true;
                }
                break;
            case TokenType::RightShift:
                depth -= 2;
                if (depth <= 0) {
                    return true;
                }
                break;
            case TokenType::Semicolon:
            case TokenType::RightParen:
            case TokenType::RightBrace:
            case TokenType::RightBracket:
            case TokenType::LeftBrace:
            case TokenType::Comma:
            case TokenType::Colon:
            case TokenType::Eof:
                return false;
            default:
                break;
            }
        }

        return false;
    }

    bool Parser::is_type_start_token(size_t index) const {
        if (index >= tokens_.size()) {
            return false;
        }

        const TokenType type = tokens_[index].type;
        return type == TokenType::Identifier || type == TokenType::LeftParen;
    }

    bool Parser::is_attribute_target_token(TokenType type) const {
        switch (type) {
        case TokenType::Public:
        case TokenType::Private:
        case TokenType::Internal:
        case TokenType::Fileprivate:
        case TokenType::Static:
        case TokenType::Override:
        case TokenType::Mutating:
        case TokenType::Init:
        case TokenType::Deinit:
        case TokenType::Import:
        case TokenType::Var:
        case TokenType::Let:
        case TokenType::Func:
        case TokenType::Class:
        case TokenType::Struct:
        case TokenType::Enum:
        case TokenType::Protocol:
        case TokenType::Extension:
        case TokenType::Attribute:
            return true;
        default:
            return false;
        }
    }

    bool Parser::looks_like_attribute_list() const {
        if (!check(TokenType::LeftBracket)) {
            return false;
        }

        size_t index = current_;
        int depth = 0;
        bool saw_identifier = false;

        while (index < tokens_.size()) {
            const TokenType type = tokens_[index].type;
            if (type == TokenType::LeftBracket) {
                ++depth;
                if (depth == 1 && index + 1 < tokens_.size() && tokens_[index + 1].type == TokenType::Identifier) {
                    saw_identifier = true;
                }
            }
            else if (type == TokenType::RightBracket) {
                --depth;
                if (depth == 0) {
                    size_t next = index + 1;
                    while (next < tokens_.size() && tokens_[next].type == TokenType::Comment) {
                        ++next;
                    }
                    if (next >= tokens_.size()) {
                        return false;
                    }
                    return saw_identifier && is_attribute_target_token(tokens_[next].type);
                }
            }
            ++index;
        }
        return false;
    }

    std::vector<Attribute> Parser::parse_attributes() {
        std::vector<Attribute> attributes;
        while (looks_like_attribute_list()) {
            consume(TokenType::LeftBracket, "Expected '[' to start attribute list.");
            if (!check(TokenType::RightBracket)) {
                do {
                    const Token& name_tok = consume(TokenType::Identifier, "Expected attribute name.");
                    Attribute attr;
                    attr.name = std::string(name_tok.lexeme);
                    attr.line = name_tok.line;
                    if (match(TokenType::LeftParen)) {
                        if (!check(TokenType::RightParen)) {
                            do {
                                attr.arguments.push_back(expression());
                            } while (match(TokenType::Comma));
                        }
                        consume(TokenType::RightParen, "Expected ')' after attribute arguments.");
                    }
                    attributes.push_back(std::move(attr));
                } while (match(TokenType::Comma));
            }
            consume(TokenType::RightBracket, "Expected ']' after attribute list.");
            skip_comments();
        }
        return attributes;
    }

    // ============================================================
    //  Type annotation
    // ============================================================

    TypeAnnotation Parser::parse_type_annotation() {
        // Parenthesized type: could be Function Type or Tuple Type
        if (match(TokenType::LeftParen)) {
            // Collect elements inside parentheses
            struct ParsedElement {
                std::optional<std::string> label;
                TypeAnnotation type;
            };
            std::vector<ParsedElement> elements;

            if (!check(TokenType::RightParen)) {
                do {
                    std::optional<std::string> label;
                    // Check for "identifier :" pattern which indicates a labeled tuple element
                    if (check(TokenType::Identifier)) {
                        // Peek next token to see if it is a colon
                        if (current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == TokenType::Colon) {
                            label = advance().lexeme;
                            advance(); // consume ':'
                        }
                    }

                    elements.push_back({ label, parse_type_annotation() });
                } while (match(TokenType::Comma));
            }

            consume(TokenType::RightParen, "Expected ')' after type list.");

            // Check if followed by '->', which means it's a function type
            if (match(TokenType::Arrow)) {
                TypeAnnotation ta;
                ta.is_function_type = true;
                ta.name = "Function";

                for (auto& elem : elements) {
                    // For function types, we currently discard external argument labels in the type system representation
                    // or the AST doesn't support them in param_types vector yet.
                    ta.param_types.push_back(std::move(elem.type));
                }

                ta.return_type = std::make_shared<TypeAnnotation>(parse_type_annotation());

                if (match(TokenType::Question)) {
                    ta.is_optional = true;
                }
                return ta;
            }
            else {
                // It is a tuple type

                // Special case: Single unnamed element in parens is just that type (grouping)
                // e.g. (Int) is same as Int
                if (elements.size() == 1 && !elements[0].label.has_value()) {
                    TypeAnnotation ta = std::move(elements[0].type);
                    if (match(TokenType::Question)) {
                        ta.is_optional = true;
                    }
                    return ta;
                }

                TypeAnnotation ta;
                ta.is_tuple_type = true;
                ta.name = "Tuple";

                for (auto& elem : elements) {
                    TupleTypeElement tte;
                    tte.label = elem.label;
                    tte.type = std::make_shared<TypeAnnotation>(std::move(elem.type));
                    ta.tuple_elements.push_back(std::move(tte));
                }

                if (match(TokenType::Question)) {
                    ta.is_optional = true;
                }
                return ta;
            }
        }

        // Simple type: TypeName or TypeName?
        const Token& name_tok = consume(TokenType::Identifier, "Expected type name.");
        TypeAnnotation ta;
        ta.name = std::string(name_tok.lexeme);
        ta.is_optional = false;

        if (match(TokenType::Less)) {
            do {
                ta.generic_args.push_back(parse_type_annotation());
            } while (match(TokenType::Comma));

            // Handle >> token for nested generics: Container<Box<Int>>
            if (check(TokenType::RightShift)) {
                // Consume >> and insert a virtual > token
                advance();

                Token virtual_greater;
                virtual_greater.type = TokenType::Greater;
                virtual_greater.lexeme = ">";
                virtual_greater.line = previous().line;

                tokens_.insert(tokens_.begin() + current_, virtual_greater);
            }
            else {
                consume(TokenType::Greater, "Expected '>' after generic arguments.");
            }
        }

        // Check for trailing '?' to mark optional
        if (match(TokenType::Question)) {
            ta.is_optional = true;
        }
        return ta;
    }

    std::vector<std::string> Parser::parse_generic_params() {
        std::vector<std::string> params;
        if (!match(TokenType::Less)) {
            return params;
        }

        do {
            const Token& param_tok = consume(TokenType::Identifier, "Expected generic parameter name.");
            params.push_back(std::string(param_tok.lexeme));
        } while (match(TokenType::Comma));

        consume(TokenType::Greater, "Expected '>' after generic parameter list.");
        return params;
    }

    std::vector<GenericConstraint> Parser::parse_generic_constraints(
        const std::vector<std::string>& generic_params) {
        std::vector<GenericConstraint> constraints;

        // Check for where clause
        if (!match(TokenType::Where)) {
            return constraints;
        }

        // Parse constraints: where T: Comparable, U: Hashable
        do {
            const Token& param_tok = consume(TokenType::Identifier, "Expected generic parameter name in constraint.");
            std::string param_name = std::string(param_tok.lexeme);

            // Verify this is a valid generic parameter
            bool valid = false;
            for (const auto& param : generic_params) {
                if (param == param_name) {
                    valid = true;
                    break;
                }
            }
            if (!valid) {
                error(param_tok, "Unknown generic parameter '" + param_name + "' in constraint");
            }

            consume(TokenType::Colon, "Expected ':' after generic parameter in constraint.");

            const Token& protocol_tok = consume(TokenType::Identifier, "Expected protocol name in constraint.");
            std::string protocol_name = std::string(protocol_tok.lexeme);

            GenericConstraint constraint;
            constraint.param_name = param_name;
            constraint.protocol_name = protocol_name;
            constraints.push_back(constraint);

        } while (match(TokenType::Comma));

        return constraints;
    }

    // ============================================================
    //  Statement parsers
    // ============================================================

    StmtPtr Parser::declaration() {
        skip_comments();
        std::vector<Attribute> attributes;
        if (looks_like_attribute_list()) {
            attributes = parse_attributes();
        }
        AccessLevel access_level = AccessLevel::Internal;
        bool has_access_modifier = false;
        if (match(TokenType::Public)) {
            access_level = AccessLevel::Public;
            has_access_modifier = true;
        }
        else if (match(TokenType::Private)) {
            access_level = AccessLevel::Private;
            has_access_modifier = true;
        }
        else if (match(TokenType::Internal)) {
            access_level = AccessLevel::Internal;
            has_access_modifier = true;
        }
        else if (match(TokenType::Fileprivate)) {
            access_level = AccessLevel::Fileprivate;
            has_access_modifier = true;
        }

        if (check(TokenType::Import)) {
            if (has_access_modifier) {
                error(peek(), "Access control modifiers cannot be applied to import statements.");
            }
            if (!attributes.empty()) {
                error(peek(), "Attributes cannot be applied to import statements.");
            }
            return import_declaration();
        }
        if (check(TokenType::Attribute)) {
            if (has_access_modifier) {
                error(peek(), "Access control modifiers cannot be applied to attribute declarations.");
            }
            auto stmt = attribute_declaration();
            stmt->attributes = std::move(attributes);
            return stmt;
        }
        if (check(TokenType::Var) || check(TokenType::Let)) {
            auto stmt = var_declaration();
            if (auto* var = dynamic_cast<VarDeclStmt*>(stmt.get())) {
                var->access_level = access_level;
            }
            else if (has_access_modifier) {
                error(previous(), "Access control modifier must precede a declaration.");
            }
            stmt->attributes = std::move(attributes);
            return stmt;
        }
        if (check(TokenType::Class)) {
            auto stmt = class_declaration(access_level);
            if (auto* decl = dynamic_cast<ClassDeclStmt*>(stmt.get())) {
            }
            stmt->attributes = std::move(attributes);
            return stmt;
        }
        if (check(TokenType::Struct)) {
            auto stmt = struct_declaration(access_level);
            if (auto* decl = dynamic_cast<StructDeclStmt*>(stmt.get())) {
            }
            stmt->attributes = std::move(attributes);
            return stmt;
        }
        if (check(TokenType::Enum)) {
            auto stmt = enum_declaration(access_level);
            if (auto* decl = dynamic_cast<EnumDeclStmt*>(stmt.get())) {
            }
            stmt->attributes = std::move(attributes);
            return stmt;
        }
        if (check(TokenType::Protocol)) {
            auto stmt = protocol_declaration(access_level);
            if (auto* decl = dynamic_cast<ProtocolDeclStmt*>(stmt.get())) {
            }
            stmt->attributes = std::move(attributes);
            return stmt;
        }
        if (check(TokenType::Extension)) {
            auto stmt = extension_declaration(access_level);
            if (auto* decl = dynamic_cast<ExtensionDeclStmt*>(stmt.get())) {
            }
            stmt->attributes = std::move(attributes);
            return stmt;
        }
        if (check(TokenType::Func)) {
            auto stmt = func_declaration();
            if (auto* func = dynamic_cast<FuncDeclStmt*>(stmt.get())) {
                func->access_level = access_level;
            }
            stmt->attributes = std::move(attributes);
            return stmt;
        }
        if (has_access_modifier) {
            error(peek(), "Access control modifier must precede a declaration.");
        }
        if (!attributes.empty()) {
            error(peek(), "Attributes must precede a declaration.");
        }
        return statement();
    }

    StmtPtr Parser::var_declaration() {
        // var x: Int? = expr
        // let x = expr
        // let (a, b) = tuple  -- Tuple destructuring
        bool is_let = check(TokenType::Let);
        uint32_t start_line = peek().line;
        advance();  // consume 'var' or 'let'

        // Check for tuple destructuring: let (a, b) = ...
        if (check(TokenType::LeftParen)) {
            return parse_tuple_destructuring(is_let, start_line);
        }

        auto stmt = parse_variable_decl(is_let);

        // Semicolons are optional (Swift-style)
        match(TokenType::Semicolon);
        return stmt;
    }

    std::unique_ptr<VarDeclStmt> Parser::parse_variable_decl(bool is_let) {
        const Token& name_tok = consume(TokenType::Identifier, "Expected variable name.");
        auto stmt = std::make_unique<VarDeclStmt>();
        stmt->line = name_tok.line;
        stmt->name = std::string(name_tok.lexeme);
        stmt->is_let = is_let;

        if (match(TokenType::Colon)) {
            stmt->type_annotation = parse_type_annotation();
        }

        // Check for computed property
        if (check(TokenType::LeftBrace)) {
            // This is a computed property
            stmt->is_computed = true;
            advance(); // consume '{'

            // Check if it's a read-only computed property shorthand
            // (no get/set keywords, just the body)
            if (!check(TokenType::Get) && !check(TokenType::Set) && !check(TokenType::RightBrace)) {
                // Read-only shorthand: var name: Type { body }
                auto getter_block = std::make_unique<BlockStmt>();
                getter_block->line = peek().line;

                // Parse statements until '}'
                while (!check(TokenType::RightBrace) && !is_at_end()) {
                    getter_block->statements.push_back(statement());
                }

                stmt->getter_body = std::move(getter_block);
            }
            else {
                // Parse getter and optional setter with keywords
                while (!check(TokenType::RightBrace) && !is_at_end()) {
                    if (match(TokenType::Get)) {
                        stmt->getter_body = block();
                    }
                    else if (match(TokenType::Set)) {
                        stmt->setter_body = block();
                    }
                    else {
                        error(peek(), "Expected 'get' or 'set' in computed property.");
                        break;
                    }
                }
            }

            consume(TokenType::RightBrace, "Expected '}' after computed property.");

            // Validate: computed property must have a getter
            if (!stmt->getter_body) {
                error(name_tok, "Computed property must have a getter.");
            }
        }
        else if (match(TokenType::Equal)) {
            // Regular stored property with initializer
            stmt->initializer = expression();

            // Check for property observers after initializer
            if (check(TokenType::LeftBrace)) {
                advance(); // consume '{'

                // Parse willSet and/or didSet
                while (!check(TokenType::RightBrace) && !is_at_end()) {
                    if (match(TokenType::WillSet)) {
                        stmt->will_set_body = block();
                    }
                    else if (match(TokenType::DidSet)) {
                        stmt->did_set_body = block();
                    }
                    else {
                        error(peek(), "Expected 'willSet' or 'didSet' in property observers.");
                        break;
                    }
                }

                consume(TokenType::RightBrace, "Expected '}' after property observers.");
            }
        }

        return stmt;
    }

    StmtPtr Parser::parse_tuple_destructuring(bool is_let, uint32_t line) {
        // Parse: (a, b) = expr  or  (x: a, y: b) = expr
        consume(TokenType::LeftParen, "Expected '(' for tuple destructuring.");

        auto stmt = std::make_unique<TupleDestructuringStmt>();
        stmt->line = line;
        stmt->is_let = is_let;

        // Parse binding patterns
        if (!check(TokenType::RightParen)) {
            do {
                TupleDestructuringElement elem;

                // Check for label: pattern (e.g., x: a) or wildcard (_)
                if (check(TokenType::Identifier)) {
                    const Token& first = advance();
                    // Check for wildcard pattern
                    if (first.lexeme == "_") {
                        elem.name = "_";
                    }
                    else if (check(TokenType::Colon)) {
                        // This is label: name
                        advance();  // consume ':'
                        elem.label = std::string(first.lexeme);
                        const Token& name_tok = consume(TokenType::Identifier, "Expected variable name after label.");
                        elem.name = std::string(name_tok.lexeme);
                    }
                    else {
                        // Just a name without label
                        elem.name = std::string(first.lexeme);
                    }
                }
                else {
                    error(peek(), "Expected variable name or underscore in tuple destructuring.");
                }

                stmt->bindings.push_back(std::move(elem));
            } while (match(TokenType::Comma));
        }

        consume(TokenType::RightParen, "Expected ')' after tuple destructuring pattern.");
        consume(TokenType::Equal, "Expected '=' after tuple destructuring pattern.");

        stmt->initializer = expression();

        // Semicolons are optional
        match(TokenType::Semicolon);

        return stmt;
    }

    StmtPtr Parser::import_declaration() {
        const Token& import_tok = advance();  // consume 'import'

        // Expect a string literal or identifier: import "path/to/module.ss" or import ModuleName
        const Token* path_tok = nullptr;
        if (check(TokenType::String) || check(TokenType::Identifier)) {
            path_tok = &advance();
        } else {
            error(peek(), "Expected string literal or identifier after 'import'.");
        }

        auto stmt = std::make_unique<ImportStmt>();
        stmt->line = import_tok.line;
        stmt->module_path = std::string(path_tok->lexeme);

        // Remove surrounding quotes if present
        if (!stmt->module_path.empty()) {
            if (stmt->module_path.front() == '"' && stmt->module_path.back() == '"') {
                stmt->module_path = stmt->module_path.substr(1, stmt->module_path.length() - 2);
            }
        }

        // Semicolons are optional
        match(TokenType::Semicolon);
        return stmt;
    }

    StmtPtr Parser::attribute_declaration() {
        advance(); // consume 'attribute'
        const Token& name_tok = consume(TokenType::Identifier, "Expected attribute name.");
        auto stmt = std::make_unique<AttributeDeclStmt>();
        stmt->line = name_tok.line;
        stmt->name = std::string(name_tok.lexeme);

        if (match(TokenType::LeftParen)) {
            stmt->params = parse_param_list(false);
        }

        match(TokenType::Semicolon);
        return stmt;
    }

    StmtPtr Parser::func_declaration() {
        advance();  // consume 'func'
        auto is_operator_name = [](TokenType type) {
            return TokenUtils::is_binary_operator(type) ||
                TokenUtils::is_unary_operator(type) ||
                TokenUtils::is_comparison_operator(type);
            };

        const Token& name_tok = ([&]() -> const Token& {
            if (check(TokenType::Identifier) || check(TokenType::Init) || is_operator_name(peek().type)) {
                return advance();
            }
            error(peek(), "Expected function name.");
            })();

        auto stmt = std::make_unique<FuncDeclStmt>();
        stmt->line = name_tok.line;
        stmt->name = std::string(name_tok.lexeme);
        stmt->generic_params = parse_generic_params();

        // Parameter list
        consume(TokenType::LeftParen, "Expected '(' after function name.");
        stmt->params = parse_param_list(true);

        // Optional return type: -> Type
        if (match(TokenType::Arrow)) {
            stmt->return_type = parse_type_annotation();
        }

        // Parse generic constraints: where T: Comparable
        stmt->generic_constraints = parse_generic_constraints(stmt->generic_params);

        // Body
        stmt->body = block();
        return stmt;
    }

    StmtPtr Parser::class_declaration(AccessLevel access_level) {
        advance(); // consume 'class'
        const Token& name_tok = consume(TokenType::Identifier, "Expected class name.");

        auto stmt = std::make_unique<ClassDeclStmt>();
        stmt->line = name_tok.line;
        stmt->name = std::string(name_tok.lexeme);
        stmt->generic_params = parse_generic_params();
        stmt->access_level = access_level;

        // Parse superclass and protocol conformances: class MyClass: SuperClass, Protocol1, Protocol2 { ... }
        if (match(TokenType::Colon)) {
            // First one could be superclass or protocol
            const Token& first_tok = consume(TokenType::Identifier, "Expected superclass or protocol name after ':' in class declaration.");
            std::string first_name = std::string(first_tok.lexeme);

            // Heuristic: If there are more names, treat first as superclass
            // In a real implementation, you'd need type information to distinguish
            // For now, we'll treat the first as superclass if it starts with uppercase (Swift convention)
            // and subsequent ones as protocols
            bool has_superclass = !first_name.empty() && std::isupper(first_name[0]);

            if (has_superclass && !check(TokenType::Comma)) {
                // Only superclass, no protocols
                stmt->superclass_name = first_name;
            }
            else if (has_superclass && check(TokenType::Comma)) {
                // Superclass followed by protocols
                stmt->superclass_name = first_name;
                while (match(TokenType::Comma)) {
                    const Token& protocol_tok = consume(TokenType::Identifier, "Expected protocol name.");
                    stmt->protocol_conformances.push_back(std::string(protocol_tok.lexeme));
                }
            }
            else {
                // Only protocols (no superclass)
                stmt->protocol_conformances.push_back(first_name);
                while (match(TokenType::Comma)) {
                    const Token& protocol_tok = consume(TokenType::Identifier, "Expected protocol name.");
                    stmt->protocol_conformances.push_back(std::string(protocol_tok.lexeme));
                }
            }
        }

        consume(TokenType::LeftBrace, "Expected '{' after class name.");
        while (!check(TokenType::RightBrace) && !is_at_end()) {
            std::vector<Attribute> attributes;
            if (looks_like_attribute_list()) {
                attributes = parse_attributes();
            }
            // Check for access control modifiers
            AccessLevel access_level = AccessLevel::Internal;  // Default
            if (match(TokenType::Public)) {
                access_level = AccessLevel::Public;
            }
            else if (match(TokenType::Private)) {
                access_level = AccessLevel::Private;
            }
            else if (match(TokenType::Internal)) {
                access_level = AccessLevel::Internal;
            }
            else if (match(TokenType::Fileprivate)) {
                access_level = AccessLevel::Fileprivate;
            }

            // Check for static modifier
            bool is_static = false;
            if (match(TokenType::Static)) {
                is_static = true;
            }

            bool is_override = false;
            if (match(TokenType::Override)) {
                is_override = true;
                if (is_static) {
                    error(previous(), "'static' cannot be combined with 'override'.");
                }
            }

            if (check(TokenType::Deinit)) {
                if (is_override) {
                    error(previous(), "'override' cannot be used with 'deinit'.");
                }
                if (is_static) {
                    error(previous(), "'static' cannot be used with 'deinit'.");
                }
                advance(); // consume 'deinit'
                if (stmt->deinit_body) {
                    error(previous(), "Class can only have one deinit.");
                }
                stmt->deinit_body = block();
                continue;
            }

            if (check(TokenType::Init)) {
                if (is_static) {
                    error(previous(), "'static' cannot be used with 'init'.");
                }
                advance(); // consume 'init'

                auto method = std::make_unique<FuncDeclStmt>();
                method->line = previous().line;
                method->name = "init";
                method->is_override = is_override;
                method->is_static = false;
                method->access_level = access_level;
                method->attributes = std::move(attributes);


                // Optional parameter list: allow both `init { ... }` and `init(...) { ... }`
                if (match(TokenType::LeftParen)) {
                    method->params = parse_param_list(true);
                }

                method->body = block();
                stmt->methods.push_back(std::move(method));
                continue;
            }

            if (check(TokenType::Func)) {
                auto method = std::unique_ptr<FuncDeclStmt>(static_cast<FuncDeclStmt*>(func_declaration().release()));
                method->is_override = is_override;
                method->is_static = is_static;
                method->access_level = access_level;
                method->attributes = std::move(attributes);
                stmt->methods.push_back(std::move(method));
                continue;
            }
            else if (is_override) {
                error(previous(), "'override' must precede a method declaration.");
            }

            // Check for lazy modifier
            bool is_lazy = false;
            if (match(TokenType::Lazy)) {
                is_lazy = true;
                if (is_static) {
                    error(previous(), "'static' cannot be combined with 'lazy'.");
                }
            }

            if (check(TokenType::Var) || check(TokenType::Let)) {
                bool is_let = check(TokenType::Let);
                if (is_lazy && is_let) {
                    error(previous(), "'lazy' cannot be used with 'let'.");
                }
                advance();
                auto property = parse_variable_decl(is_let);
                property->access_level = access_level;
                property->is_lazy = is_lazy;
                property->is_static = is_static;
                property->attributes = std::move(attributes);
                match(TokenType::Semicolon);
                stmt->properties.push_back(std::move(property));
                continue;
            }

            if (is_lazy) {
                error(previous(), "'lazy' must precede a variable declaration.");
            }

            if (is_static) {
                error(previous(), "'static' must precede a method or property declaration.");
            }

            if (!attributes.empty()) {
                error(previous(), "Attributes must precede a declaration.");
            }

            error(peek(), "Expected method or property declaration inside class.");
        }
        consume(TokenType::RightBrace, "Expected '}' after class body.");
        return stmt;
    }

    StmtPtr Parser::struct_declaration(AccessLevel access_level) {
        advance(); // consume 'struct'
        const Token& name_tok = consume(TokenType::Identifier, "Expected struct name.");

        auto stmt = std::make_unique<StructDeclStmt>();
        stmt->line = name_tok.line;
        stmt->name = std::string(name_tok.lexeme);
        stmt->generic_params = parse_generic_params();
        stmt->access_level = access_level;

        // Parse protocol conformances: struct MyStruct: Protocol1, Protocol2 { ... }
        if (match(TokenType::Colon)) {
            do {
                const Token& protocol_tok = consume(TokenType::Identifier, "Expected protocol name.");
                stmt->protocol_conformances.push_back(std::string(protocol_tok.lexeme));
            } while (match(TokenType::Comma));
        }

        // Parse generic constraints: where T: Comparable
        stmt->generic_constraints = parse_generic_constraints(stmt->generic_params);

        consume(TokenType::LeftBrace, "Expected '{' after struct name.");

        while (!check(TokenType::RightBrace) && !is_at_end()) {
            std::vector<Attribute> attributes;
            if (looks_like_attribute_list()) {
                attributes = parse_attributes();
            }
            // Check for access control modifiers
            AccessLevel access_level = AccessLevel::Internal;  // Default
            if (match(TokenType::Public)) {
                access_level = AccessLevel::Public;
            }
            else if (match(TokenType::Private)) {
                access_level = AccessLevel::Private;
            }
            else if (match(TokenType::Internal)) {
                access_level = AccessLevel::Internal;
            }
            else if (match(TokenType::Fileprivate)) {
                access_level = AccessLevel::Fileprivate;
            }

            // Check for static modifier
            bool is_static = false;
            if (match(TokenType::Static)) {
                is_static = true;
            }

            // Check for mutating modifier (cannot be both static and mutating)
            bool is_mutating = false;
            if (match(TokenType::Mutating)) {
                if (is_static) {
                    error(previous(), "Static methods cannot be mutating.");
                }
                is_mutating = true;
            }

            // Method declaration: [access] [static] [mutating] func name(...) -> Type { ... }
            if (check(TokenType::Func)) {
                advance(); // consume 'func'
                auto is_operator_name = [](TokenType type) {
                    return TokenUtils::is_binary_operator(type) ||
                        TokenUtils::is_unary_operator(type) ||
                        TokenUtils::is_comparison_operator(type);
                    };
                const Token& method_name = ([&]() -> const Token& {
                    if (check(TokenType::Identifier) || is_operator_name(peek().type)) {
                        return advance();
                    }
                    error(peek(), "Expected method name.");
                    })();

                auto method = std::make_unique<StructMethodDecl>();
                method->name = std::string(method_name.lexeme);
                method->line = method_name.line;
                method->is_mutating = is_mutating;
                method->is_static = is_static;
                method->access_level = access_level;
                method->attributes = std::move(attributes);
                method->generic_params = parse_generic_params();

                // Parameter list
                consume(TokenType::LeftParen, "Expected '(' after method name.");
                method->params = parse_param_list(true);

                // Optional return type: -> Type
                if (match(TokenType::Arrow)) {
                    method->return_type = parse_type_annotation();
                }

                // Body
                method->body = block();
                stmt->methods.push_back(std::move(method));
                continue;
            }

            // init declaration (not mutating)
            if (check(TokenType::Init)) {
                if (is_mutating) {
                    error(previous(), "'mutating' cannot be used with 'init'.");
                }
                advance(); // consume 'init'

                auto init_method = std::make_unique<FuncDeclStmt>();
                init_method->line = previous().line;
                init_method->name = "init";
                init_method->attributes = std::move(attributes);

                // Parameter list
                consume(TokenType::LeftParen, "Expected '(' after 'init'.");
                init_method->params = parse_param_list(true);

                // Body
                init_method->body = block();
                stmt->initializers.push_back(std::move(init_method));
                continue;
            }

            // Property declaration: [access] [static] var/let name: Type [= initializer]
            if (check(TokenType::Var) || check(TokenType::Let)) {
                if (is_mutating) {
                    error(previous(), "'mutating' can only be used with methods.");
                }
                bool is_let = check(TokenType::Let);
                advance();
                auto property = parse_variable_decl(is_let);
                property->is_static = is_static;  // Mark as static if modifier was present
                property->access_level = access_level;  // Set access level
                property->attributes = std::move(attributes);
                match(TokenType::Semicolon);
                stmt->properties.push_back(std::move(property));
                continue;
            }

            if (is_static) {
                error(previous(), "'static' must precede a method or property declaration.");
            }

            if (access_level != AccessLevel::Internal) {
                error(previous(), "Access control modifier must precede a declaration.");
            }

            if (is_mutating) {
                error(previous(), "'mutating' must precede a method declaration.");
            }

            if (!attributes.empty()) {
                error(previous(), "Attributes must precede a declaration.");
            }

            error(peek(), "Expected method or property declaration inside struct.");
        }

        consume(TokenType::RightBrace, "Expected '}' after struct body.");
        return stmt;
    }

    StmtPtr Parser::enum_declaration(AccessLevel access_level) {
        advance(); // consume 'enum'
        const Token& name_tok = consume(TokenType::Identifier, "Expected enum name.");

        auto stmt = std::make_unique<EnumDeclStmt>();
        stmt->line = name_tok.line;
        stmt->name = std::string(name_tok.lexeme);
        stmt->generic_params = parse_generic_params();
        stmt->access_level = access_level;

        // Optional raw type: enum Status: Int { ... }
        if (match(TokenType::Colon)) {
            stmt->raw_type = parse_type_annotation();
        }

        consume(TokenType::LeftBrace, "Expected '{' after enum name.");

        while (!check(TokenType::RightBrace) && !is_at_end()) {
            std::vector<Attribute> attributes;
            if (looks_like_attribute_list()) {
                attributes = parse_attributes();
            }
            // case declaration: case north, case south = 1
            if (match(TokenType::Case)) {
                do {
                    const Token& case_name = consume(TokenType::Identifier, "Expected case name.");
                    EnumCaseDecl case_decl;
                    case_decl.name = std::string(case_name.lexeme);

                    // Optional raw value: case high = 3
                    if (match(TokenType::Equal)) {
                        // Parse the raw value (integer, string, etc.)
                        ExprPtr value_expr = primary();

                        // Convert expression to Value if it's a literal
                        if (auto* lit = dynamic_cast<LiteralExpr*>(value_expr.get())) {
                            case_decl.raw_value = lit->value;
                        }
                    }

                    // Optional associated values: case success(message: String)
                    if (match(TokenType::LeftParen)) {
                        if (!check(TokenType::RightParen)) {
                            do {
                                const Token& param_name = consume(TokenType::Identifier, "Expected parameter name.");
                                consume(TokenType::Colon, "Expected ':' after parameter name.");
                                TypeAnnotation param_type = parse_type_annotation();
                                case_decl.associated_values.emplace_back(std::string(param_name.lexeme), param_type);
                            } while (match(TokenType::Comma));
                        }
                        consume(TokenType::RightParen, "Expected ')' after associated values.");
                    }

                    stmt->cases.push_back(std::move(case_decl));
                } while (match(TokenType::Comma));

                // Optional semicolon or newline
                match(TokenType::Semicolon);
                continue;
            }

            // Method declaration: func describe() -> String { ... }
            if (check(TokenType::Func)) {
                advance(); // consume 'func'
                auto is_operator_name = [](TokenType type) {
                    return TokenUtils::is_binary_operator(type) ||
                        TokenUtils::is_unary_operator(type) ||
                        TokenUtils::is_comparison_operator(type);
                    };
                const Token& method_name = ([&]() -> const Token& {
                    if (check(TokenType::Identifier) || is_operator_name(peek().type)) {
                        return advance();
                    }
                    error(peek(), "Expected method name.");
                    })();

                auto method = std::make_unique<StructMethodDecl>();
                method->name = std::string(method_name.lexeme);
                method->line = method_name.line;
                method->attributes = std::move(attributes);
                method->generic_params = parse_generic_params();

                // Parameter list
                consume(TokenType::LeftParen, "Expected '(' after method name.");
                method->params = parse_param_list(true);

                // Optional return type: -> Type
                if (match(TokenType::Arrow)) {
                    method->return_type = parse_type_annotation();
                }

                // Body
                method->body = block();
                stmt->methods.push_back(std::move(method));
                continue;
            }

            // Computed property: var description: String { ... }
            if (check(TokenType::Var)) {
                advance(); // consume 'var'
                const Token& prop_name = consume(TokenType::Identifier, "Expected property name.");
                consume(TokenType::Colon, "Expected ':' after property name.");
                TypeAnnotation prop_type = parse_type_annotation();

                auto method = std::make_unique<StructMethodDecl>();
                method->name = std::string(prop_name.lexeme);
                method->line = prop_name.line;
                method->return_type = prop_type;
                method->is_computed_property = true;  // Mark as computed property
                method->attributes = std::move(attributes);

                // Parse the getter body
                // Enum computed properties use read-only shorthand: var name: Type { body }
                consume(TokenType::LeftBrace, "Expected '{' after computed property type.");

                auto getter_block = std::make_unique<BlockStmt>();
                getter_block->line = peek().line;

                // Parse statements until '}'
                while (!check(TokenType::RightBrace) && !is_at_end()) {
                    getter_block->statements.push_back(statement());
                }

                consume(TokenType::RightBrace, "Expected '}' after computed property body.");

                method->body = std::move(getter_block);
                stmt->methods.push_back(std::move(method));
                continue;
            }

            if (!attributes.empty()) {
                error(previous(), "Attributes must precede a declaration.");
            }

            error(peek(), "Expected 'case', 'func', or 'var' inside enum.");
        }

        // If no raw type was specified but cases have raw values, infer the raw type
        if (!stmt->raw_type.has_value() && !stmt->cases.empty()) {
            bool has_raw_values = false;
            for (const auto& case_decl : stmt->cases) {
                if (case_decl.raw_value.has_value()) {
                    has_raw_values = true;
                    // Infer raw type from the first raw value
                    TypeAnnotation raw_type;
                    if (case_decl.raw_value->is_int()) {
                        raw_type.name = "Int";
                    }
                    else if (case_decl.raw_value->is_float()) {
                        raw_type.name = "Float";
                    }
                    else if (case_decl.raw_value->is_bool()) {
                        raw_type.name = "Bool";
                    }
                    else {
                        raw_type.name = "String";
                    }
                    raw_type.is_optional = false;
                    stmt->raw_type = raw_type;
                    break;
                }
            }
        }

        consume(TokenType::RightBrace, "Expected '}' after enum body.");
        return stmt;
    }

    StmtPtr Parser::protocol_declaration(AccessLevel access_level) {
        advance(); // consume 'protocol'
        const Token& name_tok = consume(TokenType::Identifier, "Expected protocol name.");

        auto stmt = std::make_unique<ProtocolDeclStmt>();
        stmt->line = name_tok.line;
        stmt->name = std::string(name_tok.lexeme);
        stmt->generic_params = parse_generic_params();
        stmt->access_level = access_level;

        // Optional protocol inheritance: protocol MyProtocol: BaseProtocol1, BaseProtocol2 { ... }
        if (match(TokenType::Colon)) {
            do {
                const Token& inherited_tok = consume(TokenType::Identifier, "Expected inherited protocol name.");
                stmt->inherited_protocols.push_back(std::string(inherited_tok.lexeme));
            } while (match(TokenType::Comma));
        }

        consume(TokenType::LeftBrace, "Expected '{' after protocol name.");

        while (!check(TokenType::RightBrace) && !is_at_end()) {
            std::vector<Attribute> attributes;
            if (looks_like_attribute_list()) {
                attributes = parse_attributes();
            }
            // Method requirement: func methodName(param: Type) -> ReturnType
            if (match(TokenType::Func)) {
                auto is_operator_name = [](TokenType type) {
                    return TokenUtils::is_binary_operator(type) ||
                        TokenUtils::is_unary_operator(type) ||
                        TokenUtils::is_comparison_operator(type);
                    };
                const Token& method_name = ([&]() -> const Token& {
                    if (check(TokenType::Identifier) || is_operator_name(peek().type)) {
                        return advance();
                    }
                    error(peek(), "Expected method name.");
                    })();

                ProtocolMethodRequirement method_req;
                method_req.name = std::string(method_name.lexeme);
                method_req.generic_params = parse_generic_params();
                method_req.attributes = std::move(attributes);

                // Parameter list
                consume(TokenType::LeftParen, "Expected '(' after method name.");
                method_req.params = parse_param_list(false);

                // Optional return type: -> Type
                if (match(TokenType::Arrow)) {
                    method_req.return_type = parse_type_annotation();
                }

                // Check for mutating modifier (for value types)
                // Note: In Swift, mutating is specified before func
                // Here we allow it after for simplicity

                stmt->method_requirements.push_back(std::move(method_req));
                match(TokenType::Semicolon); // Optional semicolon
                continue;
            }

            // Property requirement: var propertyName: Type { get set } or { get }
            if (check(TokenType::Var) || check(TokenType::Let)) {
                bool is_let = check(TokenType::Let);
                advance(); // consume 'var' or 'let'
                const Token& prop_name = consume(TokenType::Identifier, "Expected property name.");
                consume(TokenType::Colon, "Expected ':' after property name.");
                TypeAnnotation prop_type = parse_type_annotation();

                ProtocolPropertyRequirement prop_req;
                prop_req.name = std::string(prop_name.lexeme);
                prop_req.type = prop_type;
                prop_req.has_getter = true;
                prop_req.attributes = std::move(attributes);

                // Parse { get } or { get set }
                consume(TokenType::LeftBrace, "Expected '{' for property accessor specification.");
                consume(TokenType::Get, "Expected 'get' in property requirement.");

                if (match(TokenType::Set)) {
                    if (is_let) {
                        error(previous(), "Let properties in protocols cannot declare a setter.");
                    }
                    prop_req.has_setter = true;
                }

                consume(TokenType::RightBrace, "Expected '}' after property accessor specification.");

                stmt->property_requirements.push_back(std::move(prop_req));
                match(TokenType::Semicolon); // Optional semicolon
                continue;
            }

            // Mutating method requirement: mutating func methodName(...)
            if (match(TokenType::Mutating)) {
                if (!match(TokenType::Func)) {
                    error(previous(), "Expected 'func' after 'mutating'.");
                }

                auto is_operator_name = [](TokenType type) {
                    return TokenUtils::is_binary_operator(type) ||
                        TokenUtils::is_unary_operator(type) ||
                        TokenUtils::is_comparison_operator(type);
                    };
                const Token& method_name = ([&]() -> const Token& {
                    if (check(TokenType::Identifier) || is_operator_name(peek().type)) {
                        return advance();
                    }
                    error(peek(), "Expected method name.");
                    })();

                ProtocolMethodRequirement method_req;
                method_req.name = std::string(method_name.lexeme);
                method_req.is_mutating = true;
                method_req.generic_params = parse_generic_params();
                method_req.attributes = std::move(attributes);

                // Parameter list
                consume(TokenType::LeftParen, "Expected '(' after method name.");
                method_req.params = parse_param_list(false);

                // Optional return type: -> Type
                if (match(TokenType::Arrow)) {
                    method_req.return_type = parse_type_annotation();
                }

                stmt->method_requirements.push_back(std::move(method_req));
                match(TokenType::Semicolon); // Optional semicolon
                continue;
            }

            if (!attributes.empty()) {
                error(previous(), "Attributes must precede a declaration.");
            }

            error(peek(), "Expected method or property requirement inside protocol.");
        }

        consume(TokenType::RightBrace, "Expected '}' after protocol body.");
        return stmt;
    }

    StmtPtr Parser::extension_declaration(AccessLevel access_level) {
        advance(); // consume 'extension'
        const Token& type_tok = consume(TokenType::Identifier, "Expected type name after 'extension'.");

        auto stmt = std::make_unique<ExtensionDeclStmt>();
        stmt->line = type_tok.line;
        stmt->extended_type = std::string(type_tok.lexeme);
        stmt->access_level = access_level;

        // Parse protocol conformances: extension String: Equatable, Hashable { ... }
        if (match(TokenType::Colon)) {
            do {
                const Token& protocol_tok = consume(TokenType::Identifier, "Expected protocol name.");
                stmt->protocol_conformances.push_back(std::string(protocol_tok.lexeme));
            } while (match(TokenType::Comma));
        }

        consume(TokenType::LeftBrace, "Expected '{' after extension declaration.");

        while (!check(TokenType::RightBrace) && !is_at_end()) {
            std::vector<Attribute> attributes;
            if (looks_like_attribute_list()) {
                attributes = parse_attributes();
            }
            // Check for access control modifiers
            AccessLevel access_level = AccessLevel::Internal;  // Default
            if (match(TokenType::Public)) {
                access_level = AccessLevel::Public;
            }
            else if (match(TokenType::Private)) {
                access_level = AccessLevel::Private;
            }
            else if (match(TokenType::Internal)) {
                access_level = AccessLevel::Internal;
            }
            else if (match(TokenType::Fileprivate)) {
                access_level = AccessLevel::Fileprivate;
            }

            // Check for static modifier
            bool is_static = false;
            if (match(TokenType::Static)) {
                is_static = true;
            }

            // Check for mutating modifier
            bool is_mutating = false;
            if (match(TokenType::Mutating)) {
                is_mutating = true;
                if (is_static) {
                    error(previous(), "Static methods cannot be mutating.");
                }
            }

            // Method declaration: [access] [static] [mutating] func name(...) -> Type { ... }
            if (check(TokenType::Func)) {
                advance(); // consume 'func'
                auto is_operator_name = [](TokenType type) {
                    return TokenUtils::is_binary_operator(type) ||
                        TokenUtils::is_unary_operator(type) ||
                        TokenUtils::is_comparison_operator(type);
                    };
                const Token& method_name = ([&]() -> const Token& {
                    if (check(TokenType::Identifier) || is_operator_name(peek().type)) {
                        return advance();
                    }
                    error(peek(), "Expected method name.");
                    })();

                auto method = std::make_unique<StructMethodDecl>();
                method->name = std::string(method_name.lexeme);
                method->line = method_name.line;
                method->is_mutating = is_mutating;
                method->is_static = is_static;
                method->access_level = access_level;
                method->attributes = std::move(attributes);
                method->generic_params = parse_generic_params();

                // Parameter list
                consume(TokenType::LeftParen, "Expected '(' after method name.");
                method->params = parse_param_list(true);

                // Optional return type: -> Type
                if (match(TokenType::Arrow)) {
                    method->return_type = parse_type_annotation();
                }

                // Body
                method->body = block();
                stmt->methods.push_back(std::move(method));
                continue;
            }

            // Computed property: [static] var description: String { ... }
            if (check(TokenType::Var) || check(TokenType::Let)) {
                if (is_mutating) {
                    error(previous(), "'mutating' can only be used with methods.");
                }
                bool is_let = check(TokenType::Let);
                advance(); // consume 'var' or 'let'
                const Token& prop_name = consume(TokenType::Identifier, "Expected property name.");

                auto computed_prop = std::make_unique<StructMethodDecl>();
                computed_prop->name = std::string(prop_name.lexeme);
                computed_prop->line = prop_name.line;
                computed_prop->is_computed_property = true;
                computed_prop->is_static = is_static;
                computed_prop->attributes = std::move(attributes);

                // Type annotation
                if (match(TokenType::Colon)) {
                    computed_prop->return_type = parse_type_annotation();
                }

                // Body: { return ... } or { get { ... } set { ... } }
                computed_prop->body = block();
                stmt->methods.push_back(std::move(computed_prop));
                match(TokenType::Semicolon);
                continue;
            }

            if (is_mutating) {
                error(previous(), "'mutating' must precede a method declaration.");
            }

            if (is_static) {
                error(previous(), "'static' must precede a method or property declaration.");
            }

            if (!attributes.empty()) {
                error(previous(), "Attributes must precede a declaration.");
            }

            error(peek(), "Expected method or computed property declaration inside extension.");
        }

        consume(TokenType::RightBrace, "Expected '}' after extension body.");
        return stmt;
    }

    StmtPtr Parser::statement() {
        if (check(TokenType::If)) return if_statement();
        if (check(TokenType::Guard)) return guard_statement();
        if (check(TokenType::While)) return while_statement();
        if (check(TokenType::Repeat)) return repeat_while_statement();
        if (check(TokenType::For)) return for_in_statement();
        if (check(TokenType::Switch)) return switch_statement();
        if (check(TokenType::Break)) return break_statement();
        if (check(TokenType::Continue)) return continue_statement();
        if (check(TokenType::Return)) return return_statement();
        if (check(TokenType::Throw)) return throw_statement();
        if (check(TokenType::LeftBrace)) {
            auto blk = block();
            return blk;
        }

        // Check for print(...) as a built-in statement
        if (check(TokenType::Identifier) && peek().lexeme == "print") {
            return print_statement();
        }

        return expression_statement();
    }

    StmtPtr Parser::if_statement() {
        const Token& if_tok = advance();  // consume 'if'

        // Check for 'if let' binding
        if (check(TokenType::Let)) {
            advance();  // consume 'let'
            const Token& binding = consume(TokenType::Identifier, "Expected binding name after 'if let'.");
            consume(TokenType::Equal, "Expected '=' after binding name in 'if let'.");
            ExprPtr opt_expr = expression();

            auto stmt = std::make_unique<IfLetStmt>();
            stmt->line = if_tok.line;
            stmt->binding_name = std::string(binding.lexeme);
            stmt->optional_expr = std::move(opt_expr);
            stmt->then_branch = block();

            if (match(TokenType::Else)) {
                if (check(TokenType::If)) {
                    stmt->else_branch = if_statement();
                }
                else {
                    stmt->else_branch = block();
                }
            }
            return stmt;
        }

        // Regular 'if'
        ExprPtr condition = expression();
        auto stmt = std::make_unique<IfStmt>();
        stmt->line = if_tok.line;
        stmt->condition = std::move(condition);
        stmt->then_branch = block();

        if (match(TokenType::Else)) {
            if (check(TokenType::If)) {
                stmt->else_branch = if_statement();
            }
            else {
                stmt->else_branch = block();
            }
        }
        return stmt;
    }

    StmtPtr Parser::guard_statement() {
        const Token& guard_tok = advance();  // consume 'guard'

        // guard let x = expr else { ... }
        consume(TokenType::Let, "Expected 'let' after 'guard'.");
        const Token& binding = consume(TokenType::Identifier, "Expected binding name after 'guard let'.");
        consume(TokenType::Equal, "Expected '=' after binding name in 'guard let'.");
        ExprPtr opt_expr = expression();

        consume(TokenType::Else, "Expected 'else' after guard condition.");

        auto stmt = std::make_unique<GuardLetStmt>();
        stmt->line = guard_tok.line;
        stmt->binding_name = std::string(binding.lexeme);
        stmt->optional_expr = std::move(opt_expr);
        stmt->else_branch = block();
        return stmt;
    }

    StmtPtr Parser::while_statement() {
        const Token& while_tok = advance();  // consume 'while'
        ExprPtr condition = expression();

        auto stmt = std::make_unique<WhileStmt>();
        stmt->line = while_tok.line;
        stmt->condition = std::move(condition);
        stmt->body = block();
        return stmt;
    }

    StmtPtr Parser::repeat_while_statement() {
        const Token& repeat_tok = advance();  // consume 'repeat'

        auto stmt = std::make_unique<RepeatWhileStmt>();
        stmt->line = repeat_tok.line;
        stmt->body = block();

        consume(TokenType::While, "Expected 'while' after repeat body.");
        stmt->condition = expression();

        return stmt;
    }

    StmtPtr Parser::for_in_statement() {
        const Token& for_tok = advance();  // consume 'for'
        const Token& var_tok = consume(TokenType::Identifier, "Expected variable name after 'for'.");
        consume(TokenType::In, "Expected 'in' after for loop variable.");

        // Parse range expression
        ExprPtr iterable = expression();

        auto stmt = std::make_unique<ForInStmt>();
        stmt->line = for_tok.line;
        stmt->variable = std::string(var_tok.lexeme);
        stmt->iterable = std::move(iterable);

        // Optional where clause
        if (match(TokenType::Where)) {
            stmt->where_condition = expression();
        }

        stmt->body = block();
        return stmt;
    }

    StmtPtr Parser::break_statement() {
        const Token& break_tok = advance();  // consume 'break'

        auto stmt = std::make_unique<BreakStmt>();
        stmt->line = break_tok.line;

        match(TokenType::Semicolon);
        return stmt;
    }

    StmtPtr Parser::continue_statement() {
        const Token& continue_tok = advance();  // consume 'continue'

        auto stmt = std::make_unique<ContinueStmt>();
        stmt->line = continue_tok.line;

        match(TokenType::Semicolon);
        return stmt;
    }

    StmtPtr Parser::return_statement() {
        const Token& ret_tok = advance();  // consume 'return'

        auto stmt = std::make_unique<ReturnStmt>();
        stmt->line = ret_tok.line;

        // Return value is optional
        if (!check(TokenType::RightBrace) && !check(TokenType::Semicolon) && !is_at_end()) {
            stmt->value = expression();
        }

        match(TokenType::Semicolon);
        return stmt;
    }

    StmtPtr Parser::throw_statement() {
        const Token& throw_tok = advance();  // consume 'throw'

        auto stmt = std::make_unique<ThrowStmt>();
        stmt->line = throw_tok.line;

        // throw requires a value
        if (check(TokenType::RightBrace) || check(TokenType::Semicolon) || is_at_end()) {
            error(throw_tok, "Expected value after 'throw'.");
        }

        stmt->value = expression();
        match(TokenType::Semicolon);
        return stmt;
    }

    StmtPtr Parser::switch_statement() {
        const Token& switch_tok = advance();  // consume 'switch'

        ExprPtr value = expression();

        consume(TokenType::LeftBrace, "Expected '{' after switch value.");

        auto stmt = std::make_unique<SwitchStmt>();
        stmt->line = switch_tok.line;
        stmt->value = std::move(value);

        // Parse case clauses
        while (!check(TokenType::RightBrace) && !is_at_end()) {
            CaseClause clause;

            if (match(TokenType::Case)) {
                // Parse case patterns: case 1, 2, 3:
                do {
                    clause.patterns.push_back(parse_pattern());
                } while (match(TokenType::Comma));

                consume(TokenType::Colon, "Expected ':' after case pattern.");

            }
            else if (match(TokenType::Default)) {
                clause.is_default = true;
                consume(TokenType::Colon, "Expected ':' after 'default'.");

            }
            else {
                error(peek(), "Expected 'case' or 'default' in switch statement.");
            }

            // Parse statements until next case/default/}
            while (!check(TokenType::Case) && !check(TokenType::Default) &&
                !check(TokenType::RightBrace) && !is_at_end()) {
                clause.statements.push_back(declaration());
            }

            stmt->cases.push_back(std::move(clause));
        }

        consume(TokenType::RightBrace, "Expected '}' after switch cases.");

        return stmt;
    }

    StmtPtr Parser::print_statement() {
        const Token& print_tok = advance();  // consume 'print' identifier

        consume(TokenType::LeftParen, "Expected '(' after 'print'.");
        ExprPtr expr = expression();
        consume(TokenType::RightParen, "Expected ')' after print argument.");

        auto stmt = std::make_unique<PrintStmt>(std::move(expr));
        stmt->line = print_tok.line;

        match(TokenType::Semicolon);
        return stmt;
    }

    std::unique_ptr<BlockStmt> Parser::block() {
        consume(TokenType::LeftBrace, "Expected '{'.");

        auto blk = std::make_unique<BlockStmt>();
        blk->line = previous().line;

        while (!check(TokenType::RightBrace) && !is_at_end()) {
            skip_comments();
            if (check(TokenType::RightBrace) || is_at_end()) break;
            blk->statements.push_back(declaration());
        }

        consume(TokenType::RightBrace, "Expected '}'.");
        return blk;
    }

    StmtPtr Parser::expression_statement() {
        ExprPtr expr = expression();
        auto stmt = std::make_unique<ExprStmt>(std::move(expr));
        stmt->line = previous().line;
        match(TokenType::Semicolon);
        return stmt;
    }

    // ============================================================
    //  Expression parsers (precedence climbing)
    // ============================================================

    ExprPtr Parser::expression() {
        return assignment();
    }

    ExprPtr Parser::assignment() {
        ExprPtr expr = ternary();

        // Handle compound assignment operators: +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=
        if (check(TokenType::PlusEqual) || check(TokenType::MinusEqual) ||
            check(TokenType::StarEqual) || check(TokenType::SlashEqual) ||
            check(TokenType::PercentEqual) || check(TokenType::AndEqual) ||
            check(TokenType::OrEqual) || check(TokenType::XorEqual) ||
            check(TokenType::LeftShiftEqual) || check(TokenType::RightShiftEqual)) {
            TokenType op = peek().type;
            uint32_t op_line = peek().line;
            advance();
            ExprPtr value = assignment();

            if (expr->kind == ExprKind::Identifier) {
                auto* ident = static_cast<IdentifierExpr*>(expr.get());

                // Desugar: x += 5 becomes x = x + 5
                TokenType binop;
                switch (op) {
                case TokenType::PlusEqual: binop = TokenType::Plus; break;
                case TokenType::MinusEqual: binop = TokenType::Minus; break;
                case TokenType::StarEqual: binop = TokenType::Star; break;
                case TokenType::SlashEqual: binop = TokenType::Slash; break;
                case TokenType::PercentEqual: binop = TokenType::Percent; break;
                case TokenType::AndEqual: binop = TokenType::BitwiseAnd; break;
                case TokenType::OrEqual: binop = TokenType::BitwiseOr; break;
                case TokenType::XorEqual: binop = TokenType::BitwiseXor; break;
                case TokenType::LeftShiftEqual: binop = TokenType::LeftShift; break;
                case TokenType::RightShiftEqual: binop = TokenType::RightShift; break;
                default: error(previous(), "Invalid compound assignment operator.");
                }

                // Create a copy of the identifier for the right side
                auto ident_copy = std::make_unique<IdentifierExpr>(ident->name);
                ident_copy->line = ident->line;

                // Create binary expression: x + value
                auto bin_expr = std::make_unique<BinaryExpr>(binop, std::move(ident_copy), std::move(value));
                bin_expr->line = op_line;

                // Create assignment: x = (x + value)
                auto assign = std::make_unique<AssignExpr>(ident->name, std::move(bin_expr));
                assign->line = ident->line;
                return assign;
            }
            error(previous(), "Invalid assignment target.");
        }

        if (match(TokenType::Equal)) {
            ExprPtr value = assignment();

            if (expr->kind == ExprKind::Identifier) {
                auto* ident = static_cast<IdentifierExpr*>(expr.get());
                auto assign = std::make_unique<AssignExpr>(ident->name, std::move(value));
                assign->line = ident->line;
                return assign;
            }

            // For member expressions (obj.prop = value), return as binary expression
            // The compiler will handle this specially in expression statements
            if (expr->kind == ExprKind::Member) {
                auto binary = std::make_unique<BinaryExpr>();
                binary->op = TokenType::Equal;
                binary->left = std::move(expr);
                binary->right = std::move(value);
                binary->line = previous().line;
                return binary;
            }

            error(previous(), "Invalid assignment target.");
        }

        return expr;
    }

    ExprPtr Parser::ternary() {
        ExprPtr expr = nil_coalesce();

        // 삼항 연산자: condition ? then_expr : else_expr
        if (match(TokenType::Question)) {
            uint32_t line = previous().line;
            ExprPtr then_expr = expression();
            consume(TokenType::Colon, "Expected ':' after then branch of ternary operator.");
            ExprPtr else_expr = ternary();  // Right-associative

            auto ternary = std::make_unique<TernaryExpr>(std::move(expr), std::move(then_expr), std::move(else_expr));
            ternary->line = line;
            return ternary;
        }

        return expr;
    }

    ExprPtr Parser::nil_coalesce() {
        ExprPtr expr = or_expr();

        // ?? is right-associative
        if (match(TokenType::NilCoalesce)) {
            uint32_t line = previous().line;
            ExprPtr right = nil_coalesce();
            auto coalesce = std::make_unique<NilCoalesceExpr>(std::move(expr), std::move(right));
            coalesce->line = line;
            return coalesce;
        }

        return expr;
    }

    ExprPtr Parser::or_expr() {
        ExprPtr expr = and_expr();

        while (match(TokenType::Or)) {
            uint32_t line = previous().line;
            ExprPtr right = and_expr();
            auto bin = std::make_unique<BinaryExpr>(TokenType::Or, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::and_expr() {
        ExprPtr expr = bitwise_or();

        while (match(TokenType::And)) {
            uint32_t line = previous().line;
            ExprPtr right = bitwise_or();
            auto bin = std::make_unique<BinaryExpr>(TokenType::And, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::bitwise_or() {
        ExprPtr expr = bitwise_xor();

        while (match(TokenType::BitwiseOr)) {
            uint32_t line = previous().line;
            ExprPtr right = bitwise_xor();
            auto bin = std::make_unique<BinaryExpr>(TokenType::BitwiseOr, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::bitwise_xor() {
        ExprPtr expr = bitwise_and();

        while (match(TokenType::BitwiseXor)) {
            uint32_t line = previous().line;
            ExprPtr right = bitwise_and();
            auto bin = std::make_unique<BinaryExpr>(TokenType::BitwiseXor, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::bitwise_and() {
        ExprPtr expr = equality();

        while (match(TokenType::BitwiseAnd)) {
            uint32_t line = previous().line;
            ExprPtr right = equality();
            auto bin = std::make_unique<BinaryExpr>(TokenType::BitwiseAnd, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::equality() {
        ExprPtr expr = type_check_cast();

        while (check(TokenType::EqualEqual) || check(TokenType::NotEqual)) {
            TokenType op = peek().type;
            advance();
            uint32_t line = previous().line;
            ExprPtr right = type_check_cast();
            auto bin = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::type_check_cast() {
        ExprPtr expr = comparison();

        // Type check: expr is Type
        if (match(TokenType::Is)) {
            uint32_t line = previous().line;
            TypeAnnotation target_type = parse_type_annotation();

            auto type_check = std::make_unique<TypeCheckExpr>();
            type_check->value = std::move(expr);
            type_check->target_type = target_type;
            type_check->line = line;
            return type_check;
        }

        // Type cast: expr as Type, expr as? Type, expr as! Type
        if (match(TokenType::As)) {
            uint32_t line = previous().line;
            bool is_optional = false;
            bool is_forced = false;

            // Check for as? or as!
            if (match(TokenType::Question)) {
                is_optional = true;
            }
            else if (match(TokenType::Not)) {
                is_forced = true;
            }

            TypeAnnotation target_type = parse_type_annotation();

            auto type_cast = std::make_unique<TypeCastExpr>();
            type_cast->value = std::move(expr);
            type_cast->target_type = target_type;
            type_cast->is_optional = is_optional;
            type_cast->is_forced = is_forced;
            type_cast->line = line;
            return type_cast;
        }

        return expr;
    }

    ExprPtr Parser::comparison() {
        ExprPtr expr = shift();

        while (check(TokenType::Less) || check(TokenType::Greater) ||
            check(TokenType::LessEqual) || check(TokenType::GreaterEqual)) {
            TokenType op = peek().type;
            advance();
            uint32_t line = previous().line;
            ExprPtr right = shift();
            auto bin = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::shift() {
        ExprPtr expr = addition();

        while (check(TokenType::LeftShift) || check(TokenType::RightShift)) {
            TokenType op = peek().type;
            advance();
            uint32_t line = previous().line;
            ExprPtr right = addition();
            auto bin = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::addition() {
        ExprPtr expr = multiplication();

        while (check(TokenType::Plus) || check(TokenType::Minus)) {
            TokenType op = peek().type;
            advance();
            uint32_t line = previous().line;
            ExprPtr right = multiplication();
            auto bin = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }

        // Handle range operators: ..., ..<, and ..
        if (check(TokenType::RangeInclusive) || check(TokenType::RangeExclusive) || check(TokenType::Range)) {
            bool inclusive = check(TokenType::RangeInclusive);
            advance();
            uint32_t line = previous().line;
            ExprPtr end = multiplication();
            auto range = std::make_unique<RangeExpr>(std::move(expr), std::move(end), inclusive);
            range->line = line;
            return range;
        }

        return expr;
    }

    ExprPtr Parser::multiplication() {
        ExprPtr expr = unary();

        while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
            TokenType op = peek().type;
            advance();
            uint32_t line = previous().line;
            ExprPtr right = unary();
            auto bin = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right));
            bin->line = line;
            expr = std::move(bin);
        }
        return expr;
    }

    ExprPtr Parser::unary() {
        if (check(TokenType::Minus) || check(TokenType::Not) || check(TokenType::BitwiseNot)) {
            TokenType op = peek().type;
            advance();
            uint32_t line = previous().line;
            ExprPtr operand = unary();  // right-recursive
            auto un = std::make_unique<UnaryExpr>(op, std::move(operand));
            un->line = line;
            return un;
        }
        return postfix();
    }

    ExprPtr Parser::postfix() {
        ExprPtr expr = primary();

        for (;;) {
            if (match(TokenType::Not)) {
                // Force unwrap: expr!
                uint32_t line = previous().line;
                auto unwrap = std::make_unique<ForceUnwrapExpr>(std::move(expr));
                unwrap->line = line;
                expr = std::move(unwrap);
            }
            else if (match(TokenType::OptionalChain)) {
                // Optional chaining: expr?.member
                uint32_t line = previous().line;
                const Token& member = consume(TokenType::Identifier, "Expected member name after '?.'.");
                auto chain = std::make_unique<OptionalChainExpr>(std::move(expr), std::string(member.lexeme));
                chain->line = line;
                expr = std::move(chain);
            }
            else if (match(TokenType::Dot)) {
                // Member access: expr.member or tuple index: expr.0, expr.1
                uint32_t line = previous().line;

                // Check for tuple index access: tuple.0, tuple.1, etc.
                if (check(TokenType::Integer)) {
                    Token index_tok = advance();
                    size_t index = static_cast<size_t>(index_tok.value.int_value);
                    auto tuple_mem = std::make_unique<TupleMemberExpr>(std::move(expr), index);
                    tuple_mem->line = line;
                    expr = std::move(tuple_mem);
                }
                else {
                    const Token& member = consume(TokenType::Identifier, "Expected member name after '.'.");
                    auto mem = std::make_unique<MemberExpr>(std::move(expr), std::string(member.lexeme));
                    mem->line = line;
                    expr = std::move(mem);
                }
            }
            else if (match(TokenType::LeftParen)) {
                // Function call: expr(args...) or expr(name: value, ...)
                uint32_t line = previous().line;
                auto call = std::make_unique<CallExpr>();
                call->line = line;
                call->callee = std::move(expr);

                if (!check(TokenType::RightParen)) {
                    do {
                        // Check for named parameter: identifier:
                        if (check(TokenType::Identifier)) {
                            size_t saved_pos = current_;
                            Token potential_name = advance();

                            if (match(TokenType::Colon)) {
                                // This is a named parameter
                                call->argument_names.push_back(std::string(potential_name.lexeme));
                                call->arguments.push_back(expression());
                            }
                            else {
                                // Not a named parameter, restore position and parse as expression
                                current_ = saved_pos;
                                call->argument_names.push_back("");  // Empty name = positional
                                call->arguments.push_back(expression());
                            }
                        }
                        else {
                            // Not an identifier, parse as expression
                            call->argument_names.push_back("");  // Empty name = positional
                            call->arguments.push_back(expression());
                        }
                    } while (match(TokenType::Comma));
                }
                consume(TokenType::RightParen, "Expected ')' after arguments.");
                expr = std::move(call);
            }
            else if (match(TokenType::LeftBracket)) {
                // Subscript access: expr[index]
                uint32_t line = previous().line;
                ExprPtr index = expression();
                consume(TokenType::RightBracket, "Expected ']' after subscript index.");
                auto sub = std::make_unique<SubscriptExpr>(std::move(expr), std::move(index));
                sub->line = line;
                expr = std::move(sub);
            }
            else {
                break;
            }
        }

        return expr;
    }

    ExprPtr Parser::primary() {
        // Integer literal
        if (match(TokenType::Integer)) {
            uint32_t line = previous().line;
            auto lit = std::make_unique<LiteralExpr>(Value::from_int(previous().value.int_value));
            lit->line = line;
            return lit;
        }

        // Float literal
        if (match(TokenType::Float)) {
            uint32_t line = previous().line;
            auto lit = std::make_unique<LiteralExpr>(Value::from_float(previous().value.float_value));
            lit->line = line;
            return lit;
        }

        // String literal — stored as lexeme without quotes
        if (match(TokenType::String)) {
            uint32_t line = previous().line;
            std::string_view raw = previous().lexeme;
            std::string str_val;
            if (raw.size() >= 2) {
                str_val = std::string(raw.substr(1, raw.size() - 2));
            }
            auto lit = std::make_unique<LiteralExpr>(std::move(str_val));
            lit->line = line;
            return lit;
        }

        // true / false
        if (match(TokenType::True)) {
            uint32_t line = previous().line;
            auto lit = std::make_unique<LiteralExpr>(Value::from_bool(true));
            lit->line = line;
            return lit;
        }
        if (match(TokenType::False)) {
            uint32_t line = previous().line;
            auto lit = std::make_unique<LiteralExpr>(Value::from_bool(false));
            lit->line = line;
            return lit;
        }

        // nil
        if (match(TokenType::Nil) || match(TokenType::Null)) {
            uint32_t line = previous().line;
            auto lit = std::make_unique<LiteralExpr>(Value::null());
            lit->line = line;
            return lit;
        }

        // super.member
        if (match(TokenType::Super)) {
            uint32_t line = previous().line;
            consume(TokenType::Dot, "Expected '.' after 'super'.");
            const Token& method_tok = ([&]() -> const Token& {
                if (check(TokenType::Identifier) || check(TokenType::Init)) {
                    return advance();
                }
                error(peek(), "Expected method name after 'super.'");
                })();
            auto super_expr = std::make_unique<SuperExpr>(std::string(method_tok.lexeme));
            super_expr->line = line;
            return super_expr;
        }

        // self (treated as identifier in expression context)
        if (match(TokenType::Self)) {
            uint32_t line = previous().line;
            auto ident = std::make_unique<IdentifierExpr>("self");
            ident->line = line;
            return ident;
        }

        // Identifier
        if (match(TokenType::Identifier)) {
            uint32_t line = previous().line;
            std::string name = std::string(previous().lexeme);
            auto ident = std::make_unique<IdentifierExpr>(name);
            ident->line = line;

            // Check for generic type arguments: Box<Int>
            if (check(TokenType::Less) && looks_like_generic_type_args()) {
                advance(); // consume '<'
                std::vector<TypeAnnotation> generic_args;
                do {
                    TypeAnnotation arg = parse_type_annotation();
                    generic_args.push_back(arg);
                } while (match(TokenType::Comma));

                // Handle >> token for nested generics: Container<Box<Int>>
                // The >> is lexed as RightShift, but we need to treat it as two > tokens
                if (check(TokenType::RightShift)) {
                    // Consume >> and insert a virtual > token for the next level
                    advance();

                    // Insert a Greater token at current position
                    // We do this by adding it to the tokens list
                    Token virtual_greater;
                    virtual_greater.type = TokenType::Greater;
                    virtual_greater.lexeme = ">";
                    virtual_greater.line = previous().line;

                    // Insert at current position
                    tokens_.insert(tokens_.begin() + current_, virtual_greater);
                }
                else {
                    consume(TokenType::Greater, "Expected '>' after generic type arguments.");
                }

                ident->generic_args = std::move(generic_args);
            }

            return ident;
        }

        // Grouped expression or tuple: ( expr ) or (1, 2) or (x: 1, y: 2)
        if (match(TokenType::LeftParen)) {
            uint32_t line = previous().line;

            // Empty tuple: ()
            if (match(TokenType::RightParen)) {
                auto tuple = std::make_unique<TupleLiteralExpr>();
                tuple->line = line;
                return tuple;
            }

            // Parse first element - check if it's a named element (label: value)
            std::vector<TupleElement> elements;
            bool is_tuple = false;

            // Save position to potentially backtrack
            size_t saved_pos = current_;

            // Check if first element is labeled: identifier followed by ':'
            std::optional<std::string> first_label;
            if (check(TokenType::Identifier)) {
                Token potential_label = advance();
                if (match(TokenType::Colon)) {
                    // This is a labeled element
                    first_label = std::string(potential_label.lexeme);
                    is_tuple = true;  // Labeled elements mean it's definitely a tuple
                }
                else {
                    // Not a label, restore position
                    current_ = saved_pos;
                }
            }

            ExprPtr first_expr = expression();

            // Check for comma - indicates tuple
            if (match(TokenType::Comma)) {
                is_tuple = true;

                TupleElement first_elem;
                first_elem.label = first_label;
                first_elem.value = std::move(first_expr);
                elements.push_back(std::move(first_elem));

                // Parse remaining elements
                do {
                    if (check(TokenType::RightParen)) break;  // trailing comma

                    TupleElement elem;

                    // Check for labeled element
                    size_t elem_saved_pos = current_;
                    if (check(TokenType::Identifier)) {
                        Token potential_label = advance();
                        if (match(TokenType::Colon)) {
                            elem.label = std::string(potential_label.lexeme);
                        }
                        else {
                            current_ = elem_saved_pos;
                        }
                    }

                    elem.value = expression();
                    elements.push_back(std::move(elem));
                } while (match(TokenType::Comma));

                consume(TokenType::RightParen, "Expected ')' after tuple elements.");
                auto tuple = std::make_unique<TupleLiteralExpr>(std::move(elements));
                tuple->line = line;
                return tuple;
            }

            // If we have a label but no comma, it's still a tuple (single-element named tuple)
            if (first_label.has_value()) {
                TupleElement first_elem;
                first_elem.label = first_label;
                first_elem.value = std::move(first_expr);
                elements.push_back(std::move(first_elem));

                consume(TokenType::RightParen, "Expected ')' after tuple element.");
                auto tuple = std::make_unique<TupleLiteralExpr>(std::move(elements));
                tuple->line = line;
                return tuple;
            }

            // Just a grouped expression: (expr)
            consume(TokenType::RightParen, "Expected ')' after expression.");
            return first_expr;
        }

        // Array or Dictionary literal: [ ... ]
        if (match(TokenType::LeftBracket)) {
            uint32_t line = previous().line;

            // Empty array: []
            if (match(TokenType::RightBracket)) {
                auto arr = std::make_unique<ArrayLiteralExpr>();
                arr->line = line;
                return arr;
            }

            // Empty dictionary: [:]
            if (match(TokenType::Colon)) {
                consume(TokenType::RightBracket, "Expected ']' after empty dictionary literal.");
                auto dict = std::make_unique<DictLiteralExpr>();
                dict->line = line;
                return dict;
            }

            // Parse first element
            ExprPtr first = expression();

            // Check if it's a dictionary (first element followed by ':')
            if (match(TokenType::Colon)) {
                // Dictionary literal: ["key": value, ...]
                std::vector<std::pair<ExprPtr, ExprPtr>> entries;
                ExprPtr first_value = expression();
                entries.emplace_back(std::move(first), std::move(first_value));

                while (match(TokenType::Comma)) {
                    if (check(TokenType::RightBracket)) break;  // trailing comma
                    ExprPtr key = expression();
                    consume(TokenType::Colon, "Expected ':' in dictionary literal.");
                    ExprPtr value = expression();
                    entries.emplace_back(std::move(key), std::move(value));
                }

                consume(TokenType::RightBracket, "Expected ']' after dictionary literal.");
                auto dict = std::make_unique<DictLiteralExpr>(std::move(entries));
                dict->line = line;
                return dict;
            }

            // Array literal: [1, 2, 3]
            std::vector<ExprPtr> elements;
            elements.push_back(std::move(first));

            while (match(TokenType::Comma)) {
                if (check(TokenType::RightBracket)) break;  // trailing comma
                elements.push_back(expression());
            }

            consume(TokenType::RightBracket, "Expected ']' after array literal.");
            auto arr = std::make_unique<ArrayLiteralExpr>(std::move(elements));
            arr->line = line;
            return arr;
        }

        // Closure expression: { (params) -> ReturnType in body }
        if (check(TokenType::LeftBrace)) {
            return closure_expression();
        }

        error(peek(), "Expected expression.");
    }

    ExprPtr Parser::closure_expression() {
        const Token& brace = advance();  // consume '{'
        uint32_t line = brace.line;

        auto closure = std::make_unique<ClosureExpr>();
        closure->line = line;

        // Check if there are parameters: { (param: Type, ...) -> ReturnType in ... }
        if (match(TokenType::LeftParen)) {
            // Parse parameter list
            if (!check(TokenType::RightParen)) {
                do {
                    const Token& param_name = consume(TokenType::Identifier, "Expected parameter name.");
                    consume(TokenType::Colon, "Expected ':' after parameter name.");
                    TypeAnnotation param_type = parse_type_annotation();
                    closure->params.emplace_back(std::string(param_name.lexeme), param_type);
                } while (match(TokenType::Comma));
            }
            consume(TokenType::RightParen, "Expected ')' after closure parameters.");

            // Optional return type: -> Type
            if (match(TokenType::Arrow)) {
                closure->return_type = parse_type_annotation();
            }

            // 'in' keyword separates parameters from body
            consume(TokenType::In, "Expected 'in' after closure parameters.");
        }

        // Parse closure body (statements until '}')
        while (!check(TokenType::RightBrace) && !is_at_end()) {
            closure->body.push_back(declaration());
        }

        consume(TokenType::RightBrace, "Expected '}' at end of closure.");

        return closure;
    }

    ParamDecl Parser::parse_param(bool allow_default) {
        const Token& first_name = consume(TokenType::Identifier, "Expected parameter name.");
        std::string external_name(first_name.lexeme);
        std::string internal_name = external_name;

        if (check(TokenType::Identifier)) {
            const Token& second_name = advance();
            internal_name = std::string(second_name.lexeme);
        }

        if (external_name == "_") {
            external_name.clear();
        }

        consume(TokenType::Colon, "Expected ':' after parameter name.");
        TypeAnnotation param_type = parse_type_annotation();

        ExprPtr default_value;
        if (allow_default && match(TokenType::Equal)) {
            default_value = expression();
        }

        ParamDecl param;
        param.external_name = std::move(external_name);
        param.internal_name = std::move(internal_name);
        param.type = param_type;
        param.default_value = std::move(default_value);
        return param;
    }

    std::vector<ParamDecl> Parser::parse_param_list(bool allow_default) {
        std::vector<ParamDecl> params;
        if (!check(TokenType::RightParen)) {
            do {
                params.push_back(parse_param(allow_default));
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RightParen, "Expected ')' after parameters.");
        return params;
    }

    PatternPtr Parser::parse_pattern() {
        if (match(TokenType::Dot)) {
            auto pattern = std::make_unique<EnumCasePattern>();
            pattern->line = previous().line;
            const Token& case_name = consume(TokenType::Identifier, "Expected enum case name.");
            pattern->case_name = std::string(case_name.lexeme);

            if (match(TokenType::LeftParen)) {
                if (!check(TokenType::RightParen)) {
                    do {
                        if (match(TokenType::Let) || match(TokenType::Var)) {
                            // Binding modifier, ignore for now.
                        }
                        const Token& binding = consume(TokenType::Identifier, "Expected binding name.");
                        pattern->bindings.push_back(std::string(binding.lexeme));
                    } while (match(TokenType::Comma));
                }
                consume(TokenType::RightParen, "Expected ')' after enum case bindings.");
            }

            return pattern;
        }

        auto expr_pattern = std::make_unique<ExpressionPattern>();
        expr_pattern->expression = expression();
        expr_pattern->line = expr_pattern->expression->line;
        return expr_pattern;
    }

} // namespace swiftscript
