#pragma once

#include "ss_ast.hpp"
#include "ss_chunk.hpp"
#include <string>
#include <vector>
#include <stdexcept>

namespace swiftscript {

class CompilerError : public std::runtime_error {
public:
    CompilerError(const std::string& msg, uint32_t line = 0)
        : std::runtime_error(line > 0 ? msg + " (line " + std::to_string(line) + ")" : msg)
        , line_(line) {}
    
    uint32_t line() const { return line_; }

private:
    uint32_t line_;
};

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
    int recursion_depth_{0};

    static constexpr int MAX_RECURSION_DEPTH = 256;
    static constexpr size_t MAX_LOCALS = 65535;

    // �߰�: ���� ���ؽ�Ʈ
    struct LoopContext {
        std::vector<size_t> break_jumps;
        std::vector<size_t> continue_jumps;
        size_t loop_start;
        int scope_depth_at_start;
    };
    
    std::vector<LoopContext> loop_stack_;

    void compile_stmt(Stmt* stmt);
    void compile_expr(Expr* expr);

    void visit(VarDeclStmt* stmt);
    void visit(IfStmt* stmt);
    void visit(IfLetStmt* stmt);
    void visit(GuardLetStmt* stmt);
    void visit(WhileStmt* stmt);
    void visit(ForInStmt* stmt);      // �߰�
    void visit(BreakStmt* stmt);      // �߰�
    void visit(ContinueStmt* stmt);   // �߰�
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
    void visit(RangeExpr* expr);
    void visit(ArrayLiteralExpr* expr);
    void visit(DictLiteralExpr* expr);
    void visit(SubscriptExpr* expr);

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
    Chunk compile_function_body(const FuncDeclStmt& stmt);

    // Helper classes
    class RecursionGuard {
    public:
        explicit RecursionGuard(Compiler& compiler) : compiler_(compiler) {
            if (++compiler_.recursion_depth_ > MAX_RECURSION_DEPTH) {
                throw CompilerError("Maximum recursion depth exceeded");
            }
        }
        ~RecursionGuard() {
            --compiler_.recursion_depth_;
        }
        RecursionGuard(const RecursionGuard&) = delete;
        RecursionGuard& operator=(const RecursionGuard&) = delete;
    private:
        Compiler& compiler_;
    };
};

} // namespace swiftscript
