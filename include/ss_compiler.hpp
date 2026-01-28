#pragma once

#include "ss_ast.hpp"
#include "ss_chunk.hpp"
#include <string>
#include <vector>

namespace swiftscript {

class Compiler {
public:
    Chunk compile(const std::vector<StmtPtr>& program);

private:
    Chunk chunk_;

    struct Local {
        std::string name;
        int depth;
        bool is_optional;
    };

    std::vector<Local> locals_;
    int scope_depth_{0};

    void compile_stmt(Stmt* stmt);
    void compile_expr(Expr* expr);

    void visit(VarDeclStmt* stmt);
    void visit(IfStmt* stmt);
    void visit(IfLetStmt* stmt);
    void visit(GuardLetStmt* stmt);
    void visit(WhileStmt* stmt);
    void visit(BlockStmt* stmt);
    void visit(PrintStmt* stmt);
    void visit(ReturnStmt* stmt);
    void visit(FuncDeclStmt* stmt);
    void visit(ExprStmt* stmt);

    void visit(LiteralExpr* expr);
    void visit(IdentifierExpr* expr);
    void visit(UnaryExpr* expr);
    void visit(BinaryExpr* expr);
    void visit(AssignExpr* expr);
    void visit(ForceUnwrapExpr* expr);
    void visit(NilCoalesceExpr* expr);
    void visit(OptionalChainExpr* expr);
    void visit(MemberExpr* expr);
    void visit(CallExpr* expr);

    void begin_scope();
    void end_scope();
    void declare_local(const std::string& name, bool is_optional);
    void mark_local_initialized();
    int resolve_local(const std::string& name) const;
    bool is_exiting_stmt(Stmt* stmt) const;

    void emit_op(OpCode op, uint32_t line);
    void emit_byte(uint8_t byte, uint32_t line);
    void emit_short(uint16_t value, uint32_t line);
    void emit_constant(Value val, uint32_t line);
    void emit_string(const std::string& val, uint32_t line);
    size_t emit_jump(OpCode op, uint32_t line);
    void emit_loop(size_t loop_start, uint32_t line);
    void patch_jump(size_t offset);

    size_t identifier_constant(const std::string& name);
};

} // namespace swiftscript
