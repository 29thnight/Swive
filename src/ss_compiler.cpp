#include "ss_compiler.hpp"
#include <limits>
#include <memory>
#include <stdexcept>

namespace swiftscript {

Chunk Compiler::compile(const std::vector<StmtPtr>& program) {
    chunk_ = Chunk{};
    locals_.clear();
    scope_depth_ = 0;

    for (const auto& stmt : program) {
        compile_stmt(stmt.get());
    }

    emit_op(OpCode::OP_NIL, 0);
    emit_op(OpCode::OP_HALT, 0);
    return chunk_;
}

void Compiler::compile_stmt(Stmt* stmt) {
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
        default:
            throw std::runtime_error("Unknown statement kind");
    }
}

void Compiler::compile_expr(Expr* expr) {
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
        default:
            throw std::runtime_error("Unknown expression kind");
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
    begin_scope();
    declare_local(stmt->binding_name, false);
    mark_local_initialized();
    compile_stmt(stmt->then_branch.get());
    end_scope();

    size_t end_jump = emit_jump(OpCode::OP_JUMP, stmt->line);
    patch_jump(else_jump);

    if (stmt->else_branch) {
        compile_stmt(stmt->else_branch.get());
    }
    patch_jump(end_jump);
}

void Compiler::visit(GuardLetStmt* stmt) {
    if (!is_exiting_stmt(stmt->else_branch.get())) {
        throw std::runtime_error("guard let requires else branch to exit.");
    }

    compile_expr(stmt->optional_expr.get());

    size_t else_jump = emit_jump(OpCode::OP_JUMP_IF_NIL, stmt->line);
    size_t locals_before = locals_.size();
    if (scope_depth_ == 0) {
        size_t name_idx = identifier_constant(stmt->binding_name);
        emit_op(OpCode::OP_SET_GLOBAL, stmt->line);
        emit_short(static_cast<uint16_t>(name_idx), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
    } else {
        declare_local(stmt->binding_name, false);
        mark_local_initialized();
    }

    size_t end_jump = emit_jump(OpCode::OP_JUMP, stmt->line);
    patch_jump(else_jump);

    auto saved_locals = locals_;
    locals_.resize(locals_before);
    compile_stmt(stmt->else_branch.get());
    locals_ = std::move(saved_locals);

    patch_jump(end_jump);
}

void Compiler::visit(WhileStmt* stmt) {
    size_t loop_start = chunk_.code.size();
    compile_expr(stmt->condition.get());

    size_t exit_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
    emit_op(OpCode::OP_POP, stmt->line);
    compile_stmt(stmt->body.get());
    emit_loop(loop_start, stmt->line);
    patch_jump(exit_jump);
    emit_op(OpCode::OP_POP, stmt->line);
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
        throw std::runtime_error("Too many functions in chunk.");
    }

    emit_op(OpCode::OP_FUNCTION, stmt->line);
    emit_short(static_cast<uint16_t>(function_index), stmt->line);

    if (scope_depth_ == 0) {
        size_t name_idx = identifier_constant(stmt->name);
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
            throw std::runtime_error("Unsupported unary operator");
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
            throw std::runtime_error("Unsupported binary operator");
    }
}

void Compiler::visit(AssignExpr* expr) {
    compile_expr(expr->value.get());

    int local = resolve_local(expr->name);
    if (local != -1) {
        emit_op(OpCode::OP_SET_LOCAL, expr->line);
        emit_short(static_cast<uint16_t>(local), expr->line);
        return;
    }

    size_t name_idx = identifier_constant(expr->name);
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
    size_t end_jump = emit_jump(OpCode::OP_JUMP, expr->line);
    patch_jump(else_jump);
    compile_expr(expr->fallback.get());
    patch_jump(end_jump);
}

void Compiler::visit(OptionalChainExpr* expr) {
    compile_expr(expr->object.get());
    size_t name_idx = identifier_constant(expr->member);
    emit_op(OpCode::OP_OPTIONAL_CHAIN, expr->line);
    emit_short(static_cast<uint16_t>(name_idx), expr->line);
}

void Compiler::visit(MemberExpr* expr) {
    compile_expr(expr->object.get());
    size_t name_idx = identifier_constant(expr->member);
    emit_op(OpCode::OP_GET_PROPERTY, expr->line);
    emit_short(static_cast<uint16_t>(name_idx), expr->line);
}

void Compiler::visit(CallExpr* expr) {
    compile_expr(expr->callee.get());

    if (expr->arguments.size() > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error("Too many arguments in function call.");
    }

    for (const auto& arg : expr->arguments) {
        compile_expr(arg.get());
    }

    emit_op(OpCode::OP_CALL, expr->line);
    emit_short(static_cast<uint16_t>(expr->arguments.size()), expr->line);
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

    for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
        if (it->depth != -1 && it->depth < scope_depth_) {
            break;
        }
        if (it->name == name) {
            throw std::runtime_error("Variable redeclared in same scope: " + name);
        }
    }

    locals_.push_back({name, -1, is_optional});
}

void Compiler::mark_local_initialized() {
    if (scope_depth_ == 0) {
        return;
    }
    locals_.back().depth = scope_depth_;
}

int Compiler::resolve_local(const std::string& name) const {
    for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; --i) {
        if (locals_[i].name == name) {
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
        throw std::runtime_error("Too many constants");
    }
    emit_op(OpCode::OP_CONSTANT, line);
    emit_short(static_cast<uint16_t>(idx), line);
}

void Compiler::emit_string(const std::string& val, uint32_t line) {
    size_t idx = chunk_.add_string(val);
    if (idx > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error("Too many string constants");
    }
    emit_op(OpCode::OP_STRING, line);
    emit_short(static_cast<uint16_t>(idx), line);
}

size_t Compiler::emit_jump(OpCode op, uint32_t line) {
    return chunk_.emit_jump(op, line);
}

void Compiler::emit_loop(size_t loop_start, uint32_t line) {
    emit_op(OpCode::OP_LOOP, line);
    size_t offset = chunk_.code.size() - loop_start + 2;
    if (offset > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error("Loop too large");
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
