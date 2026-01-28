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
        case StmtKind::ClassDecl:
            visit(static_cast<ClassDeclStmt*>(stmt));
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
        case StmtKind::Switch:
            visit(static_cast<SwitchStmt*>(stmt));
            break;
        default:
            throw CompilerError("Unknown statement kind", stmt->line);
    }
}

void Compiler::visit(ClassDeclStmt* stmt) {
    size_t name_idx = identifier_constant(stmt->name);
    emit_op(OpCode::OP_CLASS, stmt->line);
    emit_short(static_cast<uint16_t>(name_idx), stmt->line);
    if (scope_depth_ > 0) {
        declare_local(stmt->name, false);
        mark_local_initialized();
    }

    // Methods: class object remains on stack top
    for (const auto& method : stmt->methods) {
        FunctionPrototype proto;
        proto.name = method->name;
        proto.params.reserve(method->params.size() + 1);
        proto.params.push_back("self");
        for (const auto& [param_name, param_type] : method->params) {
            proto.params.push_back(param_name);
        }
        proto.is_initializer = (method->name == "init");

        Compiler method_compiler;
        method_compiler.enclosing_ = this;
        method_compiler.chunk_ = Chunk{};
        method_compiler.locals_.clear();
        method_compiler.scope_depth_ = 1;
        method_compiler.recursion_depth_ = 0;

        // Implicit self
        method_compiler.declare_local("self", false);
        method_compiler.mark_local_initialized();

        for (const auto& [param_name, param_type] : method->params) {
            method_compiler.declare_local(param_name, param_type.is_optional);
            method_compiler.mark_local_initialized();
        }

        if (method->body) {
            for (const auto& statement : method->body->statements) {
                method_compiler.compile_stmt(statement.get());
            }
        }

        method_compiler.emit_op(OpCode::OP_NIL, method->line);
        method_compiler.emit_op(OpCode::OP_RETURN, method->line);

        proto.chunk = std::make_shared<Chunk>(std::move(method_compiler.chunk_));
        proto.upvalues.reserve(method_compiler.upvalues_.size());
        for (const auto& uv : method_compiler.upvalues_) {
            proto.upvalues.push_back({uv.index, uv.is_local});
        }

        size_t function_index = chunk_.add_function(std::move(proto));
        if (function_index > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many functions in chunk", method->line);
        }

        bool has_captures = !method_compiler.upvalues_.empty();
        emit_op(has_captures ? OpCode::OP_CLOSURE : OpCode::OP_FUNCTION, method->line);
        emit_short(static_cast<uint16_t>(function_index), method->line);

        size_t method_name_idx = identifier_constant(method->name);
        if (method_name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many method identifiers", method->line);
        }
        emit_op(OpCode::OP_METHOD, method->line);
        emit_short(static_cast<uint16_t>(method_name_idx), method->line);
    }

    if (scope_depth_ == 0) {
        emit_op(OpCode::OP_SET_GLOBAL, stmt->line);
        emit_short(static_cast<uint16_t>(name_idx), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
    } else {
        int local = resolve_local(stmt->name);
        emit_op(OpCode::OP_SET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(local), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
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
        case ExprKind::Ternary:
            visit(static_cast<TernaryExpr*>(expr));
            break;
        case ExprKind::Closure:
            visit(static_cast<ClosureExpr*>(expr));
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
    // Check if iterable is a Range expression
    if (stmt->iterable->kind == ExprKind::Range) {
        // Range for-in loop: for i in 1...5 { }
        compile_expr(stmt->iterable.get());  // Push start, end
        
        RangeExpr* range = static_cast<RangeExpr*>(stmt->iterable.get());
        
        begin_scope();
        
        // Stack: [start, end]
        // Loop variable (slot 0 = start)
        declare_local(stmt->variable, false);
        mark_local_initialized();
        
        // End value (slot 1 = end)
        declare_local("$end", false);
        mark_local_initialized();
        
        loop_stack_.push_back({});
        size_t loop_start = chunk_.code.size();
        loop_stack_.back().loop_start = loop_start;
        loop_stack_.back().scope_depth_at_start = scope_depth_;
        
        // Condition: i < end (exclusive) or i <= end (inclusive)
        int loop_var_idx = resolve_local(stmt->variable);
        int end_var_idx = resolve_local("$end");
        
        emit_op(OpCode::OP_GET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(loop_var_idx), stmt->line);
        
        emit_op(OpCode::OP_GET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(end_var_idx), stmt->line);
        
        emit_op(range->inclusive ? OpCode::OP_LESS_EQUAL : OpCode::OP_LESS, stmt->line);
        
        size_t exit_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
        
        compile_stmt(stmt->body.get());
        
        // Patch continue jumps
        for (size_t jump : loop_stack_.back().continue_jumps) {
            patch_jump(jump);
        }
        
        // Increment i
        emit_op(OpCode::OP_GET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(loop_var_idx), stmt->line);
        emit_constant(Value::from_int(1), stmt->line);
        emit_op(OpCode::OP_ADD, stmt->line);
        emit_op(OpCode::OP_SET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(loop_var_idx), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
        
        emit_loop(loop_start, stmt->line);
        
        patch_jump(exit_jump);
        emit_op(OpCode::OP_POP, stmt->line);
        
        // Patch break jumps
        for (size_t jump : loop_stack_.back().break_jumps) {
            patch_jump(jump);
        }
        
        loop_stack_.pop_back();
        end_scope();
    } else {
        // Array for-in loop: for item in array { }
        // Evaluate iterable (push to stack)
        compile_expr(stmt->iterable.get());
        
        begin_scope();
        
        // Store array in local variable
        declare_local("$array", false);
        mark_local_initialized();
        
        // Initialize index to 0
        emit_constant(Value::from_int(0), stmt->line);
        declare_local("$index", false);
        mark_local_initialized();
        
        // Declare loop variable
        declare_local(stmt->variable, false);
        emit_op(OpCode::OP_NIL, stmt->line);
        mark_local_initialized();
        
        loop_stack_.push_back({});
        size_t loop_start = chunk_.code.size();
        loop_stack_.back().loop_start = loop_start;
        loop_stack_.back().scope_depth_at_start = scope_depth_;
        
        int array_idx = resolve_local("$array");
        int index_idx = resolve_local("$index");
        int loop_var_idx = resolve_local(stmt->variable);
        
        // Condition: index < array.count
        // Load array
        emit_op(OpCode::OP_GET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(array_idx), stmt->line);
        
        // Get array.count
        size_t count_name_idx = identifier_constant("count");
        emit_op(OpCode::OP_GET_PROPERTY, stmt->line);
        emit_short(static_cast<uint16_t>(count_name_idx), stmt->line);
        
        // Load index
        emit_op(OpCode::OP_GET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(index_idx), stmt->line);
        
        // count > index
        emit_op(OpCode::OP_GREATER, stmt->line);
        
        size_t exit_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
        
        // Assign loop_var = array[index]
        // Load array
        emit_op(OpCode::OP_GET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(array_idx), stmt->line);
        
        // Load index
        emit_op(OpCode::OP_GET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(index_idx), stmt->line);
        
        // array[index]
        emit_op(OpCode::OP_GET_SUBSCRIPT, stmt->line);
        
        // Store in loop variable
        emit_op(OpCode::OP_SET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(loop_var_idx), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
        
        // Execute loop body
        compile_stmt(stmt->body.get());
        
        // Patch continue jumps
        for (size_t jump : loop_stack_.back().continue_jumps) {
            patch_jump(jump);
        }
        
        // Increment index
        emit_op(OpCode::OP_GET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(index_idx), stmt->line);
        emit_constant(Value::from_int(1), stmt->line);
        emit_op(OpCode::OP_ADD, stmt->line);
        emit_op(OpCode::OP_SET_LOCAL, stmt->line);
        emit_short(static_cast<uint16_t>(index_idx), stmt->line);
        emit_op(OpCode::OP_POP, stmt->line);
        
        emit_loop(loop_start, stmt->line);
        
        patch_jump(exit_jump);
        emit_op(OpCode::OP_POP, stmt->line);
        
        // Patch break jumps
        for (size_t jump : loop_stack_.back().break_jumps) {
            patch_jump(jump);
        }
        
        loop_stack_.pop_back();
        end_scope();
    }
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
        emit_op(it->is_captured ? OpCode::OP_CLOSE_UPVALUE : OpCode::OP_POP, stmt->line);
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
        emit_op(it->is_captured ? OpCode::OP_CLOSE_UPVALUE : OpCode::OP_POP, stmt->line);
    }
    
    size_t jump = emit_jump(OpCode::OP_JUMP, stmt->line);
    loop_stack_.back().continue_jumps.push_back(jump);
}

void Compiler::visit(SwitchStmt* stmt) {
    // Evaluate and store switch value
    compile_expr(stmt->value.get());
    
    begin_scope();
    declare_local("$switch", false);
    mark_local_initialized();
    
    int switch_var = resolve_local("$switch");
    std::vector<size_t> end_jumps;
    
    // Compile each case
    for (const auto& case_clause : stmt->cases) {
        if (case_clause.is_default) {
            // Default case: always executes if reached
            for (const auto& case_stmt : case_clause.statements) {
                compile_stmt(case_stmt.get());
            }
            break;  // Default is always last
        }
        
        // For multiple patterns, we need OR logic:
        // if pattern1 matches OR pattern2 matches OR ... then execute body
        std::vector<size_t> match_jumps;  // Jump to body if matched
        
        for (size_t i = 0; i < case_clause.patterns.size(); ++i) {
            const auto& pattern = case_clause.patterns[i];
            
            // Load switch value
            emit_op(OpCode::OP_GET_LOCAL, stmt->line);
            emit_short(static_cast<uint16_t>(switch_var), stmt->line);
            
            // Check if pattern is a range
            if (pattern->kind == ExprKind::Range) {
                auto* range = static_cast<RangeExpr*>(pattern.get());
                
                // value >= start
                emit_op(OpCode::OP_GET_LOCAL, stmt->line);
                emit_short(static_cast<uint16_t>(switch_var), stmt->line);
                compile_expr(range->start.get());
                emit_op(OpCode::OP_GREATER_EQUAL, stmt->line);
                
                // value <= end (or < for exclusive)
                emit_op(OpCode::OP_GET_LOCAL, stmt->line);
                emit_short(static_cast<uint16_t>(switch_var), stmt->line);
                compile_expr(range->end.get());
                emit_op(range->inclusive ? OpCode::OP_LESS_EQUAL : OpCode::OP_LESS, stmt->line);
                
                // Both must be true
                emit_op(OpCode::OP_AND, stmt->line);
                
                // Pop the extra switch value we loaded
                // (we loaded it twice but only need once for the final check)
            } else {
                // Simple value comparison
                compile_expr(pattern.get());
                emit_op(OpCode::OP_EQUAL, stmt->line);
            }
            
            // If matched, remember to jump to body
            size_t match = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
            emit_op(OpCode::OP_POP, stmt->line);
            
            // Matched! Jump to case body
            size_t to_body = emit_jump(OpCode::OP_JUMP, stmt->line);
            match_jumps.push_back(to_body);
            
            // Not matched, try next pattern
            patch_jump(match);
            emit_op(OpCode::OP_POP, stmt->line);
        }
        
        // No pattern matched, skip to next case
        size_t next_case = emit_jump(OpCode::OP_JUMP, stmt->line);
        
        // Patch all match jumps to here (case body)
        for (size_t jump : match_jumps) {
            patch_jump(jump);
        }
        
        // Execute case body
        for (const auto& case_stmt : case_clause.statements) {
            compile_stmt(case_stmt.get());
        }
        
        // Jump to end (no fall-through)
        size_t to_end = emit_jump(OpCode::OP_JUMP, stmt->line);
        end_jumps.push_back(to_end);
        
        // Patch next case jump
        patch_jump(next_case);
    }
    
    // Patch all end jumps
    for (size_t jump : end_jumps) {
        patch_jump(jump);
    }
    
    end_scope();  // Pops $switch value
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

    Compiler function_compiler;
    function_compiler.enclosing_ = this;
    function_compiler.chunk_ = Chunk{};
    function_compiler.locals_.clear();
    function_compiler.scope_depth_ = 1;
    function_compiler.recursion_depth_ = 0;

    for (const auto& [param_name, param_type] : stmt->params) {
        function_compiler.declare_local(param_name, param_type.is_optional);
        function_compiler.mark_local_initialized();
    }

    if (stmt->body) {
        for (const auto& statement : stmt->body->statements) {
            function_compiler.compile_stmt(statement.get());
        }
    }

    function_compiler.emit_op(OpCode::OP_NIL, stmt->line);
    function_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

    proto.chunk = std::make_shared<Chunk>(std::move(function_compiler.chunk_));
    proto.upvalues.reserve(function_compiler.upvalues_.size());
    for (const auto& uv : function_compiler.upvalues_) {
        proto.upvalues.push_back({uv.index, uv.is_local});
    }

    size_t function_index = chunk_.add_function(std::move(proto));
    if (function_index > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many functions in chunk", stmt->line);
    }

    bool has_captures = !function_compiler.upvalues_.empty();
    emit_op(has_captures ? OpCode::OP_CLOSURE : OpCode::OP_FUNCTION, stmt->line);
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
    
    int upvalue = resolve_upvalue(expr->name);
    if (upvalue != -1) {
        emit_op(OpCode::OP_GET_UPVALUE, expr->line);
        emit_short(static_cast<uint16_t>(upvalue), expr->line);
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
        // x += 5 -> x = x + 5 로 변환
        int local = resolve_local(expr->name);
        if (local != -1) {
            emit_op(OpCode::OP_GET_LOCAL, expr->line);
            emit_short(static_cast<uint16_t>(local), expr->line);
        } else {
            int upvalue = resolve_upvalue(expr->name);
            if (upvalue != -1) {
                emit_op(OpCode::OP_GET_UPVALUE, expr->line);
                emit_short(static_cast<uint16_t>(upvalue), expr->line);
            } else {
                size_t name_idx = identifier_constant(expr->name);
                if (name_idx > std::numeric_limits<uint16_t>::max()) {
                    throw CompilerError("Too many identifiers", expr->line);
                }
                emit_op(OpCode::OP_GET_GLOBAL, expr->line);
                emit_short(static_cast<uint16_t>(name_idx), expr->line);
            }
        }
        
        // 오른쪽 값 컴파일
        compile_expr(expr->value.get());
        
        // 연산 실행
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
        // 일반 할당
        compile_expr(expr->value.get());
    }

    // ��� ����
    int local = resolve_local(expr->name);
    if (local != -1) {
        emit_op(OpCode::OP_SET_LOCAL, expr->line);
        emit_short(static_cast<uint16_t>(local), expr->line);
        return;
    }
    
    int upvalue = resolve_upvalue(expr->name);
    if (upvalue != -1) {
        emit_op(OpCode::OP_SET_UPVALUE, expr->line);
        emit_short(static_cast<uint16_t>(upvalue), expr->line);
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

void Compiler::visit(TernaryExpr* expr) {
    compile_expr(expr->condition.get());

    size_t else_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, expr->line);
    emit_op(OpCode::OP_POP, expr->line);
    compile_expr(expr->then_expr.get());

    size_t end_jump = emit_jump(OpCode::OP_JUMP, expr->line);
    patch_jump(else_jump);
    emit_op(OpCode::OP_POP, expr->line);
    compile_expr(expr->else_expr.get());

    patch_jump(end_jump);
}

void Compiler::visit(ClosureExpr* expr) {
    // Closures are compiled similar to functions but can capture outer locals
    FunctionPrototype proto;
    proto.name = "<closure>";
    proto.params.reserve(expr->params.size());
    for (const auto& [param_name, param_type] : expr->params) {
        proto.params.push_back(param_name);
    }

    Compiler closure_compiler;
    closure_compiler.enclosing_ = this;
    closure_compiler.chunk_ = Chunk{};
    closure_compiler.locals_.clear();
    closure_compiler.scope_depth_ = 1;
    closure_compiler.recursion_depth_ = 0;

    for (const auto& [param_name, param_type] : expr->params) {
        closure_compiler.declare_local(param_name, param_type.is_optional);
        closure_compiler.mark_local_initialized();
    }

    for (const auto& stmt : expr->body) {
        closure_compiler.compile_stmt(stmt.get());
    }

    closure_compiler.emit_op(OpCode::OP_NIL, expr->line);
    closure_compiler.emit_op(OpCode::OP_RETURN, expr->line);

    proto.chunk = std::make_shared<Chunk>(std::move(closure_compiler.chunk_));
    proto.upvalues.reserve(closure_compiler.upvalues_.size());
    for (const auto& uv : closure_compiler.upvalues_) {
        proto.upvalues.push_back({uv.index, uv.is_local});
    }

    size_t function_index = chunk_.add_function(std::move(proto));
    if (function_index > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many functions in chunk", expr->line);
    }

    emit_op(OpCode::OP_CLOSURE, expr->line);
    emit_short(static_cast<uint16_t>(function_index), expr->line);
}

void Compiler::begin_scope() {
    scope_depth_++;
}

void Compiler::end_scope() {
    scope_depth_--;
    while (!locals_.empty() && locals_.back().depth > scope_depth_) {
        if (locals_.back().is_captured) {
            emit_op(OpCode::OP_CLOSE_UPVALUE, 0);
        } else {
            emit_op(OpCode::OP_POP, 0);
        }
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

    locals_.push_back({name, -1, is_optional, false});
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

int Compiler::resolve_upvalue(const std::string& name) {
    if (enclosing_ == nullptr) {
        return -1;
    }
    
    // Check if the variable is a local in the enclosing scope
    int local = enclosing_->resolve_local(name);
    if (local != -1) {
        enclosing_->locals_[local].is_captured = true;
        return add_upvalue(static_cast<uint16_t>(local), true);
    }
    
    // Check if the variable is an upvalue in the enclosing scope
    int upvalue = enclosing_->resolve_upvalue(name);
    if (upvalue != -1) {
        return add_upvalue(static_cast<uint16_t>(upvalue), false);
    }
    
    return -1;
}

int Compiler::add_upvalue(uint16_t index, bool is_local) {
    // Check if this upvalue already exists
    for (size_t i = 0; i < upvalues_.size(); ++i) {
        if (upvalues_[i].index == index && upvalues_[i].is_local == is_local) {
            return static_cast<int>(i);
        }
    }
    
    if (upvalues_.size() >= MAX_UPVALUES) {
        throw CompilerError("Too many captured variables in closure");
    }
    
    upvalues_.push_back({index, is_local});
    return static_cast<int>(upvalues_.size() - 1);
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
