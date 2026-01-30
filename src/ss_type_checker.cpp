#include "ss_type_checker.hpp"

#include <algorithm>
#include <sstream>

namespace swiftscript {

TypeChecker::TypeInfo TypeChecker::TypeInfo::unknown() {
    return TypeInfo{"Unknown", false, TypeKind::Unknown, {}, nullptr};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::builtin(std::string name, bool optional) {
    return TypeInfo{std::move(name), optional, TypeKind::Builtin, {}, nullptr};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::user(std::string name, bool optional) {
    return TypeInfo{std::move(name), optional, TypeKind::User, {}, nullptr};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::protocol(std::string name, bool optional) {
    return TypeInfo{std::move(name), optional, TypeKind::Protocol, {}, nullptr};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::function(std::vector<TypeInfo> params, TypeInfo result) {
    TypeInfo info{"Function", false, TypeKind::Function, std::move(params), nullptr};
    info.return_type = std::make_shared<TypeInfo>(std::move(result));
    return info;
}

void TypeChecker::check(const std::vector<StmtPtr>& program) {
    known_types_.clear();
    protocol_conformers_.clear();
    protocol_inheritance_.clear();
    protocol_descendants_.clear();
    scopes_.clear();
    function_stack_.clear();

    add_builtin_types();
    collect_type_declarations(program);
    finalize_protocol_maps();

    enter_scope();
    for (const auto& [name, kind] : known_types_) {
        if (kind == TypeKind::User || kind == TypeKind::Protocol) {
            declare_symbol(name, TypeInfo{name, false, kind, {}, nullptr}, 0);
        }
    }
    for (const auto& stmt : program) {
        if (stmt && stmt->kind == StmtKind::FuncDecl) {
            const auto* func = static_cast<const FuncDeclStmt*>(stmt.get());
            std::vector<TypeInfo> params;
            params.reserve(func->params.size());
            for (const auto& [_, annotation] : func->params) {
                params.push_back(type_from_annotation(annotation, func->line));
            }
            TypeInfo return_type = TypeInfo::builtin("Void");
            if (func->return_type.has_value()) {
                return_type = type_from_annotation(func->return_type.value(), func->line);
            }
            declare_symbol(func->name, TypeInfo::function(params, return_type), func->line);
        }
    }
    for (const auto& stmt : program) {
        if (!stmt) {
            error("Null statement in program", 0);
        }
        check_stmt(stmt.get());
    }
    exit_scope();
}

void TypeChecker::add_builtin_types() {
    known_types_.emplace("Int", TypeKind::Builtin);
    known_types_.emplace("Float", TypeKind::Builtin);
    known_types_.emplace("Bool", TypeKind::Builtin);
    known_types_.emplace("String", TypeKind::Builtin);
    known_types_.emplace("Void", TypeKind::Builtin);
    known_types_.emplace("Any", TypeKind::Builtin);
}

void TypeChecker::add_known_type(const std::string& name, TypeKind kind, uint32_t line) {
    if (known_types_.contains(name)) {
        return;
    }
    known_types_.emplace(name, kind);
    if (kind == TypeKind::Protocol) {
        protocol_conformers_.emplace(name, std::unordered_set<std::string>{});
        protocol_inheritance_.emplace(name, std::unordered_set<std::string>{});
    }
}

void TypeChecker::add_protocol_inheritance(const std::string& protocol, const std::vector<std::string>& parents) {
    if (!protocol_inheritance_.contains(protocol)) {
        protocol_inheritance_[protocol] = {};
    }
    auto& entry = protocol_inheritance_[protocol];
    for (const auto& parent : parents) {
        entry.insert(parent);
    }
}

void TypeChecker::add_protocol_conformance(const std::string& type_name, const std::vector<std::string>& protocols, uint32_t line) {
    for (const auto& protocol_name : protocols) {
        auto it = known_types_.find(protocol_name);
        if (it == known_types_.end() || it->second != TypeKind::Protocol) {
            error("Unknown protocol type '" + protocol_name + "'", line);
        }
        protocol_conformers_[protocol_name].insert(type_name);
    }
}

void TypeChecker::collect_type_declarations(const std::vector<StmtPtr>& program) {
    for (const auto& stmt_ptr : program) {
        if (!stmt_ptr) {
            continue;
        }
        const Stmt* stmt = stmt_ptr.get();
        switch (stmt->kind) {
            case StmtKind::ClassDecl: {
                auto* decl = static_cast<const ClassDeclStmt*>(stmt);
                add_known_type(decl->name, TypeKind::User, decl->line);
                add_protocol_conformance(decl->name, decl->protocol_conformances, decl->line);
                break;
            }
            case StmtKind::StructDecl: {
                auto* decl = static_cast<const StructDeclStmt*>(stmt);
                add_known_type(decl->name, TypeKind::User, decl->line);
                add_protocol_conformance(decl->name, decl->protocol_conformances, decl->line);
                break;
            }
            case StmtKind::EnumDecl: {
                auto* decl = static_cast<const EnumDeclStmt*>(stmt);
                add_known_type(decl->name, TypeKind::User, decl->line);
                break;
            }
            case StmtKind::ProtocolDecl: {
                auto* decl = static_cast<const ProtocolDeclStmt*>(stmt);
                add_known_type(decl->name, TypeKind::Protocol, decl->line);
                add_protocol_inheritance(decl->name, decl->inherited_protocols);
                break;
            }
            case StmtKind::ExtensionDecl: {
                auto* decl = static_cast<const ExtensionDeclStmt*>(stmt);
                add_protocol_conformance(decl->extended_type, decl->protocol_conformances, decl->line);
                break;
            }
            default:
                break;
        }
    }
}

void TypeChecker::finalize_protocol_maps() {
    protocol_descendants_.clear();
    for (const auto& [protocol, _] : protocol_inheritance_) {
        protocol_descendants_[protocol].insert(protocol);
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [protocol, parents] : protocol_inheritance_) {
            auto& descendants = protocol_descendants_[protocol];
            for (const auto& parent : parents) {
                descendants.insert(parent);
                if (protocol_descendants_.contains(parent)) {
                    for (const auto& ancestor : protocol_descendants_[parent]) {
                        if (descendants.insert(ancestor).second) {
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    for (const auto& [protocol, conformers] : protocol_conformers_) {
        for (const auto& conformer : conformers) {
            for (const auto& ancestor : protocol_descendants_[protocol]) {
                protocol_conformers_[ancestor].insert(conformer);
            }
        }
    }
}

void TypeChecker::enter_scope() {
    scopes_.emplace_back();
}

void TypeChecker::exit_scope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

void TypeChecker::declare_symbol(const std::string& name, const TypeInfo& type, uint32_t line) {
    if (scopes_.empty()) {
        error("Internal error: no scope available", line);
    }
    auto& scope = scopes_.back();
    if (scope.contains(name)) {
        error("Duplicate symbol '" + name + "'", line);
    }
    scope.emplace(name, type);
}

TypeChecker::TypeInfo TypeChecker::lookup_symbol(const std::string& name, uint32_t line) const {
    for (auto scope_it = scopes_.rbegin(); scope_it != scopes_.rend(); ++scope_it) {
        const auto& scope = *scope_it;
        auto it = scope.find(name);
        if (it != scope.end()) {
            return it->second;
        }
    }
    error("Undefined symbol '" + name + "'", line);
}

bool TypeChecker::has_symbol(const std::string& name) const {
    for (auto scope_it = scopes_.rbegin(); scope_it != scopes_.rend(); ++scope_it) {
        const auto& scope = *scope_it;
        if (scope.contains(name)) {
            return true;
        }
    }
    return false;
}

void TypeChecker::check_stmt(const Stmt* stmt) {
    if (!stmt) {
        return;
    }
    switch (stmt->kind) {
        case StmtKind::Expression:
            check_expr(static_cast<const ExprStmt*>(stmt)->expression.get());
            break;
        case StmtKind::Print:
            check_expr(static_cast<const PrintStmt*>(stmt)->expression.get());
            break;
        case StmtKind::Block:
            check_block(static_cast<const BlockStmt*>(stmt));
            break;
        case StmtKind::VarDecl:
            check_var_decl(static_cast<const VarDeclStmt*>(stmt));
            break;
        case StmtKind::If:
            check_if_stmt(static_cast<const IfStmt*>(stmt));
            break;
        case StmtKind::IfLet:
            check_if_let_stmt(static_cast<const IfLetStmt*>(stmt));
            break;
        case StmtKind::GuardLet:
            check_guard_let_stmt(static_cast<const GuardLetStmt*>(stmt));
            break;
        case StmtKind::While:
            check_while_stmt(static_cast<const WhileStmt*>(stmt));
            break;
        case StmtKind::RepeatWhile:
            check_repeat_while_stmt(static_cast<const RepeatWhileStmt*>(stmt));
            break;
        case StmtKind::ForIn:
            check_for_in_stmt(static_cast<const ForInStmt*>(stmt));
            break;
        case StmtKind::Switch:
            check_switch_stmt(static_cast<const SwitchStmt*>(stmt));
            break;
        case StmtKind::Return:
            check_return_stmt(static_cast<const ReturnStmt*>(stmt));
            break;
        case StmtKind::Throw:
            check_throw_stmt(static_cast<const ThrowStmt*>(stmt));
            break;
        case StmtKind::FuncDecl:
            check_func_decl(static_cast<const FuncDeclStmt*>(stmt));
            break;
        case StmtKind::ClassDecl:
            check_class_decl(static_cast<const ClassDeclStmt*>(stmt));
            break;
        case StmtKind::StructDecl:
            check_struct_decl(static_cast<const StructDeclStmt*>(stmt));
            break;
        case StmtKind::EnumDecl:
            check_enum_decl(static_cast<const EnumDeclStmt*>(stmt));
            break;
        case StmtKind::ProtocolDecl:
            check_protocol_decl(static_cast<const ProtocolDeclStmt*>(stmt));
            break;
        case StmtKind::ExtensionDecl:
            check_extension_decl(static_cast<const ExtensionDeclStmt*>(stmt));
            break;
        case StmtKind::Import:
            break;
        case StmtKind::DoCatch:
            check_do_catch_stmt(static_cast<const DoCatchStmt*>(stmt));
            break;
        case StmtKind::Break:
        case StmtKind::Continue:
            break;
    }
}

void TypeChecker::check_block(const BlockStmt* stmt) {
    enter_scope();
    for (const auto& nested : stmt->statements) {
        check_stmt(nested.get());
    }
    exit_scope();
}

void TypeChecker::check_var_decl(const VarDeclStmt* stmt) {
    TypeInfo declared = TypeInfo::unknown();
    if (stmt->type_annotation.has_value()) {
        declared = type_from_annotation(stmt->type_annotation.value(), stmt->line);
    }

    if (stmt->initializer) {
        TypeInfo init_type = check_expr(stmt->initializer.get());
        if (declared.kind != TypeKind::Unknown) {
            if (!is_assignable(declared, init_type)) {
                error("Cannot assign '" + init_type.name + "' to variable of type '" + declared.name + "'", stmt->line);
            }
        } else {
            declared = init_type;
        }
    }

    declare_symbol(stmt->name, declared, stmt->line);

    if (stmt->is_computed && stmt->getter_body) {
        TypeInfo return_type = declared;
        if (return_type.kind == TypeKind::Unknown) {
            return_type = TypeInfo::unknown();
        }
        function_stack_.push_back(FunctionContext{return_type});
        check_block(stmt->getter_body.get());
        function_stack_.pop_back();
    }

    if (stmt->is_computed && stmt->setter_body) {
        enter_scope();
        declare_symbol("newValue", declared, stmt->line);
        function_stack_.push_back(FunctionContext{TypeInfo::builtin("Void")});
        check_block(stmt->setter_body.get());
        function_stack_.pop_back();
        exit_scope();
    }
}

void TypeChecker::check_if_stmt(const IfStmt* stmt) {
    TypeInfo cond = check_expr(stmt->condition.get());
    if (!is_unknown(cond) && !is_bool(cond)) {
        error("If condition must be Bool", stmt->line);
    }
    check_stmt(stmt->then_branch.get());
    if (stmt->else_branch) {
        check_stmt(stmt->else_branch.get());
    }
}

void TypeChecker::check_if_let_stmt(const IfLetStmt* stmt) {
    TypeInfo opt_type = check_expr(stmt->optional_expr.get());
    if (!is_unknown(opt_type) && !opt_type.is_optional) {
        error("if let requires optional expression", stmt->line);
    }
    TypeInfo bound = base_type(opt_type);
    enter_scope();
    declare_symbol(stmt->binding_name, bound, stmt->line);
    check_stmt(stmt->then_branch.get());
    exit_scope();
    if (stmt->else_branch) {
        check_stmt(stmt->else_branch.get());
    }
}

void TypeChecker::check_guard_let_stmt(const GuardLetStmt* stmt) {
    TypeInfo opt_type = check_expr(stmt->optional_expr.get());
    if (!is_unknown(opt_type) && !opt_type.is_optional) {
        error("guard let requires optional expression", stmt->line);
    }
    check_stmt(stmt->else_branch.get());
    declare_symbol(stmt->binding_name, base_type(opt_type), stmt->line);
}

void TypeChecker::check_while_stmt(const WhileStmt* stmt) {
    TypeInfo cond = check_expr(stmt->condition.get());
    if (!is_unknown(cond) && !is_bool(cond)) {
        error("While condition must be Bool", stmt->line);
    }
    check_stmt(stmt->body.get());
}

void TypeChecker::check_repeat_while_stmt(const RepeatWhileStmt* stmt) {
    check_stmt(stmt->body.get());
    TypeInfo cond = check_expr(stmt->condition.get());
    if (!is_unknown(cond) && !is_bool(cond)) {
        error("repeat-while condition must be Bool", stmt->line);
    }
}

void TypeChecker::check_for_in_stmt(const ForInStmt* stmt) {
    check_expr(stmt->iterable.get());
    enter_scope();
    declare_symbol(stmt->variable, TypeInfo::unknown(), stmt->line);
    if (stmt->where_condition) {
        TypeInfo cond = check_expr(stmt->where_condition.get());
        if (!is_unknown(cond) && !is_bool(cond)) {
            error("for-in where condition must be Bool", stmt->line);
        }
    }
    check_stmt(stmt->body.get());
    exit_scope();
}

void TypeChecker::check_switch_stmt(const SwitchStmt* stmt) {
    check_expr(stmt->value.get());
    for (const auto& case_clause : stmt->cases) {
        enter_scope();
        for (const auto& pattern : case_clause.patterns) {
            if (pattern) {
                check_expr(pattern.get());
            }
        }
        for (const auto& statement : case_clause.statements) {
            check_stmt(statement.get());
        }
        exit_scope();
    }
}

void TypeChecker::check_return_stmt(const ReturnStmt* stmt) {
    TypeInfo expected = TypeInfo::builtin("Void");
    if (!function_stack_.empty()) {
        expected = function_stack_.back().return_type;
    }

    if (!stmt->value) {
        if (!is_unknown(expected) && expected.name != "Void") {
            error("Return statement missing value", stmt->line);
        }
        return;
    }

    TypeInfo actual = check_expr(stmt->value.get());
    if (!is_unknown(expected) && !is_assignable(expected, actual)) {
        error("Return type mismatch: expected '" + expected.name + "'", stmt->line);
    }
}

void TypeChecker::check_throw_stmt(const ThrowStmt* stmt) {
    if (stmt->value) {
        check_expr(stmt->value.get());
    }
}

void TypeChecker::check_func_decl(const FuncDeclStmt* stmt, const std::string& self_type) {
    std::vector<TypeInfo> params;
    params.reserve(stmt->params.size());
    for (const auto& [name, annotation] : stmt->params) {
        params.push_back(type_from_annotation(annotation, stmt->line));
    }

    TypeInfo return_type = TypeInfo::builtin("Void");
    if (stmt->return_type.has_value()) {
        return_type = type_from_annotation(stmt->return_type.value(), stmt->line);
    }

    TypeInfo function_type = TypeInfo::function(params, return_type);
    if (!has_symbol(stmt->name)) {
        declare_symbol(stmt->name, function_type, stmt->line);
    }

    enter_scope();
    if (!self_type.empty()) {
        TypeInfo self_info = TypeInfo::user(self_type);
        declare_symbol("self", self_info, stmt->line);
    }
    for (size_t i = 0; i < stmt->params.size(); ++i) {
        declare_symbol(stmt->params[i].first, params[i], stmt->line);
    }
    function_stack_.push_back(FunctionContext{return_type});
    check_block(stmt->body.get());
    function_stack_.pop_back();
    exit_scope();
}

void TypeChecker::check_class_decl(const ClassDeclStmt* stmt) {
    enter_scope();
    declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);

    for (const auto& property : stmt->properties) {
        check_var_decl(property.get());
    }
    for (const auto& method : stmt->methods) {
        check_func_decl(method.get(), stmt->name);
    }

    if (stmt->deinit_body) {
        function_stack_.push_back(FunctionContext{TypeInfo::builtin("Void")});
        check_block(stmt->deinit_body.get());
        function_stack_.pop_back();
    }

    exit_scope();
}

void TypeChecker::check_struct_decl(const StructDeclStmt* stmt) {
    enter_scope();
    declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);

    for (const auto& property : stmt->properties) {
        check_var_decl(property.get());
    }

    for (const auto& method : stmt->methods) {
        if (!method) {
            continue;
        }
        enter_scope();
        declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);
        std::vector<TypeInfo> params;
        params.reserve(method->params.size());
        for (const auto& [param_name, annotation] : method->params) {
            TypeInfo param_type = type_from_annotation(annotation, stmt->line);
            params.push_back(param_type);
            declare_symbol(param_name, param_type, stmt->line);
        }
        TypeInfo return_type = TypeInfo::builtin("Void");
        if (method->return_type.has_value()) {
            return_type = type_from_annotation(method->return_type.value(), stmt->line);
        }
        function_stack_.push_back(FunctionContext{return_type});
        check_block(method->body.get());
        function_stack_.pop_back();
        exit_scope();
    }

    for (const auto& init : stmt->initializers) {
        check_func_decl(init.get(), stmt->name);
    }

    exit_scope();
}

void TypeChecker::check_enum_decl(const EnumDeclStmt* stmt) {
    enter_scope();
    declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);

    for (const auto& method : stmt->methods) {
        if (!method) {
            continue;
        }
        enter_scope();
        declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);
        std::vector<TypeInfo> params;
        params.reserve(method->params.size());
        for (const auto& [param_name, annotation] : method->params) {
            TypeInfo param_type = type_from_annotation(annotation, stmt->line);
            params.push_back(param_type);
            declare_symbol(param_name, param_type, stmt->line);
        }
        TypeInfo return_type = TypeInfo::builtin("Void");
        if (method->return_type.has_value()) {
            return_type = type_from_annotation(method->return_type.value(), stmt->line);
        }
        function_stack_.push_back(FunctionContext{return_type});
        check_block(method->body.get());
        function_stack_.pop_back();
        exit_scope();
    }

    exit_scope();
}

void TypeChecker::check_protocol_decl(const ProtocolDeclStmt* stmt) {
    for (const auto& requirement : stmt->method_requirements) {
        for (const auto& [_, annotation] : requirement.params) {
            type_from_annotation(annotation, stmt->line);
        }
        if (requirement.return_type.has_value()) {
            type_from_annotation(requirement.return_type.value(), stmt->line);
        }
    }

    for (const auto& requirement : stmt->property_requirements) {
        type_from_annotation(requirement.type, stmt->line);
    }
}

void TypeChecker::check_extension_decl(const ExtensionDeclStmt* stmt) {
    if (!known_types_.contains(stmt->extended_type)) {
        error("Unknown extended type '" + stmt->extended_type + "'", stmt->line);
    }

    enter_scope();
    declare_symbol("self", TypeInfo::user(stmt->extended_type), stmt->line);

    for (const auto& method : stmt->methods) {
        if (!method) {
            continue;
        }
        enter_scope();
        declare_symbol("self", TypeInfo::user(stmt->extended_type), stmt->line);
        std::vector<TypeInfo> params;
        params.reserve(method->params.size());
        for (const auto& [param_name, annotation] : method->params) {
            TypeInfo param_type = type_from_annotation(annotation, stmt->line);
            params.push_back(param_type);
            declare_symbol(param_name, param_type, stmt->line);
        }
        TypeInfo return_type = TypeInfo::builtin("Void");
        if (method->return_type.has_value()) {
            return_type = type_from_annotation(method->return_type.value(), stmt->line);
        }
        function_stack_.push_back(FunctionContext{return_type});
        check_block(method->body.get());
        function_stack_.pop_back();
        exit_scope();
    }

    exit_scope();
}

void TypeChecker::check_do_catch_stmt(const DoCatchStmt* stmt) {
    if (stmt->try_block) {
        check_block(stmt->try_block.get());
    }
    for (const auto& clause : stmt->catch_clauses) {
        enter_scope();
        if (!clause.binding_name.empty()) {
            declare_symbol(clause.binding_name, TypeInfo::unknown(), stmt->line);
        }
        for (const auto& nested : clause.statements) {
            check_stmt(nested.get());
        }
        exit_scope();
    }
}

TypeChecker::TypeInfo TypeChecker::check_expr(const Expr* expr) {
    if (!expr) {
        return TypeInfo::unknown();
    }

    switch (expr->kind) {
        case ExprKind::Literal:
            return check_literal_expr(static_cast<const LiteralExpr*>(expr));
        case ExprKind::Identifier:
            return check_identifier_expr(static_cast<const IdentifierExpr*>(expr));
        case ExprKind::Unary:
            return check_unary_expr(static_cast<const UnaryExpr*>(expr));
        case ExprKind::Binary:
            return check_binary_expr(static_cast<const BinaryExpr*>(expr));
        case ExprKind::Assign:
            return check_assign_expr(static_cast<const AssignExpr*>(expr));
        case ExprKind::Call:
            return check_call_expr(static_cast<const CallExpr*>(expr));
        case ExprKind::Member:
            return check_member_expr(static_cast<const MemberExpr*>(expr));
        case ExprKind::OptionalChain:
            return check_optional_chain_expr(static_cast<const OptionalChainExpr*>(expr));
        case ExprKind::NilCoalesce:
            return check_nil_coalesce_expr(static_cast<const NilCoalesceExpr*>(expr));
        case ExprKind::ForceUnwrap:
            return check_force_unwrap_expr(static_cast<const ForceUnwrapExpr*>(expr));
        case ExprKind::Range:
            return check_range_expr(static_cast<const RangeExpr*>(expr));
        case ExprKind::ArrayLiteral:
            return check_array_literal_expr(static_cast<const ArrayLiteralExpr*>(expr));
        case ExprKind::DictLiteral:
            return check_dict_literal_expr(static_cast<const DictLiteralExpr*>(expr));
        case ExprKind::Subscript:
            return check_subscript_expr(static_cast<const SubscriptExpr*>(expr));
        case ExprKind::Ternary:
            return check_ternary_expr(static_cast<const TernaryExpr*>(expr));
        case ExprKind::Closure:
            return check_closure_expr(static_cast<const ClosureExpr*>(expr));
        case ExprKind::TypeCast:
            return check_type_cast_expr(static_cast<const TypeCastExpr*>(expr));
        case ExprKind::TypeCheck:
            return check_type_check_expr(static_cast<const TypeCheckExpr*>(expr));
        case ExprKind::Try:
            return check_try_expr(static_cast<const TryExpr*>(expr));
        case ExprKind::Super:
            return TypeInfo::unknown();
    }
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_literal_expr(const LiteralExpr* expr) {
    if (expr->string_value.has_value()) {
        return TypeInfo::builtin("String");
    }

    switch (expr->value.type()) {
        case Value::Type::Bool:
            return TypeInfo::builtin("Bool");
        case Value::Type::Int:
            return TypeInfo::builtin("Int");
        case Value::Type::Float:
            return TypeInfo::builtin("Float");
        case Value::Type::Null:
        case Value::Type::Undefined:
            return TypeInfo{"Null", false, TypeKind::Builtin, {}, nullptr};
        case Value::Type::Object:
            return TypeInfo::unknown();
    }
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_identifier_expr(const IdentifierExpr* expr) {
    return lookup_symbol(expr->name, expr->line);
}

TypeChecker::TypeInfo TypeChecker::check_unary_expr(const UnaryExpr* expr) {
    TypeInfo operand = check_expr(expr->operand.get());
    switch (expr->op) {
        case TokenType::Minus:
            if (!is_unknown(operand) && !is_numeric(operand)) {
                error("Unary '-' expects numeric operand", expr->line);
            }
            return operand;
        case TokenType::Not:
            if (!is_unknown(operand) && !is_bool(operand)) {
                error("Unary '!' expects Bool operand", expr->line);
            }
            return TypeInfo::builtin("Bool");
        case TokenType::BitwiseNot:
            if (!is_unknown(operand) && operand.name != "Int") {
                error("Unary '~' expects Int operand", expr->line);
            }
            return TypeInfo::builtin("Int");
        default:
            return operand;
    }
}

TypeChecker::TypeInfo TypeChecker::check_binary_expr(const BinaryExpr* expr) {
    TypeInfo left = check_expr(expr->left.get());
    TypeInfo right = check_expr(expr->right.get());

    switch (expr->op) {
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Percent:
            if (expr->op == TokenType::Plus && (is_string(left) || is_string(right))) {
                if (!is_unknown(left) && !is_string(left)) {
                    error("Cannot add String with non-String", expr->line);
                }
                if (!is_unknown(right) && !is_string(right)) {
                    error("Cannot add String with non-String", expr->line);
                }
                return TypeInfo::builtin("String");
            }
            if (!is_unknown(left) && !is_numeric(left)) {
                error("Binary operator expects numeric left operand", expr->line);
            }
            if (!is_unknown(right) && !is_numeric(right)) {
                error("Binary operator expects numeric right operand", expr->line);
            }
            if (left.name == "Float" || right.name == "Float") {
                return TypeInfo::builtin("Float");
            }
            return TypeInfo::builtin("Int");
        case TokenType::EqualEqual:
        case TokenType::NotEqual:
            if (!is_unknown(left) && !is_unknown(right) && !is_assignable(left, right)) {
                error("Equality comparison requires compatible types", expr->line);
            }
            return TypeInfo::builtin("Bool");
        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual:
            if (!is_unknown(left) && !is_numeric(left)) {
                error("Comparison requires numeric left operand", expr->line);
            }
            if (!is_unknown(right) && !is_numeric(right)) {
                error("Comparison requires numeric right operand", expr->line);
            }
            return TypeInfo::builtin("Bool");
        case TokenType::And:
        case TokenType::Or:
            if (!is_unknown(left) && !is_bool(left)) {
                error("Logical operator requires Bool left operand", expr->line);
            }
            if (!is_unknown(right) && !is_bool(right)) {
                error("Logical operator requires Bool right operand", expr->line);
            }
            return TypeInfo::builtin("Bool");
        default:
            return TypeInfo::unknown();
    }
}

TypeChecker::TypeInfo TypeChecker::check_assign_expr(const AssignExpr* expr) {
    if (!has_symbol(expr->name)) {
        error("Undefined symbol '" + expr->name + "'", expr->line);
    }
    TypeInfo expected = lookup_symbol(expr->name, expr->line);
    TypeInfo actual = check_expr(expr->value.get());
    if (!is_assignable(expected, actual)) {
        error("Cannot assign '" + actual.name + "' to '" + expected.name + "'", expr->line);
    }
    return expected;
}

TypeChecker::TypeInfo TypeChecker::check_call_expr(const CallExpr* expr) {
    TypeInfo callee = check_expr(expr->callee.get());
    for (const auto& arg : expr->arguments) {
        check_expr(arg.get());
    }
    if (callee.kind == TypeKind::Function) {
        if (callee.param_types.size() != expr->arguments.size()) {
            error("Function argument count mismatch", expr->line);
        }
        for (size_t i = 0; i < callee.param_types.size(); ++i) {
            TypeInfo arg_type = check_expr(expr->arguments[i].get());
            if (!is_assignable(callee.param_types[i], arg_type)) {
                error("Function argument type mismatch", expr->line);
            }
        }
        return *callee.return_type;
    }
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_member_expr(const MemberExpr* expr) {
    TypeInfo object = check_expr(expr->object.get());
    if (!is_unknown(object) && object.is_optional) {
        error("Use optional chaining to access members on optional", expr->line);
    }
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_optional_chain_expr(const OptionalChainExpr* expr) {
    TypeInfo object = check_expr(expr->object.get());
    if (!is_unknown(object) && !object.is_optional) {
        error("Optional chaining requires optional base", expr->line);
    }
    TypeInfo member = TypeInfo::unknown();
    return make_optional(member);
}

TypeChecker::TypeInfo TypeChecker::check_nil_coalesce_expr(const NilCoalesceExpr* expr) {
    TypeInfo optional = check_expr(expr->optional_expr.get());
    TypeInfo fallback = check_expr(expr->fallback.get());
    if (!is_unknown(optional) && !optional.is_optional) {
        error("Nil-coalescing requires optional left operand", expr->line);
    }
    TypeInfo base = base_type(optional);
    if (!is_unknown(base) && !is_unknown(fallback) && !is_assignable(base, fallback)) {
        error("Nil-coalescing fallback type mismatch", expr->line);
    }
    return !is_unknown(fallback) ? fallback : base;
}

TypeChecker::TypeInfo TypeChecker::check_force_unwrap_expr(const ForceUnwrapExpr* expr) {
    TypeInfo operand = check_expr(expr->operand.get());
    if (!is_unknown(operand) && !operand.is_optional) {
        error("Force unwrap requires optional operand", expr->line);
    }
    return base_type(operand);
}

TypeChecker::TypeInfo TypeChecker::check_range_expr(const RangeExpr* expr) {
    check_expr(expr->start.get());
    check_expr(expr->end.get());
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_array_literal_expr(const ArrayLiteralExpr* expr) {
    for (const auto& elem : expr->elements) {
        check_expr(elem.get());
    }
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_dict_literal_expr(const DictLiteralExpr* expr) {
    for (const auto& [key, value] : expr->entries) {
        check_expr(key.get());
        check_expr(value.get());
    }
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_subscript_expr(const SubscriptExpr* expr) {
    check_expr(expr->object.get());
    check_expr(expr->index.get());
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_ternary_expr(const TernaryExpr* expr) {
    TypeInfo cond = check_expr(expr->condition.get());
    if (!is_unknown(cond) && !is_bool(cond)) {
        error("Ternary condition must be Bool", expr->line);
    }
    TypeInfo then_type = check_expr(expr->then_expr.get());
    TypeInfo else_type = check_expr(expr->else_expr.get());
    if (!is_unknown(then_type) && !is_unknown(else_type) && !is_assignable(then_type, else_type)) {
        error("Ternary branches must have compatible types", expr->line);
    }
    return !is_unknown(then_type) ? then_type : else_type;
}

TypeChecker::TypeInfo TypeChecker::check_closure_expr(const ClosureExpr* expr) {
    enter_scope();
    std::vector<TypeInfo> params;
    params.reserve(expr->params.size());
    for (const auto& [name, annotation] : expr->params) {
        TypeInfo param_type = type_from_annotation(annotation, expr->line);
        params.push_back(param_type);
        declare_symbol(name, param_type, expr->line);
    }
    TypeInfo return_type = TypeInfo::builtin("Void");
    if (expr->return_type.has_value()) {
        return_type = type_from_annotation(expr->return_type.value(), expr->line);
    }
    function_stack_.push_back(FunctionContext{return_type});
    check_block(expr->body.get());
    function_stack_.pop_back();
    exit_scope();
    return TypeInfo::function(params, return_type);
}

TypeChecker::TypeInfo TypeChecker::check_type_cast_expr(const TypeCastExpr* expr) {
    check_expr(expr->value.get());
    TypeInfo target = type_from_annotation(expr->target_type, expr->line);
    if (expr->is_optional) {
        target.is_optional = true;
    }
    return target;
}

TypeChecker::TypeInfo TypeChecker::check_type_check_expr(const TypeCheckExpr* expr) {
    check_expr(expr->value.get());
    type_from_annotation(expr->target_type, expr->line);
    return TypeInfo::builtin("Bool");
}

TypeChecker::TypeInfo TypeChecker::check_try_expr(const TryExpr* expr) {
    TypeInfo inner = check_expr(expr->expression.get());
    if (expr->is_optional) {
        return make_optional(inner);
    }
    return inner;
}

TypeChecker::TypeInfo TypeChecker::type_from_annotation(const TypeAnnotation& annotation, uint32_t line) {
    if (annotation.is_function_type) {
        std::vector<TypeInfo> params;
        params.reserve(annotation.param_types.size());
        for (const auto& param : annotation.param_types) {
            params.push_back(type_from_annotation(param, line));
        }
        TypeInfo result = TypeInfo::builtin("Void");
        if (annotation.return_type) {
            result = type_from_annotation(*annotation.return_type, line);
        }
        TypeInfo func_type = TypeInfo::function(params, result);
        if (annotation.is_optional) {
            func_type.is_optional = true;
        }
        return func_type;
    }

    auto it = known_types_.find(annotation.name);
    if (it == known_types_.end()) {
        error("Unknown type '" + annotation.name + "'", line);
    }

    TypeInfo info;
    info.name = annotation.name;
    info.is_optional = annotation.is_optional;
    info.kind = it->second;
    return info;
}

bool TypeChecker::is_assignable(const TypeInfo& expected, const TypeInfo& actual) const {
    if (is_unknown(expected) || is_unknown(actual)) {
        return true;
    }
    if (expected.name == "Any") {
        return true;
    }
    if (expected.is_optional) {
        if (is_nil(actual)) {
            return true;
        }
        TypeInfo expected_base = base_type(expected);
        TypeInfo actual_base = actual.is_optional ? base_type(actual) : actual;
        return is_assignable(expected_base, actual_base);
    }

    if (actual.is_optional || is_nil(actual)) {
        return false;
    }

    if (expected.kind == TypeKind::Protocol) {
        if (actual.kind == TypeKind::Protocol) {
            return protocol_inherits(actual.name, expected.name);
        }
        return protocol_conforms(actual.name, expected.name);
    }

    if (expected.name == actual.name) {
        return true;
    }

    if (expected.kind == TypeKind::Function && actual.kind == TypeKind::Function) {
        if (expected.param_types.size() != actual.param_types.size()) {
            return false;
        }
        for (size_t i = 0; i < expected.param_types.size(); ++i) {
            if (!is_assignable(expected.param_types[i], actual.param_types[i])) {
                return false;
            }
        }
        if (expected.return_type && actual.return_type) {
            return is_assignable(*expected.return_type, *actual.return_type);
        }
        return true;
    }

    return false;
}

bool TypeChecker::is_numeric(const TypeInfo& type) const {
    return type.name == "Int" || type.name == "Float";
}

bool TypeChecker::is_bool(const TypeInfo& type) const {
    return type.name == "Bool";
}

bool TypeChecker::is_string(const TypeInfo& type) const {
    return type.name == "String";
}

bool TypeChecker::is_nil(const TypeInfo& type) const {
    return type.name == "Null";
}

bool TypeChecker::is_unknown(const TypeInfo& type) const {
    return type.kind == TypeKind::Unknown;
}

TypeChecker::TypeInfo TypeChecker::make_optional(const TypeInfo& type) const {
    TypeInfo result = type;
    result.is_optional = true;
    return result;
}

TypeChecker::TypeInfo TypeChecker::base_type(const TypeInfo& type) const {
    TypeInfo result = type;
    result.is_optional = false;
    return result;
}

bool TypeChecker::protocol_conforms(const std::string& type_name, const std::string& protocol_name) const {
    auto it = protocol_conformers_.find(protocol_name);
    if (it == protocol_conformers_.end()) {
        return false;
    }
    return it->second.contains(type_name);
}

bool TypeChecker::protocol_inherits(const std::string& protocol_name, const std::string& ancestor) const {
    auto it = protocol_descendants_.find(protocol_name);
    if (it == protocol_descendants_.end()) {
        return false;
    }
    return it->second.contains(ancestor);
}

[[noreturn]] void TypeChecker::error(const std::string& message, uint32_t line) const {
    throw TypeCheckError(message, line);
}

} // namespace swiftscript
