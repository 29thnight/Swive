#include "ss_compiler.hpp"
#include <limits>
#include <memory>
#include <stdexcept>

namespace swiftscript {

Chunk Compiler::compile(const std::vector<StmtPtr>& program) {
    chunk_ = Chunk{};
    locals_.clear();
    scope_depth_ = 0;
    recursion_depth_ = 0;

    for (const auto& stmt : program) {
        if (!stmt) {
            throw CompilerError("Null statement in program");
        }
        compile_stmt(stmt.get());
    }

    emit_op(OpCode::OP_NIL, 0);
    emit_op(OpCode::OP_HALT, 0);
    return chunk_;
}

void Compiler::compile_stmt(Stmt* stmt) {
    if (!stmt) {
        throw CompilerError("Null statement pointer");
    }

    RecursionGuard guard(*this);

    switch (stmt->kind) {
        case StmtKind::VarDecl:
            visit(static_cast<VarDeclStmt*>(stmt));
            break;
        case StmtKind::If:
            visit(static_cast<IfStmt*>(stmt));
            break;
        case StmtKind::IfLet:
            visit(static_cast<IfLetStmt*>(stmt));
            break;
        case StmtKind::GuardLet:
            visit(static_cast<GuardLetStmt*>(stmt));
            break;
        case StmtKind::While:
            visit(static_cast<WhileStmt*>(stmt));
            break;
        case StmtKind::Block:
            visit(static_cast<BlockStmt*>(stmt));
            break;
        case StmtKind::Print:
            visit(static_cast<PrintStmt*>(stmt));
            break;
        case StmtKind::Return:
            visit(static_cast<ReturnStmt*>(stmt));
            break;
        case StmtKind::FuncDecl:
            visit(static_cast<FuncDeclStmt*>(stmt));
            break;
        case StmtKind::Expression:
            visit(static_cast<ExprStmt*>(stmt));
            break;
        case StmtKind::ForIn:
            visit(static_cast<ForInStmt*>(stmt));
            break;
        case StmtKind::Break:
            visit(static_cast<BreakStmt*>(stmt));
            break;
        case StmtKind::Continue:
            visit(static_cast<ContinueStmt*>(stmt));
            break;
        default:
            throw CompilerError("Unknown statement kind", stmt->line);
    }
}

void Compiler::compile_expr(Expr* expr) {
    if (!expr) {
        throw CompilerError("Null expression pointer");
    }

    RecursionGuard guard(*this);

    switch (expr->kind) {
        case ExprKind::Literal:
            visit(static_cast<LiteralExpr*>(expr));
            break;
        case ExprKind::Identifier:
            visit(static_cast<IdentifierExpr*>(expr));
            break;
        case ExprKind::Unary:
            visit(static_cast<UnaryExpr*>(expr));
            break;
        case ExprKind::Binary:
            visit(static_cast<BinaryExpr*>(expr));
            break;
        case ExprKind::Assign:
            visit(static_cast<AssignExpr*>(expr));
            break;
        case ExprKind::ForceUnwrap:
            visit(static_cast<ForceUnwrapExpr*>(expr));
            break;
        case ExprKind::NilCoalesce:
            visit(static_cast<NilCoalesceExpr*>(expr));
            break;
        case ExprKind::OptionalChain:
            visit(static_cast<OptionalChainExpr*>(expr));
            break;
        case ExprKind::Member:
            visit(static_cast<MemberExpr*>(expr));
            break;
        case ExprKind::Call:
            visit(static_cast<CallExpr*>(expr));
            break;
        case ExprKind::Range:
            visit(static_cast<RangeExpr*>(expr));
            break;
        case ExprKind::ArrayLiteral:
            visit(static_cast<ArrayLiteralExpr*>(expr));
            break;
        case ExprKind::DictLiteral:
            visit(static_cast<DictLiteralExpr*>(expr));
            break;
        case ExprKind::Subscript:
            visit(static_cast<SubscriptExpr*>(expr));
            break;
        default:
            throw CompilerError("Unknown expression kind", expr->line);
    }
}

void Compiler::visit(VarDeclStmt* stmt) {
    if (scope_depth_ > 0) {
        declare_local(stmt->name, stmt->type_annotation.value_or(TypeAnnotation{}).is_optional);
    }

    if (stmt->initializer) {
        compile_expr(stmt->initializer.get());
    } else {
        emit_op(OpCode::OP_NIL, stmt->line);
    }

    if (scope_depth_ == 0) {
        size_t name_idx = identifier_constant(stmt->name);
        if (name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many global variables", stmt->line);
        }
        emit_op(OpCode::OP_SET_GLOBAL, stmt->line);
        emit_short(static_cast<uint16_t>(name_idx), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
    } else {
        mark_local_initialized();
    }
}

void Compiler::visit(IfStmt* stmt) {
    compile_expr(stmt->condition.get());

    size_t else_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
    emit_op(OpCode::OP_POP, stmt->line);
    compile_stmt(stmt->then_branch.get());

    size_t end_jump = emit_jump(OpCode::OP_JUMP, stmt->line);
    patch_jump(else_jump);
    emit_op(OpCode::OP_POP, stmt->line);

    if (stmt->else_branch) {
        compile_stmt(stmt->else_branch.get());
    }
    patch_jump(end_jump);
}

void Compiler::visit(IfLetStmt* stmt) {
    compile_expr(stmt->optional_expr.get());

    size_t else_jump = emit_jump(OpCode::OP_JUMP_IF_NIL, stmt->line);
    // OP_JUMP_IF_NIL�� nil�� �ƴ� �� ���� ���ÿ� ����
    
    begin_scope();
    declare_local(stmt->binding_name, false);
    mark_local_initialized();
    compile_stmt(stmt->then_branch.get());
    end_scope();

    size_t end_jump = emit_jump(OpCode::OP_JUMP, stmt->line);
    patch_jump(else_jump);
    // OP_JUMP_IF_NIL�� �̹� nil�� POP�����Ƿ� �߰� POP ���ʿ�

    if (stmt->else_branch) {
        compile_stmt(stmt->else_branch.get());
    }
    patch_jump(end_jump);
}

void Compiler::visit(GuardLetStmt* stmt) {
    if (!is_exiting_stmt(stmt->else_branch.get())) {
        throw CompilerError("guard let requires else branch to exit", stmt->line);
    }

    compile_expr(stmt->optional_expr.get());

    size_t else_jump = emit_jump(OpCode::OP_JUMP_IF_NIL, stmt->line);
    size_t locals_before = locals_.size();
    
    if (scope_depth_ == 0) {
        size_t name_idx = identifier_constant(stmt->binding_name);
        if (name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many global variables", stmt->line);
        }
        emit_op(OpCode::OP_SET_GLOBAL, stmt->line);
        emit_short(static_cast<uint16_t>(name_idx), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
    } else {
        declare_local(stmt->binding_name, false);
        mark_local_initialized();
    }

    size_t end_jump = emit_jump(OpCode::OP_JUMP, stmt->line);
    patch_jump(else_jump);
    // OP_JUMP_IF_NIL�� �̹� nil�� POP�����Ƿ� �߰� POP ���ʿ�

    auto saved_locals = locals_;
    locals_.resize(locals_before);
    compile_stmt(stmt->else_branch.get());
    locals_ = std::move(saved_locals);

    patch_jump(end_jump);
}

void Compiler::visit(WhileStmt* stmt) {
    // ���� ���ؽ�Ʈ ����
    loop_stack_.push_back({});
    size_t loop_start = chunk_.code.size();
    loop_stack_.back().loop_start = loop_start;
    loop_stack_.back().scope_depth_at_start = scope_depth_;
    
    compile_expr(stmt->condition.get());

    size_t exit_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
    emit_op(OpCode::OP_POP, stmt->line);
    
    compile_stmt(stmt->body.get());
    
    // continue ���� ��ġ
    for (size_t jump : loop_stack_.back().continue_jumps) {
        patch_jump(jump);
    }
    
    emit_loop(loop_start, stmt->line);
    patch_jump(exit_jump);
    emit_op(OpCode::OP_POP, stmt->line);
    
    // break ���� ��ġ
    for (size_t jump : loop_stack_.back().break_jumps) {
        patch_jump(jump);
    }
    
    loop_stack_.pop_back();
}

void Compiler::visit(ForInStmt* stmt) {
    // for i in range { body }�� while ������ ��ȯ
    // 1. ���� ���� �� iterator ������ ����
    
    compile_expr(stmt->iterable.get());  // ������ �� (start, end�� ���ÿ� push)
    
    // iterable�� RangeExpr���� Ȯ��
    if (stmt->iterable->kind != ExprKind::Range) {
        throw CompilerError("for-in only supports range expressions", stmt->line);
    }
    
    RangeExpr* range = static_cast<RangeExpr*>(stmt->iterable.get());
    
    // ������ ����
    begin_scope();
    
    // Stack: [start, end]
    // ���� ���� ���� ������ ���� ������ ��ġ��Ŵ
    
    // ���� ������ ���� ���� (slot 0 = start)
    declare_local(stmt->variable, false);
    mark_local_initialized();
    
    // end ���� ���� ������ ���� (slot 1 = end)
    declare_local("$end", false);
    mark_local_initialized();
    
    // ���� ���ؽ�Ʈ ����
    loop_stack_.push_back({});
    size_t loop_start = chunk_.code.size();
    loop_stack_.back().loop_start = loop_start;
    loop_stack_.back().scope_depth_at_start = scope_depth_;
    
    // ���� üũ: i < end (exclusive) �Ǵ� i <= end (inclusive)
    int loop_var_idx = resolve_local(stmt->variable);
    int end_var_idx = resolve_local("$end");
    
    emit_op(OpCode::OP_GET_LOCAL, stmt->line);
    emit_short(static_cast<uint16_t>(loop_var_idx), stmt->line);
    
    emit_op(OpCode::OP_GET_LOCAL, stmt->line);
    emit_short(static_cast<uint16_t>(end_var_idx), stmt->line);
    
    emit_op(range->inclusive ? OpCode::OP_LESS_EQUAL : OpCode::OP_LESS, stmt->line);
    
    size_t exit_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
    emit_op(OpCode::OP_POP, stmt->line);
    
    // ���� ���� ����
    compile_stmt(stmt->body.get());
    
    // continue ���� ��ġ
    for (size_t jump : loop_stack_.back().continue_jumps) {
        patch_jump(jump);
    }
    
    // i�� ������Ŵ
    emit_op(OpCode::OP_GET_LOCAL, stmt->line);
    emit_short(static_cast<uint16_t>(loop_var_idx), stmt->line);
    emit_constant(Value::from_int(1), stmt->line);
    emit_op(OpCode::OP_ADD, stmt->line);
    emit_op(OpCode::OP_SET_LOCAL, stmt->line);
    emit_short(static_cast<uint16_t>(loop_var_idx), stmt->line);
    emit_op(OpCode::OP_POP, stmt->line);
    
    // ���� �������� ����
    emit_loop(loop_start, stmt->line);
    
    // ���� ����
    patch_jump(exit_jump);
    emit_op(OpCode::OP_POP, stmt->line);
    
    // break ���� ��ġ
    for (size_t jump : loop_stack_.back().break_jumps) {
        patch_jump(jump);
    }
    
    loop_stack_.pop_back();
    end_scope();
}

void Compiler::visit(BreakStmt* stmt) {
    if (loop_stack_.empty()) {
        throw CompilerError("'break' outside of loop", stmt->line);
    }
    
    // ���� �������� ���� ������ ����
    int target_depth = loop_stack_.back().scope_depth_at_start;
    for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
        if (it->depth <= target_depth) {
            break;
        }
        emit_op(OpCode::OP_POP, stmt->line);
    }
    
    size_t jump = emit_jump(OpCode::OP_JUMP, stmt->line);
    loop_stack_.back().break_jumps.push_back(jump);
}

void Compiler::visit(ContinueStmt* stmt) {
    if (loop_stack_.empty()) {
        throw CompilerError("'continue' outside of loop", stmt->line);
    }
    
    // ���� �������� ���� ������ ���� (���� ������ ����)
    int target_depth = loop_stack_.back().scope_depth_at_start;
    for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
        if (it->depth <= target_depth) {
            break;
        }
        emit_op(OpCode::OP_POP, stmt->line);
    }
    
    size_t jump = emit_jump(OpCode::OP_JUMP, stmt->line);
    loop_stack_.back().continue_jumps.push_back(jump);
}

void Compiler::visit(BlockStmt* stmt) {
    begin_scope();
    for (const auto& statement : stmt->statements) {
        compile_stmt(statement.get());
    }
    end_scope();
}

void Compiler::visit(PrintStmt* stmt) {
    compile_expr(stmt->expression.get());
    emit_op(OpCode::OP_PRINT, stmt->line);
}

void Compiler::visit(ReturnStmt* stmt) {
    if (stmt->value) {
        compile_expr(stmt->value.get());
    } else {
        emit_op(OpCode::OP_NIL, stmt->line);
    }
    emit_op(OpCode::OP_RETURN, stmt->line);
}

void Compiler::visit(FuncDeclStmt* stmt) {
    FunctionPrototype proto;
    proto.name = stmt->name;
    proto.params.reserve(stmt->params.size());
    for (const auto& [param_name, param_type] : stmt->params) {
        proto.params.push_back(param_name);
    }

    Chunk function_chunk = compile_function_body(*stmt);
    proto.chunk = std::make_shared<Chunk>(std::move(function_chunk));

    size_t function_index = chunk_.add_function(std::move(proto));
    if (function_index > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many functions in chunk", stmt->line);
    }

    emit_op(OpCode::OP_FUNCTION, stmt->line);
    emit_short(static_cast<uint16_t>(function_index), stmt->line);

    if (scope_depth_ == 0) {
        size_t name_idx = identifier_constant(stmt->name);
        if (name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many global variables", stmt->line);
        }
        emit_op(OpCode::OP_SET_GLOBAL, stmt->line);
        emit_short(static_cast<uint16_t>(name_idx), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
    } else {
        declare_local(stmt->name, false);
        mark_local_initialized();
    }
}

void Compiler::visit(ExprStmt* stmt) {
    compile_expr(stmt->expression.get());
    emit_op(OpCode::OP_POP, stmt->line);
}

void Compiler::visit(LiteralExpr* expr) {
    if (expr->string_value) {
        emit_string(*expr->string_value, expr->line);
        return;
    }

    if (expr->value.is_null()) {
        emit_op(OpCode::OP_NIL, expr->line);
    } else if (expr->value.is_bool()) {
        emit_op(expr->value.as_bool() ? OpCode::OP_TRUE : OpCode::OP_FALSE, expr->line);
    } else {
        emit_constant(expr->value, expr->line);
    }
}

void Compiler::visit(IdentifierExpr* expr) {
    int local = resolve_local(expr->name);
    if (local != -1) {
        emit_op(OpCode::OP_GET_LOCAL, expr->line);
        emit_short(static_cast<uint16_t>(local), expr->line);
        return;
    }

    size_t name_idx = identifier_constant(expr->name);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", expr->line);
    }
    emit_op(OpCode::OP_GET_GLOBAL, expr->line);
    emit_short(static_cast<uint16_t>(name_idx), expr->line);
}

void Compiler::visit(UnaryExpr* expr) {
    compile_expr(expr->operand.get());
    switch (expr->op) {
        case TokenType::Minus:
            emit_op(OpCode::OP_NEGATE, expr->line);
            break;
        case TokenType::Not:
            emit_op(OpCode::OP_NOT, expr->line);
            break;
        case TokenType::BitwiseNot:
            emit_op(OpCode::OP_BITWISE_NOT, expr->line);
            break;
        default:
            throw CompilerError("Unsupported unary operator", expr->line);
    }
}

void Compiler::visit(BinaryExpr* expr) {
    compile_expr(expr->left.get());
    compile_expr(expr->right.get());

    switch (expr->op) {
        case TokenType::Plus:
            emit_op(OpCode::OP_ADD, expr->line);
            break;
        case TokenType::Minus:
            emit_op(OpCode::OP_SUBTRACT, expr->line);
            break;
        case TokenType::Star:
            emit_op(OpCode::OP_MULTIPLY, expr->line);
            break;
        case TokenType::Slash:
            emit_op(OpCode::OP_DIVIDE, expr->line);
            break;
        case TokenType::Percent:
            emit_op(OpCode::OP_MODULO, expr->line);
            break;
        case TokenType::EqualEqual:
            emit_op(OpCode::OP_EQUAL, expr->line);
            break;
        case TokenType::NotEqual:
            emit_op(OpCode::OP_NOT_EQUAL, expr->line);
            break;
        case TokenType::Less:
            emit_op(OpCode::OP_LESS, expr->line);
            break;
        case TokenType::Greater:
            emit_op(OpCode::OP_GREATER, expr->line);
            break;
        case TokenType::LessEqual:
            emit_op(OpCode::OP_LESS_EQUAL, expr->line);
            break;
        case TokenType::GreaterEqual:
            emit_op(OpCode::OP_GREATER_EQUAL, expr->line);
            break;
        case TokenType::And:
            emit_op(OpCode::OP_AND, expr->line);
            break;
        case TokenType::Or:
            emit_op(OpCode::OP_OR, expr->line);
            break;
        default:
            throw CompilerError("Unsupported binary operator", expr->line);
    }
}

void Compiler::visit(AssignExpr* expr) {
    // ���� ���� ������ ó��
    if (expr->op != TokenType::Equal) {
        // x += 5 �� x = x + 5 �� ��ȯ
        int local = resolve_local(expr->name);
        if (local != -1) {
            emit_op(OpCode::OP_GET_LOCAL, expr->line);
            emit_short(static_cast<uint16_t>(local), expr->line);
        } else {
            size_t name_idx = identifier_constant(expr->name);
            if (name_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many identifiers", expr->line);
            }
            emit_op(OpCode::OP_GET_GLOBAL, expr->line);
            emit_short(static_cast<uint16_t>(name_idx), expr->line);
        }
        
        // ������ �� ������
        compile_expr(expr->value.get());
        
        // ���� ����
        switch (expr->op) {
            case TokenType::PlusEqual:
                emit_op(OpCode::OP_ADD, expr->line);
                break;
            case TokenType::MinusEqual:
                emit_op(OpCode::OP_SUBTRACT, expr->line);
                break;
            case TokenType::StarEqual:
                emit_op(OpCode::OP_MULTIPLY, expr->line);
                break;
            case TokenType::SlashEqual:
                emit_op(OpCode::OP_DIVIDE, expr->line);
                break;
            default:
                throw CompilerError("Unsupported compound assignment", expr->line);
        }
    } else {
        // �Ϲ� ����
        compile_expr(expr->value.get());
    }

    // ��� ����
    int local = resolve_local(expr->name);
    if (local != -1) {
        emit_op(OpCode::OP_SET_LOCAL, expr->line);
        emit_short(static_cast<uint16_t>(local), expr->line);
        return;
    }

    size_t name_idx = identifier_constant(expr->name);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", expr->line);
    }
    emit_op(OpCode::OP_SET_GLOBAL, expr->line);
    emit_short(static_cast<uint16_t>(name_idx), expr->line);
}

void Compiler::visit(ForceUnwrapExpr* expr) {
    compile_expr(expr->operand.get());
    emit_op(OpCode::OP_UNWRAP, expr->line);
}

void Compiler::visit(NilCoalesceExpr* expr) {
    compile_expr(expr->optional_expr.get());
    size_t else_jump = emit_jump(OpCode::OP_JUMP_IF_NIL, expr->line);
    // nil�� �ƴϸ�: ���� ���ÿ� ���� -> end_jump�� �̵�
    size_t end_jump = emit_jump(OpCode::OP_JUMP, expr->line);
    
    // nil�̸�: OP_JUMP_IF_NIL�� �̹� POP���� -> fallback ��
    patch_jump(else_jump);
    compile_expr(expr->fallback.get());
    
    patch_jump(end_jump);
}

void Compiler::visit(OptionalChainExpr* expr) {
    compile_expr(expr->object.get());
    size_t name_idx = identifier_constant(expr->member);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", expr->line);
    }
    emit_op(OpCode::OP_OPTIONAL_CHAIN, expr->line);
    emit_short(static_cast<uint16_t>(name_idx), expr->line);
}

void Compiler::visit(MemberExpr* expr) {
    compile_expr(expr->object.get());
    size_t name_idx = identifier_constant(expr->member);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", expr->line);
    }
    emit_op(OpCode::OP_GET_PROPERTY, expr->line);
    emit_short(static_cast<uint16_t>(name_idx), expr->line);
}

void Compiler::visit(CallExpr* expr) {
    compile_expr(expr->callee.get());

    if (expr->arguments.size() > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many arguments in function call", expr->line);
    }

    for (const auto& arg : expr->arguments) {
        compile_expr(arg.get());
    }

    emit_op(OpCode::OP_CALL, expr->line);
    emit_short(static_cast<uint16_t>(expr->arguments.size()), expr->line);
}

void Compiler::visit(RangeExpr* expr) {
    compile_expr(expr->start.get());
    compile_expr(expr->end.get());

    emit_op(expr->inclusive ? OpCode::OP_RANGE_INCLUSIVE : OpCode::OP_RANGE_EXCLUSIVE, expr->line);
}

void Compiler::visit(ArrayLiteralExpr* expr) {
    // Push all elements onto the stack
    for (const auto& elem : expr->elements) {
        compile_expr(elem.get());
    }

    if (expr->elements.size() > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many elements in array literal", expr->line);
    }

    emit_op(OpCode::OP_ARRAY, expr->line);
    emit_short(static_cast<uint16_t>(expr->elements.size()), expr->line);
}

void Compiler::visit(DictLiteralExpr* expr) {
    // Push all key-value pairs onto the stack
    for (const auto& [key, value] : expr->entries) {
        compile_expr(key.get());
        compile_expr(value.get());
    }

    if (expr->entries.size() > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many entries in dictionary literal", expr->line);
    }

    emit_op(OpCode::OP_DICT, expr->line);
    emit_short(static_cast<uint16_t>(expr->entries.size()), expr->line);
}

void Compiler::visit(SubscriptExpr* expr) {
    compile_expr(expr->object.get());
    compile_expr(expr->index.get());
    emit_op(OpCode::OP_GET_SUBSCRIPT, expr->line);
}

void Compiler::begin_scope() {
    scope_depth_++;
}

void Compiler::end_scope() {
    scope_depth_--;
    while (!locals_.empty() && locals_.back().depth > scope_depth_) {
        emit_op(OpCode::OP_POP, 0);
        locals_.pop_back();
    }
}

void Compiler::declare_local(const std::string& name, bool is_optional) {
    if (scope_depth_ == 0) {
        return;
    }

    if (locals_.size() >= MAX_LOCALS) {
        throw CompilerError("Too many local variables in scope (max: " + 
                          std::to_string(MAX_LOCALS) + ")");
    }

    for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
        if (it->depth != -1 && it->depth < scope_depth_) {
            break;
        }
        if (it->name == name) {
            throw CompilerError("Variable '" + name + "' already declared in this scope");
        }
    }

    locals_.push_back({name, -1, is_optional});
}

void Compiler::mark_local_initialized() {
    if (scope_depth_ == 0 || locals_.empty()) {
        return;
    }
    locals_.back().depth = scope_depth_;
}

int Compiler::resolve_local(const std::string& name) const {
    for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; --i) {
        if (locals_[i].name == name) {
            if (locals_[i].depth == -1) {
                throw CompilerError("Cannot read local variable '" + name + 
                                  "' in its own initializer");
            }
            return i;
        }
    }
    return -1;
}

bool Compiler::is_exiting_stmt(Stmt* stmt) const {
    if (!stmt) {
        return false;
    }
    switch (stmt->kind) {
        case StmtKind::Return:
            return true;
        case StmtKind::Block: {
            auto* block = static_cast<BlockStmt*>(stmt);
            if (block->statements.empty()) {
                return false;
            }
            return is_exiting_stmt(block->statements.back().get());
        }
        case StmtKind::If: {
            auto* if_stmt = static_cast<IfStmt*>(stmt);
            if (!if_stmt->else_branch) {
                return false;
            }
            return is_exiting_stmt(if_stmt->then_branch.get())
                && is_exiting_stmt(if_stmt->else_branch.get());
        }
        default:
            return false;
    }
}

void Compiler::emit_op(OpCode op, uint32_t line) {
    chunk_.write_op(op, line);
}

void Compiler::emit_byte(uint8_t byte, uint32_t line) {
    chunk_.write(byte, line);
}

void Compiler::emit_short(uint16_t value, uint32_t line) {
    emit_byte(static_cast<uint8_t>((value >> 8) & 0xff), line);
    emit_byte(static_cast<uint8_t>(value & 0xff), line);
}

void Compiler::emit_constant(Value val, uint32_t line) {
    size_t idx = chunk_.add_constant(val);
    if (idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many constants", line);
    }
    emit_op(OpCode::OP_CONSTANT, line);
    emit_short(static_cast<uint16_t>(idx), line);
}

void Compiler::emit_string(const std::string& val, uint32_t line) {
    size_t idx = chunk_.add_string(val);
    if (idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many string constants", line);
    }
    emit_op(OpCode::OP_STRING, line);
    emit_short(static_cast<uint16_t>(idx), line);
}

size_t Compiler::emit_jump(OpCode op, uint32_t line) {
    return chunk_.emit_jump(op, line);
}

void Compiler::emit_loop(size_t loop_start, uint32_t line) {
    if (chunk_.code.size() < loop_start) {
        throw CompilerError("Invalid loop offset calculation", line);
    }
    
    emit_op(OpCode::OP_LOOP, line);
    size_t offset = chunk_.code.size() - loop_start + 2;
    
    if (offset > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Loop body too large", line);
    }
    emit_short(static_cast<uint16_t>(offset), line);
}

void Compiler::patch_jump(size_t offset) {
    chunk_.patch_jump(offset);
}

size_t Compiler::identifier_constant(const std::string& name) {
    return chunk_.add_string(name);
}

Chunk Compiler::compile_function_body(const FuncDeclStmt& stmt) {
    Compiler function_compiler;
    function_compiler.chunk_ = Chunk{};
    function_compiler.locals_.clear();
    function_compiler.scope_depth_ = 1;
    function_compiler.recursion_depth_ = 0;

    for (const auto& [param_name, param_type] : stmt.params) {
        function_compiler.declare_local(param_name, param_type.is_optional);
        function_compiler.mark_local_initialized();
    }

    if (stmt.body) {
        for (const auto& statement : stmt.body->statements) {
            function_compiler.compile_stmt(statement.get());
        }
    }

    function_compiler.emit_op(OpCode::OP_NIL, stmt.line);
    function_compiler.emit_op(OpCode::OP_RETURN, stmt.line);
    return std::move(function_compiler.chunk_);
}

} // namespace swiftscript
