#include "ss_parser.hpp"
#include <sstream>

namespace swiftscript {

// ============================================================
//  Constructor & entry point
// ============================================================

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)) {}

std::vector<StmtPtr> Parser::parse() {
    std::vector<StmtPtr> statements;
    while (!is_at_end()) {
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

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    error(peek(), message);
}

[[noreturn]] void Parser::error(const Token& token, const std::string& message) {
    std::ostringstream oss;
    oss << "[line " << token.line << ":" << token.column << "] Error";
    if (token.type == TokenType::Eof) {
        oss << " at end";
    } else {
        oss << " at '" << token.lexeme << "'";
    }
    oss << ": " << message;
    throw ParseError(oss.str(), token.line, token.column);
}

bool Parser::is_at_end() const {
    return peek().type == TokenType::Eof;
}

// ============================================================
//  Type annotation
// ============================================================

TypeAnnotation Parser::parse_type_annotation() {
    // Function type: (ParamType, ...) -> ReturnType
    if (match(TokenType::LeftParen)) {
        TypeAnnotation ta;
        ta.is_function_type = true;
        ta.name = "Function";

        if (!check(TokenType::RightParen)) {
            do {
                ta.param_types.push_back(parse_type_annotation());
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RightParen, "Expected ')' in function type.");
        consume(TokenType::Arrow, "Expected '->' in function type.");
        ta.return_type = std::make_shared<TypeAnnotation>(parse_type_annotation());

        if (match(TokenType::Question)) {
            ta.is_optional = true;
        }
        return ta;
    }

    // Simple type: TypeName or TypeName?
    const Token& name_tok = consume(TokenType::Identifier, "Expected type name.");
    TypeAnnotation ta;
    ta.name = std::string(name_tok.lexeme);
    ta.is_optional = false;

    // Check for trailing '?' to mark optional
    if (match(TokenType::Question)) {
        ta.is_optional = true;
    }
    return ta;
}

// ============================================================
//  Statement parsers
// ============================================================

StmtPtr Parser::declaration() {
    if (check(TokenType::Var) || check(TokenType::Let)) {
        return var_declaration();
    }
    if (check(TokenType::Class)) {
        return class_declaration();
    }
    if (check(TokenType::Func)) {
        return func_declaration();
    }
    return statement();
}

StmtPtr Parser::var_declaration() {
    // var x: Int? = expr
    // let x = expr
    bool is_let = check(TokenType::Let);
    advance();  // consume 'var' or 'let'

    const Token& name_tok = consume(TokenType::Identifier, "Expected variable name.");
    auto stmt = std::make_unique<VarDeclStmt>();
    stmt->line = name_tok.line;
    stmt->name = std::string(name_tok.lexeme);
    stmt->is_let = is_let;

    // Optional type annotation
    if (match(TokenType::Colon)) {
        stmt->type_annotation = parse_type_annotation();
    }

    // Optional initializer
    if (match(TokenType::Equal)) {
        stmt->initializer = expression();
    }

    // Semicolons are optional (Swift-style)
    match(TokenType::Semicolon);
    return stmt;
}

StmtPtr Parser::func_declaration() {
    advance();  // consume 'func'
    const Token& name_tok = ([&]() -> const Token& {
        if (check(TokenType::Identifier) || check(TokenType::Init)) {
            return advance();
        }
        error(peek(), "Expected function name.");
    })();

    auto stmt = std::make_unique<FuncDeclStmt>();
    stmt->line = name_tok.line;
    stmt->name = std::string(name_tok.lexeme);

    // Parameter list
    consume(TokenType::LeftParen, "Expected '(' after function name.");
    if (!check(TokenType::RightParen)) {
        do {
            const Token& param_name = consume(TokenType::Identifier, "Expected parameter name.");
            consume(TokenType::Colon, "Expected ':' after parameter name.");
            TypeAnnotation param_type = parse_type_annotation();
            stmt->params.emplace_back(std::string(param_name.lexeme), param_type);
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParen, "Expected ')' after parameters.");

    // Optional return type: -> Type
    if (match(TokenType::Arrow)) {
        stmt->return_type = parse_type_annotation();
    }

    // Body
    stmt->body = block();
    return stmt;
}

StmtPtr Parser::class_declaration() {
    advance(); // consume 'class'
    const Token& name_tok = consume(TokenType::Identifier, "Expected class name.");

    auto stmt = std::make_unique<ClassDeclStmt>();
    stmt->line = name_tok.line;
    stmt->name = std::string(name_tok.lexeme);

    consume(TokenType::LeftBrace, "Expected '{' after class name.");
    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!check(TokenType::Func)) {
            error(peek(), "Expected method declaration inside class.");
        }
        auto method = std::unique_ptr<FuncDeclStmt>(static_cast<FuncDeclStmt*>(func_declaration().release()));
        stmt->methods.push_back(std::move(method));
    }
    consume(TokenType::RightBrace, "Expected '}' after class body.");
    return stmt;
}

StmtPtr Parser::statement() {
    if (check(TokenType::If)) return if_statement();
    if (check(TokenType::Guard)) return guard_statement();
    if (check(TokenType::While)) return while_statement();
    if (check(TokenType::For)) return for_in_statement();
    if (check(TokenType::Switch)) return switch_statement();
    if (check(TokenType::Break)) return break_statement();
    if (check(TokenType::Continue)) return continue_statement();
    if (check(TokenType::Return)) return return_statement();
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
            } else {
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
        } else {
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
                clause.patterns.push_back(expression());
            } while (match(TokenType::Comma));
            
            consume(TokenType::Colon, "Expected ':' after case pattern.");
            
        } else if (match(TokenType::Default)) {
            clause.is_default = true;
            consume(TokenType::Colon, "Expected ':' after 'default'.");
            
        } else {
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

    // Handle compound assignment operators: +=, -=, *=, /=
    if (check(TokenType::PlusEqual) || check(TokenType::MinusEqual) ||
        check(TokenType::StarEqual) || check(TokenType::SlashEqual)) {
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
    ExprPtr expr = equality();

    while (match(TokenType::And)) {
        uint32_t line = previous().line;
        ExprPtr right = equality();
        auto bin = std::make_unique<BinaryExpr>(TokenType::And, std::move(expr), std::move(right));
        bin->line = line;
        expr = std::move(bin);
    }
    return expr;
}

ExprPtr Parser::equality() {
    ExprPtr expr = comparison();

    while (check(TokenType::EqualEqual) || check(TokenType::NotEqual)) {
        TokenType op = peek().type;
        advance();
        uint32_t line = previous().line;
        ExprPtr right = comparison();
        auto bin = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(right));
        bin->line = line;
        expr = std::move(bin);
    }
    return expr;
}

ExprPtr Parser::comparison() {
    ExprPtr expr = addition();

    while (check(TokenType::Less) || check(TokenType::Greater) ||
           check(TokenType::LessEqual) || check(TokenType::GreaterEqual)) {
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
        } else if (match(TokenType::OptionalChain)) {
            // Optional chaining: expr?.member
            uint32_t line = previous().line;
            const Token& member = consume(TokenType::Identifier, "Expected member name after '?.'.");
            auto chain = std::make_unique<OptionalChainExpr>(std::move(expr), std::string(member.lexeme));
            chain->line = line;
            expr = std::move(chain);
        } else if (match(TokenType::Dot)) {
            // Member access: expr.member
            uint32_t line = previous().line;
            const Token& member = consume(TokenType::Identifier, "Expected member name after '.'.");
            auto mem = std::make_unique<MemberExpr>(std::move(expr), std::string(member.lexeme));
            mem->line = line;
            expr = std::move(mem);
        } else if (match(TokenType::LeftParen)) {
            // Function call: expr(args...)
            uint32_t line = previous().line;
            auto call = std::make_unique<CallExpr>();
            call->line = line;
            call->callee = std::move(expr);

            if (!check(TokenType::RightParen)) {
                do {
                    call->arguments.push_back(expression());
                } while (match(TokenType::Comma));
            }
            consume(TokenType::RightParen, "Expected ')' after arguments.");
            expr = std::move(call);
        } else if (match(TokenType::LeftBracket)) {
            // Subscript access: expr[index]
            uint32_t line = previous().line;
            ExprPtr index = expression();
            consume(TokenType::RightBracket, "Expected ']' after subscript index.");
            auto sub = std::make_unique<SubscriptExpr>(std::move(expr), std::move(index));
            sub->line = line;
            expr = std::move(sub);
        } else {
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

    // Identifier
    if (match(TokenType::Identifier)) {
        uint32_t line = previous().line;
        auto ident = std::make_unique<IdentifierExpr>(std::string(previous().lexeme));
        ident->line = line;
        return ident;
    }

    // Grouped expression: ( expr )
    if (match(TokenType::LeftParen)) {
        ExprPtr expr = expression();
        consume(TokenType::RightParen, "Expected ')' after expression.");
        return expr;
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

} // namespace swiftscript
