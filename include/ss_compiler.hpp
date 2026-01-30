#pragma once

#include "ss_ast.hpp"
#include "ss_chunk.hpp"
#include <string>
#include <vector>
#include <stdexcept>
#include <unordered_set>

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
    
    // Set base directory for resolving relative import paths
    void set_base_directory(const std::string& dir) { base_directory_ = dir; }

private:
    Chunk chunk_;
    std::string base_directory_;  // Base directory for resolving imports
    std::unordered_set<std::string> imported_modules_;  // Track imported modules to prevent duplicates
    std::unordered_set<std::string> compiling_modules_; // Track modules being compiled (circular dependency detection)

    struct Local {
        std::string name;
        int depth;
        bool is_optional;
        bool is_captured{false};  // True if captured by closure
    };

    struct Upvalue {
        uint16_t index;
        bool is_local;
    };

    std::vector<Local> locals_;
    std::vector<Upvalue> upvalues_;
    int scope_depth_{0};
    int recursion_depth_{0};
    Compiler* enclosing_{nullptr};  // For nested function/closure compilation
    const std::unordered_set<std::string>* current_class_properties_{nullptr};
    bool allow_implicit_self_property_{false};
    bool current_class_has_super_{false};
    bool in_struct_method_{false};      // True when compiling struct method
    bool in_mutating_method_{false};    // True when compiling mutating method

    static constexpr int MAX_RECURSION_DEPTH = 256;
    static constexpr size_t MAX_LOCALS = 65535;
    static constexpr size_t MAX_UPVALUES = 256;

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
    void visit(RepeatWhileStmt* stmt);
    void visit(ForInStmt* stmt);
    void visit(BreakStmt* stmt);
    void visit(ContinueStmt* stmt);
    void visit(SwitchStmt* stmt);
    void visit(BlockStmt* stmt);
    void visit(ThrowStmt* stmt);
    void visit(ClassDeclStmt* stmt);
    void visit(StructDeclStmt* stmt);  // Struct declaration
    void visit(EnumDeclStmt* stmt);    // Enum declaration
    void visit(ProtocolDeclStmt* stmt); // Protocol declaration
    void visit(ExtensionDeclStmt* stmt); // Extension declaration
    void visit(ImportStmt* stmt);
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
    void visit(SuperExpr* expr);
    void visit(CallExpr* expr);
    void visit(RangeExpr* expr);
    void visit(ArrayLiteralExpr* expr);
    void visit(DictLiteralExpr* expr);
    void visit(SubscriptExpr* expr);
    void visit(TernaryExpr* expr);
    void visit(ClosureExpr* expr);
    void visit(TypeCastExpr* expr);
    void visit(TypeCheckExpr* expr);

    void begin_scope();
    void end_scope();
    void declare_local(const std::string& name, bool is_optional);
    void mark_local_initialized();
    int resolve_local(const std::string& name) const;
    int resolve_upvalue(const std::string& name);
    int add_upvalue(uint16_t index, bool is_local);
    bool is_exiting_stmt(Stmt* stmt) const;
    bool is_implicit_property(const std::string& name) const;
    int resolve_self_index() const;
    void emit_self_property_get(const std::string& name, uint32_t line);
    void emit_self_property_set(const std::string& name, uint32_t line);
    void emit_load_self(uint32_t line);
    void emit_variable_get(const std::string& name, uint32_t line);
    FunctionPrototype::ParamDefaultValue build_param_default(const ParamDecl& param);

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
    Chunk compile_struct_method_body(const StructMethodDecl& method, bool is_mutating);

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
