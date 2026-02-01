#pragma once

#include "ss_ast.hpp"
#include "ss_chunk.hpp"

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

struct IModuleResolver {
    virtual ~IModuleResolver() = default;

    // module_name: "math" or "foo/bar"
    // out_full_path: resolved file path
    // out_source: loaded source text (cached possible)
    virtual bool ResolveAndLoad(const std::string& module_name,
                                std::string& out_full_path,
                                std::string& out_source,
                                std::string& out_error) = 0;
};

struct EntryMainInfo {
    enum class Kind { None, GlobalFunc, StaticMethod } kind{Kind::None};
    std::string type_name; // Used only when Kind is StaticMethod
    int line{0};
};

class Compiler {
public:
    Assembly compile(const std::vector<StmtPtr>& program);
    
    // Set base directory for resolving relative import paths
    void set_base_directory(const std::string& dir) { base_directory_ = dir; }
    void set_module_resolver(IModuleResolver* r) { module_resolver_ = r; }

private:
    Assembly chunk_;
    std::string base_directory_;  // Base directory for resolving imports
    IModuleResolver* module_resolver_{nullptr};
    std::unordered_set<std::string> imported_modules_;  // Track imported modules to prevent duplicates
    std::unordered_set<std::string> compiling_modules_; // Track modules being compiled (circular dependency detection)
    
    // Generic specialization
    std::unordered_map<std::string, const StructDeclStmt*> generic_struct_templates_;
    std::vector<StmtPtr> specialize_generics(const std::vector<StmtPtr>& program);
    StmtPtr create_specialized_struct(const StructDeclStmt* template_decl, 
                                      const std::vector<TypeAnnotation>& type_args);
    std::string mangle_generic_name(const std::string& base_name, 
                                     const std::vector<TypeAnnotation>& type_args);
    void collect_generic_templates(const std::vector<StmtPtr>& program);
    void collect_generic_usages(const std::vector<StmtPtr>& program, 
                                std::unordered_set<std::string>& needed_specializations);

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
    void visit(TupleDestructuringStmt* stmt);
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

    // Entry point handling
    EntryMainInfo entry_main_;
    void record_entry_main_global(const FuncDeclStmt* stmt);
    void record_entry_main_static(const std::string& type_name, int line);
    void emit_auto_entry_main_call();
    void emit_module_namespace(const std::string& module_key,
                               const std::vector<std::string>& exports,
                               uint32_t line);

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
    void visit(TupleLiteralExpr* expr);
    void visit(TupleMemberExpr* expr);
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

    struct MethodBodyRecord {
        body_idx body{0};
    };

    std::unordered_map<std::string, MethodBodyRecord> method_body_lookup_;

    std::string build_method_key(const std::string& type_name,
                                 const std::string& method_name,
                                 bool is_static,
                                 const std::vector<TypeAnnotation>& param_types) const;
    std::string build_method_key(const std::string& type_name,
                                 const std::string& method_name,
                                 bool is_static,
                                 const std::vector<ParamDecl>& params) const;
    std::vector<TypeAnnotation> extract_param_types(const std::vector<ParamDecl>& params) const;
    std::vector<TypeAnnotation> build_accessor_param_types(const std::optional<TypeAnnotation>& type) const;
    body_idx store_method_body(const Assembly& body_chunk);
    void record_method_body(const std::string& type_name,
                            const std::string& method_name,
                            bool is_static,
                            const std::vector<TypeAnnotation>& param_types,
                            const Assembly& body_chunk);

    Assembly compile_function_body(const FuncDeclStmt& stmt);
    Assembly compile_struct_method_body(const StructMethodDecl& method, bool is_mutating);
    std::shared_ptr<Assembly> finalize_function_chunk(Assembly&& chunk);
    void populate_metadata_tables(const std::vector<StmtPtr>& program);

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
