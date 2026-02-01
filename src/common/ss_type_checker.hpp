#pragma once

#include "ss_ast.hpp"

namespace swiftscript {

struct IModuleResolver;

class TypeCheckError : public std::runtime_error {
public:
    TypeCheckError(const std::string& msg, uint32_t line = 0)
        : std::runtime_error(line > 0 ? msg + " (line " + std::to_string(line) + ")" : msg)
        , line_(line) {}

    uint32_t line() const { return line_; }

private:
    uint32_t line_{};
};

class TypeChecker {
public:
    void check(const std::vector<StmtPtr>& program);
    void set_base_directory(const std::string& dir) { base_directory_ = dir; }
    void set_module_resolver(IModuleResolver* resolver) { module_resolver_ = resolver; }

private:
    enum class TypeKind {
        Unknown,
        Builtin,
        User,
        Protocol,
        Function,
        GenericParameter,
        Tuple,
    };

    // Forward declaration
    struct TypeInfo;

    struct TupleElementInfo {
        std::optional<std::string> label;
        std::shared_ptr<TypeInfo> type;  // Use shared_ptr due to forward declaration
    };

    struct TypeInfo {
        std::string name;
        bool is_optional{false};
        TypeKind kind{TypeKind::Unknown};
        std::vector<TypeInfo> param_types;
        std::shared_ptr<TypeInfo> return_type;
        std::vector<TupleElementInfo> tuple_elements;  // For tuple types

        static TypeInfo unknown();
        static TypeInfo builtin(std::string name, bool optional = false);
        static TypeInfo user(std::string name, bool optional = false);
        static TypeInfo protocol(std::string name, bool optional = false);
        static TypeInfo function(std::vector<TypeInfo> params, TypeInfo result);
        static TypeInfo generic(std::string name, bool optional = false);
        static TypeInfo tuple(std::vector<TupleElementInfo> elements);
    };

    struct FunctionContext {
        TypeInfo return_type;
    };

    std::unordered_map<std::string, TypeKind> known_types_;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeInfo>> type_properties_;
    std::unordered_map<std::string, std::unordered_map<std::string, TypeInfo>> type_methods_;
    std::unordered_map<std::string, std::unordered_map<std::string, AccessLevel>> member_access_levels_;  // Track access levels
    std::unordered_map<std::string, std::unordered_set<std::string>> mutating_methods_;  // Track mutating methods
    std::unordered_map<std::string, std::unordered_set<std::string>> enum_cases_;
    std::unordered_map<std::string, std::string> superclass_map_;
    std::unordered_map<std::string, std::unordered_set<std::string>> protocol_conformers_;
    std::unordered_map<std::string, std::unordered_set<std::string>> protocol_inheritance_;
    std::unordered_map<std::string, std::unordered_set<std::string>> protocol_descendants_;

    std::vector<std::unordered_map<std::string, TypeInfo>> scopes_;
    std::vector<FunctionContext> function_stack_;
    std::vector<std::unordered_set<std::string>> generic_param_stack_;
    std::unordered_set<std::string> let_constants_;  // Track let constants
    std::string current_type_context_;  // Track which type we're currently inside (for access control)
    mutable std::vector<TypeCheckError> errors_;
    mutable std::vector<std::string> warnings_;
    std::string base_directory_;
    IModuleResolver* module_resolver_{nullptr};
    std::unordered_map<std::string, std::vector<StmtPtr>> module_cache_;
    std::unordered_set<std::string> imported_modules_;
    std::unordered_set<std::string> compiling_modules_;
    std::vector<std::string> imported_module_names_;
    std::unordered_set<std::string> known_attributes_;
    
    // Generic templates storage
    std::unordered_map<std::string, const StructDeclStmt*> generic_struct_templates_;

    void collect_type_declarations(const std::vector<StmtPtr>& program);
    void collect_imported_programs(const std::vector<StmtPtr>& program,
                                   std::vector<const std::vector<StmtPtr>*>& ordered_programs);
    const std::vector<StmtPtr>& load_module_program(const std::string& module_key, uint32_t line);
    void declare_functions(const std::vector<StmtPtr>& program);
    static std::string module_symbol_name(const std::string& module_key);
    void add_builtin_types();
    void add_builtin_attributes();
    void register_attribute(const std::string& name, uint32_t line);
    void add_known_type(const std::string& name, TypeKind kind, uint32_t line);
    void add_protocol_inheritance(const std::string& protocol, const std::vector<std::string>& parents);
    void add_protocol_conformance(const std::string& type_name, const std::vector<std::string>& protocols, uint32_t line);
    void finalize_protocol_maps();

    void enter_scope();
    void exit_scope();
    void enter_generic_params(const std::vector<std::string>& params);
    void exit_generic_params();
    bool is_generic_param(const std::string& name) const;
    void declare_symbol(const std::string& name, const TypeInfo& type, uint32_t line, bool is_let = false);
    TypeInfo lookup_symbol(const std::string& name, uint32_t line) const;
    bool has_symbol(const std::string& name) const;

    void check_stmt(const Stmt* stmt);
    void check_block(const BlockStmt* stmt);
    void check_var_decl(const VarDeclStmt* stmt);
    void check_tuple_destructuring(const TupleDestructuringStmt* stmt);
    void check_if_stmt(const IfStmt* stmt);
    void check_if_let_stmt(const IfLetStmt* stmt);
    void check_guard_let_stmt(const GuardLetStmt* stmt);
    void check_while_stmt(const WhileStmt* stmt);
    void check_repeat_while_stmt(const RepeatWhileStmt* stmt);
    void check_for_in_stmt(const ForInStmt* stmt);
    void check_switch_stmt(const SwitchStmt* stmt);
    void check_return_stmt(const ReturnStmt* stmt);
    void check_throw_stmt(const ThrowStmt* stmt);
    void check_func_decl(const FuncDeclStmt* stmt, const std::string& self_type = "");
    void check_class_decl(const ClassDeclStmt* stmt);
    void check_struct_decl(const StructDeclStmt* stmt);
    void check_enum_decl(const EnumDeclStmt* stmt);
    void check_protocol_decl(const ProtocolDeclStmt* stmt);
    void check_extension_decl(const ExtensionDeclStmt* stmt);
    void check_do_catch_stmt(const DoCatchStmt* stmt);
    void check_attributes(const std::vector<Attribute>& attributes, uint32_t line);

    TypeInfo check_expr(const Expr* expr);
    TypeInfo check_literal_expr(const LiteralExpr* expr);
    TypeInfo check_identifier_expr(const IdentifierExpr* expr);
    TypeInfo check_unary_expr(const UnaryExpr* expr);
    TypeInfo check_binary_expr(const BinaryExpr* expr);
    TypeInfo check_assign_expr(const AssignExpr* expr);
    TypeInfo check_call_expr(const CallExpr* expr);
    TypeInfo check_member_expr(const MemberExpr* expr);
    TypeInfo check_optional_chain_expr(const OptionalChainExpr* expr);
    TypeInfo check_nil_coalesce_expr(const NilCoalesceExpr* expr);
    TypeInfo check_force_unwrap_expr(const ForceUnwrapExpr* expr);
    TypeInfo check_range_expr(const RangeExpr* expr);
    TypeInfo check_array_literal_expr(const ArrayLiteralExpr* expr);
    TypeInfo check_dict_literal_expr(const DictLiteralExpr* expr);
    TypeInfo check_subscript_expr(const SubscriptExpr* expr);
    TypeInfo check_ternary_expr(const TernaryExpr* expr);
    TypeInfo check_closure_expr(const ClosureExpr* expr);
    TypeInfo check_type_cast_expr(const TypeCastExpr* expr);
    TypeInfo check_type_check_expr(const TypeCheckExpr* expr);
    TypeInfo check_try_expr(const TryExpr* expr);
    TypeInfo check_tuple_literal_expr(const TupleLiteralExpr* expr);
    TypeInfo check_tuple_member_expr(const TupleMemberExpr* expr);

    TypeInfo type_from_annotation(const TypeAnnotation& annotation, uint32_t line);
    bool is_assignable(const TypeInfo& expected, const TypeInfo& actual) const;
    bool is_numeric(const TypeInfo& type) const;
    bool is_bool(const TypeInfo& type) const;
    bool is_string(const TypeInfo& type) const;
    bool is_nil(const TypeInfo& type) const;
    bool is_unknown(const TypeInfo& type) const;
    TypeInfo make_optional(const TypeInfo& type) const;
    TypeInfo base_type(const TypeInfo& type) const;
    bool protocol_conforms(const std::string& type_name, const std::string& protocol_name) const;
    bool protocol_inherits(const std::string& protocol_name, const std::string& ancestor) const;
    bool is_subclass_of(const std::string& subclass, const std::string& superclass) const;
    
    // Generic specialization helpers
    std::string mangle_generic_name(const std::string& base_name, const std::vector<TypeAnnotation>& type_args) const;
    void specialize_generic_struct(const std::string& base_name, const std::vector<TypeAnnotation>& type_args, uint32_t line);

    void error(const std::string& message, uint32_t line) const;
    void warn(const std::string& message, uint32_t line) const;
    void emit_warnings() const;
    void throw_if_errors();
};

} // namespace swiftscript
