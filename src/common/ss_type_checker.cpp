#include "pch.h"
#include "ss_type_checker.hpp"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"

namespace swiftscript {

TypeChecker::TypeInfo TypeChecker::TypeInfo::unknown() {
    return TypeInfo{"Unknown", false, TypeKind::Unknown, {}, nullptr, {}};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::builtin(std::string name, bool optional) {
    return TypeInfo{std::move(name), optional, TypeKind::Builtin, {}, nullptr, {}};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::user(std::string name, bool optional) {
    return TypeInfo{std::move(name), optional, TypeKind::User, {}, nullptr, {}};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::protocol(std::string name, bool optional) {
    return TypeInfo{std::move(name), optional, TypeKind::Protocol, {}, nullptr, {}};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::function(std::vector<TypeInfo> params, TypeInfo result) {
    TypeInfo info{"Function", false, TypeKind::Function, std::move(params), nullptr, {}};
    info.return_type = std::make_shared<TypeInfo>(std::move(result));
    return info;
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::generic(std::string name, bool optional) {
    return TypeInfo{std::move(name), optional, TypeKind::GenericParameter, {}, nullptr, {}};
}

TypeChecker::TypeInfo TypeChecker::TypeInfo::tuple(std::vector<TupleElementInfo> elements) {
    TypeInfo info;
    info.name = "Tuple";
    info.is_optional = false;
    info.kind = TypeKind::Tuple;
    info.tuple_elements = std::move(elements);
    return info;
}

void TypeChecker::check(const std::vector<StmtPtr>& program) {
known_types_.clear();
type_properties_.clear();
type_methods_.clear();
member_access_levels_.clear();
mutating_methods_.clear();
enum_cases_.clear();
superclass_map_.clear();
protocol_conformers_.clear();
protocol_inheritance_.clear();
protocol_descendants_.clear();
scopes_.clear();
function_stack_.clear();
generic_param_stack_.clear();
let_constants_.clear();
current_type_context_.clear();
errors_.clear();
warnings_.clear();
module_cache_.clear();
imported_modules_.clear();
compiling_modules_.clear();
imported_module_names_.clear();
known_attributes_.clear();

    add_builtin_types();
    add_builtin_attributes();
    std::vector<const std::vector<StmtPtr>*> imported_programs;
    collect_imported_programs(program, imported_programs);
    collect_type_declarations(program);
    for (const auto* module_program : imported_programs) {
        collect_type_declarations(*module_program);
    }
    finalize_protocol_maps();

    enter_scope();
    for (const auto& [name, kind] : known_types_) {
        if (kind == TypeKind::User || kind == TypeKind::Protocol) {
            declare_symbol(name, TypeInfo{name, false, kind, {}, nullptr}, 0);
        }
    }
    for (const auto& module_name : imported_module_names_) {
        declare_symbol(module_name, TypeInfo::unknown(), 0);
    }
    declare_symbol("readLine", TypeInfo::function({}, TypeInfo::builtin("String", true)), 0);
    for (const auto* module_program : imported_programs) {
        declare_functions(*module_program);
    }
    declare_functions(program);
    for (const auto* module_program : imported_programs) {
        for (const auto& stmt : *module_program) {
            if (!stmt) {
                error("Null statement in program", 0);
                continue;
            }
            check_stmt(stmt.get());
        }
    }
    for (const auto& stmt : program) {
        if (!stmt) {
            error("Null statement in program", 0);
            continue;
        }
        check_stmt(stmt.get());
    }
    exit_scope();
    emit_warnings();
    throw_if_errors();
}

void TypeChecker::add_builtin_types() {
    // Register builtin types
    known_types_.emplace("Int", TypeKind::Builtin);
    known_types_.emplace("Float", TypeKind::Builtin);
    known_types_.emplace("Bool", TypeKind::Builtin);
    known_types_.emplace("String", TypeKind::Builtin);
    known_types_.emplace("Array", TypeKind::Builtin);
    known_types_.emplace("Dictionary", TypeKind::Builtin);
    known_types_.emplace("Void", TypeKind::Builtin);
    known_types_.emplace("Any", TypeKind::Builtin);

    type_properties_["Array"].emplace("count", TypeInfo::builtin("Int"));
    type_properties_["Array"].emplace("isEmpty", TypeInfo::builtin("Bool"));
    type_methods_["Array"].emplace(
        "append",
        TypeInfo::function({ TypeInfo::builtin("Any") }, TypeInfo::builtin("Void")));
    
    // Register standard library protocols
    add_known_type("Equatable", TypeKind::Protocol, 0);
    add_known_type("Comparable", TypeKind::Protocol, 0);
    add_known_type("Hashable", TypeKind::Protocol, 0);
    add_known_type("Numeric", TypeKind::Protocol, 0);
    add_known_type("SignedNumeric", TypeKind::Protocol, 0);
    add_known_type("CustomStringConvertible", TypeKind::Protocol, 0);
    
    // Protocol inheritance hierarchy
    // Comparable inherits from Equatable
    protocol_inheritance_["Comparable"].insert("Equatable");
    protocol_descendants_["Equatable"].insert("Comparable");
    
    // Hashable inherits from Equatable
    protocol_inheritance_["Hashable"].insert("Equatable");
    protocol_descendants_["Equatable"].insert("Hashable");
    
    // SignedNumeric inherits from Numeric
    protocol_inheritance_["SignedNumeric"].insert("Numeric");
    protocol_descendants_["Numeric"].insert("SignedNumeric");
    
    // Register protocol conformances for builtin types
    
    // Int conforms to: Comparable, Hashable, Equatable, Numeric, SignedNumeric
    protocol_conformers_["Equatable"].insert("Int");
    protocol_conformers_["Comparable"].insert("Int");
    protocol_conformers_["Hashable"].insert("Int");
    protocol_conformers_["Numeric"].insert("Int");
    protocol_conformers_["SignedNumeric"].insert("Int");
    protocol_conformers_["CustomStringConvertible"].insert("Int");
    
    // Float conforms to: Comparable, Hashable, Equatable, Numeric, SignedNumeric
    protocol_conformers_["Equatable"].insert("Float");
    protocol_conformers_["Comparable"].insert("Float");
    protocol_conformers_["Hashable"].insert("Float");
    protocol_conformers_["Numeric"].insert("Float");
    protocol_conformers_["SignedNumeric"].insert("Float");
    protocol_conformers_["CustomStringConvertible"].insert("Float");
    
    // Bool conforms to: Equatable, Hashable
    protocol_conformers_["Equatable"].insert("Bool");
    protocol_conformers_["Hashable"].insert("Bool");
    protocol_conformers_["CustomStringConvertible"].insert("Bool");
    
    // String conforms to: Comparable, Hashable, Equatable
    protocol_conformers_["Equatable"].insert("String");
    protocol_conformers_["Comparable"].insert("String");
    protocol_conformers_["Hashable"].insert("String");
    protocol_conformers_["CustomStringConvertible"].insert("String");
}

void TypeChecker::add_builtin_attributes() {
    known_attributes_.emplace("Range");
    known_attributes_.emplace("Obsolete");
    known_attributes_.emplace("Deprecated");
}

void TypeChecker::register_attribute(const std::string& name, uint32_t line) {
    if (known_attributes_.contains(name)) {
        error("Duplicate attribute '" + name + "'", line);
        return;
    }
    known_attributes_.insert(name);
}

void TypeChecker::add_known_type(const std::string& name, TypeKind kind, uint32_t line) {
    if (known_types_.contains(name)) {
        return;
    }
    known_types_.emplace(name, kind);
    type_properties_.emplace(name, std::unordered_map<std::string, TypeInfo>{});
    type_methods_.emplace(name, std::unordered_map<std::string, TypeInfo>{});
    member_access_levels_.emplace(name, std::unordered_map<std::string, AccessLevel>{});
    mutating_methods_.emplace(name, std::unordered_set<std::string>{});
    enum_cases_.emplace(name, std::unordered_set<std::string>{});
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
            continue;
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
            case StmtKind::AttributeDecl: {
                auto* decl = static_cast<const AttributeDeclStmt*>(stmt);
                register_attribute(decl->name, decl->line);
                break;
            }
            case StmtKind::ClassDecl: {
                auto* decl = static_cast<const ClassDeclStmt*>(stmt);
                add_known_type(decl->name, TypeKind::User, decl->line);
                enter_generic_params(decl->generic_params);
                if (decl->superclass_name.has_value()) {
                    superclass_map_[decl->name] = decl->superclass_name.value();
                }
                add_protocol_conformance(decl->name, decl->protocol_conformances, decl->line);
                for (const auto& property : decl->properties) {
                    if (!property || !property->type_annotation.has_value()) {
                        continue;
                    }
                    type_properties_[decl->name].emplace(
                        property->name,
                        type_from_annotation(property->type_annotation.value(), property->line));
                    // Track access level
                    member_access_levels_[decl->name][property->name] = property->access_level;
                }
                for (const auto& method : decl->methods) {
                    if (!method) {
                        continue;
                    }
                    enter_generic_params(method->generic_params);
                    std::vector<TypeInfo> params;
                    params.reserve(method->params.size());
                    for (const auto& param : method->params) {
                        params.push_back(type_from_annotation(param.type, method->line));
                    }
                    TypeInfo return_type = TypeInfo::builtin("Void");
                    if (method->return_type.has_value()) {
                        return_type = type_from_annotation(method->return_type.value(), method->line);
                    }
                    type_methods_[decl->name].emplace(
                        method->name,
                        TypeInfo::function(params, return_type));
                    // Track access level
                    member_access_levels_[decl->name][method->name] = method->access_level;
                    exit_generic_params();
                }
                exit_generic_params();
                break;
            }
            case StmtKind::StructDecl: {
                auto* decl = static_cast<const StructDeclStmt*>(stmt);
                add_known_type(decl->name, TypeKind::User, decl->line);
                
                // Store generic struct templates
                if (!decl->generic_params.empty()) {
                    generic_struct_templates_[decl->name] = decl;
                }
                
                enter_generic_params(decl->generic_params);
                
                // Validate generic constraints
                for (const auto& constraint : decl->generic_constraints) {
                    // Check if the protocol exists
                    auto it = known_types_.find(constraint.protocol_name);
                    if (it == known_types_.end() || it->second != TypeKind::Protocol) {
                        error("Unknown protocol '" + constraint.protocol_name + "' in generic constraint", decl->line);
                    }
                }
                
                add_protocol_conformance(decl->name, decl->protocol_conformances, decl->line);
                for (const auto& property : decl->properties) {
                    if (!property || !property->type_annotation.has_value()) {
                        continue;
                    }
                    type_properties_[decl->name].emplace(
                        property->name,
                        type_from_annotation(property->type_annotation.value(), property->line));
                    // Track access level
                    member_access_levels_[decl->name][property->name] = property->access_level;
                }
                for (const auto& method : decl->methods) {
                    if (!method) {
                        continue;
                    }
                    enter_generic_params(method->generic_params);
                    std::vector<TypeInfo> params;
                    params.reserve(method->params.size());
                    for (const auto& param : method->params) {
                        params.push_back(type_from_annotation(param.type, decl->line));
                    }
                    TypeInfo return_type = TypeInfo::builtin("Void");
                    if (method->return_type.has_value()) {
                        return_type = type_from_annotation(method->return_type.value(), decl->line);
                    }
                    type_methods_[decl->name].emplace(
                        method->name,
                        TypeInfo::function(params, return_type));
                    // Track access level
                    member_access_levels_[decl->name][method->name] = method->access_level;
                    // Track mutating methods
                    if (method->is_mutating) {
                        mutating_methods_[decl->name].insert(method->name);
                    }
                    exit_generic_params();
                }
                exit_generic_params();
                break;
            }
            case StmtKind::EnumDecl: {
                auto* decl = static_cast<const EnumDeclStmt*>(stmt);
                add_known_type(decl->name, TypeKind::User, decl->line);
                enter_generic_params(decl->generic_params);
                
                // Add rawValue property if enum has raw type
                if (decl->raw_type.has_value()) {
                    TypeInfo raw_value_type = type_from_annotation(decl->raw_type.value(), decl->line);
                    type_properties_[decl->name].emplace("rawValue", raw_value_type);
                }
                
                for (const auto& enum_case : decl->cases) {
                    enum_cases_[decl->name].insert(enum_case.name);
                }
                for (const auto& method : decl->methods) {
                    if (!method) {
                        continue;
                    }
                    enter_generic_params(method->generic_params);
                    std::vector<TypeInfo> params;
                    params.reserve(method->params.size());
                    for (const auto& param : method->params) {
                        params.push_back(type_from_annotation(param.type, decl->line));
                    }
                    TypeInfo return_type = TypeInfo::builtin("Void");
                    if (method->return_type.has_value()) {
                        return_type = type_from_annotation(method->return_type.value(), decl->line);
                    }
                    type_methods_[decl->name].emplace(
                        method->name,
                        TypeInfo::function(params, return_type));
                    exit_generic_params();
                }
                exit_generic_params();
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
                if (known_types_.contains(decl->extended_type)) {
                    for (const auto& method : decl->methods) {
                        if (!method) {
                            continue;
                        }
                        enter_generic_params(method->generic_params);
                        std::vector<TypeInfo> params;
                        params.reserve(method->params.size());
                        for (const auto& param : method->params) {
                            params.push_back(type_from_annotation(param.type, decl->line));
                        }
                        TypeInfo return_type = TypeInfo::builtin("Void");
                        if (method->return_type.has_value()) {
                            return_type = type_from_annotation(method->return_type.value(), decl->line);
                        }
                        type_methods_[decl->extended_type].emplace(
                            method->name,
                            TypeInfo::function(params, return_type));
                        // Track access level for extension methods
                        member_access_levels_[decl->extended_type][method->name] = method->access_level;
                        // Track mutating methods for extensions
                        if (method->is_mutating) {
                            mutating_methods_[decl->extended_type].insert(method->name);
                        }
                        exit_generic_params();
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

void TypeChecker::collect_imported_programs(const std::vector<StmtPtr>& program,
                                            std::vector<const std::vector<StmtPtr>*>& ordered_programs) {
    for (const auto& stmt_ptr : program) {
        if (!stmt_ptr || stmt_ptr->kind != StmtKind::Import) {
            continue;
        }
        const auto* stmt = static_cast<const ImportStmt*>(stmt_ptr.get());
        const std::string& module_key = stmt->module_path;
        if (imported_modules_.contains(module_key)) {
            continue;
        }
        imported_modules_.insert(module_key);
        std::string module_name = module_symbol_name(module_key);
        if (!module_name.empty()) {
            imported_module_names_.push_back(module_name);
        }
        const auto& module_program = load_module_program(module_key, stmt->line);
        collect_imported_programs(module_program, ordered_programs);
        ordered_programs.push_back(&module_program);
    }
}

const std::vector<StmtPtr>& TypeChecker::load_module_program(const std::string& module_key, uint32_t line) {
    auto cached = module_cache_.find(module_key);
    if (cached != module_cache_.end()) {
        return cached->second;
    }

    if (compiling_modules_.contains(module_key)) {
        error("Circular import detected: " + module_key, line);
        auto [it, _] = module_cache_.emplace(module_key, std::vector<StmtPtr>{});
        return it->second;
    }
    compiling_modules_.insert(module_key);

    std::string source;
    if (module_resolver_) {
        std::string full_path;
        std::string err;
        if (!module_resolver_->ResolveAndLoad(module_key, full_path, source, err)) {
            error("Cannot resolve import '" + module_key + "': " + err, line);
            compiling_modules_.erase(module_key);
            auto [it, _] = module_cache_.emplace(module_key, std::vector<StmtPtr>{});
            return it->second;
        }
    } else {
        std::filesystem::path full_path = module_key;
        if (!base_directory_.empty() && !full_path.is_absolute()) {
            full_path = std::filesystem::path(base_directory_) / full_path;
        }
        if (full_path.extension() != ".ss") {
            full_path += ".ss";
        }
        std::ifstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            error("Cannot open import file: " + full_path.string(), line);
            compiling_modules_.erase(module_key);
            auto [it, _] = module_cache_.emplace(module_key, std::vector<StmtPtr>{});
            return it->second;
        }
        source.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize_all();
        Parser parser(std::move(tokens));
        auto parsed_program = parser.parse();
        auto [it, _] = module_cache_.emplace(module_key, std::move(parsed_program));
        compiling_modules_.erase(module_key);
        return it->second;
    } catch (const std::exception& e) {
        error("Error importing module '" + module_key + "': " + e.what(), line);
        compiling_modules_.erase(module_key);
        auto [it, _] = module_cache_.emplace(module_key, std::vector<StmtPtr>{});
        return it->second;
    }
}

void TypeChecker::declare_functions(const std::vector<StmtPtr>& program) {
    for (const auto& stmt : program) {
        if (stmt && stmt->kind == StmtKind::FuncDecl) {
            const auto* func = static_cast<const FuncDeclStmt*>(stmt.get());
            enter_generic_params(func->generic_params);
            std::vector<TypeInfo> params;
            params.reserve(func->params.size());
            for (const auto& param : func->params) {
                params.push_back(type_from_annotation(param.type, func->line));
            }
            TypeInfo return_type = TypeInfo::builtin("Void");
            if (func->return_type.has_value()) {
                return_type = type_from_annotation(func->return_type.value(), func->line);
            }
            declare_symbol(func->name, TypeInfo::function(params, return_type), func->line);
            exit_generic_params();
        }
    }
}

std::string TypeChecker::module_symbol_name(const std::string& module_key) {
    std::filesystem::path module_path(module_key);
    std::string stem = module_path.stem().string();
    if (!stem.empty()) {
        return stem;
    }
    return module_key;
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

void TypeChecker::enter_generic_params(const std::vector<std::string>& params) {
    std::unordered_set<std::string> param_set;
    param_set.reserve(params.size());
    for (const auto& param : params) {
        param_set.insert(param);
    }
    generic_param_stack_.push_back(std::move(param_set));
}

void TypeChecker::exit_generic_params() {
    if (!generic_param_stack_.empty()) {
        generic_param_stack_.pop_back();
    }
}

bool TypeChecker::is_generic_param(const std::string& name) const {
    for (auto it = generic_param_stack_.rbegin(); it != generic_param_stack_.rend(); ++it) {
        if (it->contains(name)) {
            return true;
        }
    }
    return false;
}

void TypeChecker::declare_symbol(const std::string& name, const TypeInfo& type, uint32_t line, bool is_let) {
    if (scopes_.empty()) {
        error("Internal error: no scope available", line);
    }
    auto& scope = scopes_.back();
    if (scope.contains(name)) {
        error("Duplicate symbol '" + name + "'", line);
    }
    scope.emplace(name, type);
    
    // Track let constants separately
    if (is_let) {
        let_constants_.insert(name);
    }
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
    return TypeInfo::unknown();
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
        case StmtKind::TupleDestructuring:
            check_tuple_destructuring(static_cast<const TupleDestructuringStmt*>(stmt));
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
        case StmtKind::AttributeDecl:
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
    check_attributes(stmt->attributes, stmt->line);
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

    declare_symbol(stmt->name, declared, stmt->line, stmt->is_let);

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

void TypeChecker::check_tuple_destructuring(const TupleDestructuringStmt* stmt) {
    if (!stmt->initializer) {
        error("Tuple destructuring requires an initializer", stmt->line);
        return;
    }

    TypeInfo init_type = check_expr(stmt->initializer.get());

    if (init_type.kind != TypeKind::Tuple) {
        error("Cannot destructure non-tuple type '" + init_type.name + "'", stmt->line);
        return;
    }

    if (stmt->bindings.size() != init_type.tuple_elements.size()) {
        error("Tuple destructuring pattern has " + std::to_string(stmt->bindings.size()) +
              " elements but tuple has " + std::to_string(init_type.tuple_elements.size()), stmt->line);
        return;
    }

    for (size_t i = 0; i < stmt->bindings.size(); ++i) {
        const auto& binding = stmt->bindings[i];

        // Skip wildcard patterns
        if (binding.name == "_") {
            continue;
        }

        TypeInfo elem_type;
        if (binding.label.has_value()) {
            // Label-based binding: find the element by label
            bool found = false;
            for (const auto& elem : init_type.tuple_elements) {
                if (elem.label.has_value() && elem.label.value() == binding.label.value()) {
                    elem_type = *elem.type;
                    found = true;
                    break;
                }
            }
            if (!found) {
                error("Tuple has no element with label '" + binding.label.value() + "'", stmt->line);
                elem_type = TypeInfo::unknown();
            }
        } else {
            // Index-based binding
            elem_type = *init_type.tuple_elements[i].type;
        }

        declare_symbol(binding.name, elem_type, stmt->line, stmt->is_let);
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
            if (!pattern) {
                continue;
            }
            if (pattern->kind == PatternKind::Expression) {
                auto* expr_pattern = static_cast<const ExpressionPattern*>(pattern.get());
                if (expr_pattern->expression) {
                    check_expr(expr_pattern->expression.get());
                }
            } else if (pattern->kind == PatternKind::EnumCase) {
                auto* enum_pattern = static_cast<const EnumCasePattern*>(pattern.get());
                for (const auto& binding : enum_pattern->bindings) {
                    declare_symbol(binding, TypeInfo::builtin("Any"), stmt->line);
                }
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
    check_attributes(stmt->attributes, stmt->line);
    enter_generic_params(stmt->generic_params);
    
// Validate generic constraints
for (const auto& constraint : stmt->generic_constraints) {
    // Check if the protocol exists
    auto it = known_types_.find(constraint.protocol_name);
    if (it == known_types_.end() || it->second != TypeKind::Protocol) {
        error("Unknown protocol '" + constraint.protocol_name + "' in generic constraint", stmt->line);
    }
}
    
std::vector<TypeInfo> params;
params.reserve(stmt->params.size());
for (const auto& param : stmt->params) {
    params.push_back(type_from_annotation(param.type, stmt->line));
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
    declare_symbol(stmt->params[i].internal_name, params[i], stmt->line);
}
function_stack_.push_back(FunctionContext{return_type});
check_block(stmt->body.get());
    function_stack_.pop_back();
    exit_scope();
    exit_generic_params();
}

void TypeChecker::check_class_decl(const ClassDeclStmt* stmt) {
    check_attributes(stmt->attributes, stmt->line);
    if (stmt->superclass_name.has_value()) {
        const std::string& superclass = stmt->superclass_name.value();
        auto it = known_types_.find(superclass);
        if (it == known_types_.end() || it->second != TypeKind::User) {
            error("Unknown superclass '" + superclass + "'", stmt->line);
        }
    }

    std::string prev_context = current_type_context_;
    current_type_context_ = stmt->name;
    
    enter_generic_params(stmt->generic_params);
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
    exit_generic_params();
    current_type_context_ = prev_context;
}

void TypeChecker::check_struct_decl(const StructDeclStmt* stmt) {
    check_attributes(stmt->attributes, stmt->line);
    std::string prev_context = current_type_context_;
    current_type_context_ = stmt->name;
    
    enter_generic_params(stmt->generic_params);
    enter_scope();
    declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);

    for (const auto& property : stmt->properties) {
        check_var_decl(property.get());
    }

    for (const auto& method : stmt->methods) {
        if (!method) {
            continue;
        }
        check_attributes(method->attributes, method->line);
        enter_generic_params(method->generic_params);
        enter_scope();
        declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);
        std::vector<TypeInfo> params;
        params.reserve(method->params.size());
        for (const auto& param : method->params) {
            TypeInfo param_type = type_from_annotation(param.type, stmt->line);
            params.push_back(param_type);
            declare_symbol(param.internal_name, param_type, stmt->line);
        }
        TypeInfo return_type = TypeInfo::builtin("Void");
        if (method->return_type.has_value()) {
            return_type = type_from_annotation(method->return_type.value(), stmt->line);
        }
        function_stack_.push_back(FunctionContext{return_type});
        check_block(method->body.get());
        function_stack_.pop_back();
        exit_scope();
        exit_generic_params();
    }

    for (const auto& init : stmt->initializers) {
        check_func_decl(init.get(), stmt->name);
    }

    exit_scope();
    exit_generic_params();
    current_type_context_ = prev_context;
}

void TypeChecker::check_enum_decl(const EnumDeclStmt* stmt) {
    check_attributes(stmt->attributes, stmt->line);
    enter_generic_params(stmt->generic_params);
    enter_scope();
    declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);

    for (const auto& method : stmt->methods) {
        if (!method) {
            continue;
        }
        check_attributes(method->attributes, method->line);
        enter_generic_params(method->generic_params);
        enter_scope();
        declare_symbol("self", TypeInfo::user(stmt->name), stmt->line);
        std::vector<TypeInfo> params;
        params.reserve(method->params.size());
        for (const auto& param : method->params) {
            TypeInfo param_type = type_from_annotation(param.type, stmt->line);
            params.push_back(param_type);
            declare_symbol(param.internal_name, param_type, stmt->line);
        }
        TypeInfo return_type = TypeInfo::builtin("Void");
        if (method->return_type.has_value()) {
            return_type = type_from_annotation(method->return_type.value(), stmt->line);
        }
        function_stack_.push_back(FunctionContext{return_type});
        check_block(method->body.get());
        function_stack_.pop_back();
        exit_scope();
        exit_generic_params();
    }

    exit_scope();
    exit_generic_params();
}

void TypeChecker::check_protocol_decl(const ProtocolDeclStmt* stmt) {
    check_attributes(stmt->attributes, stmt->line);
    enter_generic_params(stmt->generic_params);
    for (const auto& requirement : stmt->method_requirements) {
        check_attributes(requirement.attributes, stmt->line);
        enter_generic_params(requirement.generic_params);
        for (const auto& param : requirement.params) {
            type_from_annotation(param.type, stmt->line);
        }
        if (requirement.return_type.has_value()) {
            type_from_annotation(requirement.return_type.value(), stmt->line);
        }
        exit_generic_params();
    }

    for (const auto& requirement : stmt->property_requirements) {
        check_attributes(requirement.attributes, stmt->line);
        type_from_annotation(requirement.type, stmt->line);
    }
    exit_generic_params();
}

void TypeChecker::check_extension_decl(const ExtensionDeclStmt* stmt) {
    check_attributes(stmt->attributes, stmt->line);
    if (!known_types_.contains(stmt->extended_type)) {
        error("Unknown extended type '" + stmt->extended_type + "'", stmt->line);
    }

    std::string prev_context = current_type_context_;
    current_type_context_ = stmt->extended_type;
    
    enter_scope();
    declare_symbol("self", TypeInfo::user(stmt->extended_type), stmt->line);

    for (const auto& method : stmt->methods) {
        if (!method) {
            continue;
        }
        check_attributes(method->attributes, method->line);
        enter_generic_params(method->generic_params);
        enter_scope();
        declare_symbol("self", TypeInfo::user(stmt->extended_type), stmt->line);
        std::vector<TypeInfo> params;
        params.reserve(method->params.size());
        for (const auto& param : method->params) {
            TypeInfo param_type = type_from_annotation(param.type, stmt->line);
            params.push_back(param_type);
            declare_symbol(param.internal_name, param_type, stmt->line);
        }
        TypeInfo return_type = TypeInfo::builtin("Void");
        if (method->return_type.has_value()) {
            return_type = type_from_annotation(method->return_type.value(), stmt->line);
        }
        function_stack_.push_back(FunctionContext{return_type});
        check_block(method->body.get());
        function_stack_.pop_back();
    exit_scope();
    exit_generic_params();
}

void TypeChecker::check_attributes(const std::vector<Attribute>& attributes, uint32_t line) {
    for (const auto& attribute : attributes) {
        if (!known_attributes_.contains(attribute.name)) {
            error("Unknown attribute '" + attribute.name + "'", attribute.line ? attribute.line : line);
            continue;
        }
        if (attribute.name == "Obsolete") {
            error("Obsolete attribute applied", attribute.line ? attribute.line : line);
        }
        else if (attribute.name == "Deprecated") {
            warn("Deprecated attribute applied", attribute.line ? attribute.line : line);
        }
    }
}

    exit_scope();
    current_type_context_ = prev_context;
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
        case ExprKind::TupleLiteral:
            return check_tuple_literal_expr(static_cast<const TupleLiteralExpr*>(expr));
        case ExprKind::TupleMember:
            return check_tuple_member_expr(static_cast<const TupleMemberExpr*>(expr));
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
    // Check for generic type instantiation (e.g., Box<Int>)
    if (!expr->generic_args.empty()) {
        // This is a generic type with type arguments
        specialize_generic_struct(expr->name, expr->generic_args, expr->line);
        std::string specialized_name = mangle_generic_name(expr->name, expr->generic_args);
        
        // Return the specialized type
        auto it = known_types_.find(specialized_name);
        if (it != known_types_.end()) {
            return TypeInfo{specialized_name, false, it->second, {}, nullptr};
        }
    }
    
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

    auto operator_name = [](TokenType type) -> std::string {
        switch (type) {
            case TokenType::Plus:
                return "+";
            case TokenType::Minus:
                return "-";
            case TokenType::Star:
                return "*";
            case TokenType::Slash:
                return "/";
            case TokenType::Percent:
                return "%";
            case TokenType::EqualEqual:
                return "==";
            case TokenType::NotEqual:
                return "!=";
            case TokenType::Less:
                return "<";
            case TokenType::LessEqual:
                return "<=";
            case TokenType::Greater:
                return ">";
            case TokenType::GreaterEqual:
                return ">=";
            case TokenType::BitwiseAnd:
                return "&";
            case TokenType::BitwiseOr:
                return "|";
            case TokenType::BitwiseXor:
                return "^";
            case TokenType::LeftShift:
                return "<<";
            case TokenType::RightShift:
                return ">>";
            default:
                return "";
        }
    };

    auto resolve_overload = [&](const TypeInfo& lhs, const TypeInfo& rhs) -> std::optional<TypeInfo> {
        std::string symbol = operator_name(expr->op);
        if (symbol.empty()) {
            return std::nullopt;
        }
        TypeInfo base = lhs.is_optional ? base_type(lhs) : lhs;
        if (base.kind != TypeKind::User) {
            return std::nullopt;
        }
        auto type_it = type_methods_.find(base.name);
        if (type_it == type_methods_.end()) {
            return std::nullopt;
        }
        auto method_it = type_it->second.find(symbol);
        if (method_it == type_it->second.end()) {
            return std::nullopt;
        }
        const TypeInfo& method_type = method_it->second;
        if (method_type.param_types.size() == 1) {
            if (!is_unknown(rhs) && !is_assignable(method_type.param_types[0], rhs)) {
                return std::nullopt;
            }
        }
        if (!method_type.return_type) {
            return TypeInfo::unknown();
        }
        return *method_type.return_type;
    };

    if (auto overload = resolve_overload(left, right)) {
        return *overload;
    }

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
        case TokenType::BitwiseAnd:
        case TokenType::BitwiseOr:
        case TokenType::BitwiseXor:
        case TokenType::LeftShift:
        case TokenType::RightShift:
            if (!is_unknown(left) && left.name != "Int") {
                error("Bitwise operator requires Int left operand", expr->line);
            }
            if (!is_unknown(right) && right.name != "Int") {
                error("Bitwise operator requires Int right operand", expr->line);
            }
            return TypeInfo::builtin("Int");
        default:
            return TypeInfo::unknown();
    }
}

TypeChecker::TypeInfo TypeChecker::check_assign_expr(const AssignExpr* expr) {
    if (!has_symbol(expr->name)) {
        error("Undefined symbol '" + expr->name + "'", expr->line);
    }
    
    // Check if trying to reassign a let constant
    if (let_constants_.contains(expr->name)) {
        error("Cannot assign to value: '" + expr->name + "' is a 'let' constant", expr->line);
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
    std::vector<TypeInfo> arg_types;
    arg_types.reserve(expr->arguments.size());
    for (const auto& arg : expr->arguments) {
        arg_types.push_back(check_expr(arg.get()));
    }
    
    // Check if calling a mutating method on a let constant
    if (expr->callee->kind == ExprKind::Member) {
        auto* member_expr = static_cast<const MemberExpr*>(expr->callee.get());
        if (member_expr->object->kind == ExprKind::Identifier) {
            auto* id_expr = static_cast<const IdentifierExpr*>(member_expr->object.get());
            // Check if the object is a let constant
            if (let_constants_.contains(id_expr->name)) {
                // Check if it's a struct type
                TypeInfo object_type = check_expr(member_expr->object.get());
                if (object_type.kind == TypeKind::User) {
                    // Check if the method is mutating
                    auto mutating_it = mutating_methods_.find(object_type.name);
                    if (mutating_it != mutating_methods_.end()) {
                        if (mutating_it->second.contains(member_expr->member)) {
                            error("Cannot call mutating method '" + member_expr->member + 
                                  "' on 'let' constant '" + id_expr->name + "'", expr->line);
                        }
                    }
                }
            }
        }
    }
    
    if (callee.kind == TypeKind::Function) {
        if (callee.param_types.size() != expr->arguments.size()) {
            error("Function argument count mismatch", expr->line);
        }
        size_t param_count = std::min(callee.param_types.size(), arg_types.size());
        for (size_t i = 0; i < param_count; ++i) {
            if (!is_assignable(callee.param_types[i], arg_types[i])) {
                error("Function argument type mismatch", expr->line);
            }
        }
        return *callee.return_type;
    }
    // Constructor call: Type() returns an instance of that type
    if (callee.kind == TypeKind::User || callee.kind == TypeKind::Protocol) {
        return callee;  // Return the type itself (e.g., Person() returns Person)
    }
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_member_expr(const MemberExpr* expr) {
    TypeInfo object = check_expr(expr->object.get());
    if (!is_unknown(object) && object.is_optional) {
        error("Use optional chaining to access members on optional", expr->line);
    }
    if (is_unknown(object)) {
        return TypeInfo::unknown();
    }

    auto lookup_member = [&](const std::string& type_name) -> std::optional<TypeInfo> {
        std::string current = type_name;
        while (!current.empty()) {
            auto prop_it = type_properties_.find(current);
            if (prop_it != type_properties_.end()) {
                auto member_it = prop_it->second.find(expr->member);
                if (member_it != prop_it->second.end()) {
                    // Check access level
                    auto access_it = member_access_levels_.find(current);
                    if (access_it != member_access_levels_.end()) {
                        auto member_access_it = access_it->second.find(expr->member);
                        if (member_access_it != access_it->second.end()) {
                            AccessLevel level = member_access_it->second;
                            if (level == AccessLevel::Private && current_type_context_ != current) {
                                error("'" + expr->member + "' is inaccessible due to 'private' protection level", expr->line);
                                return std::nullopt;  // Don't return the member if it's inaccessible
                            }
                        }
                    }
                    return member_it->second;
                }
            }
            auto method_it = type_methods_.find(current);
            if (method_it != type_methods_.end()) {
                auto found = method_it->second.find(expr->member);
                if (found != method_it->second.end()) {
                    // Check access level
                    auto access_it = member_access_levels_.find(current);
                    if (access_it != member_access_levels_.end()) {
                        auto member_access_it = access_it->second.find(expr->member);
                        if (member_access_it != access_it->second.end()) {
                            AccessLevel level = member_access_it->second;
                            if (level == AccessLevel::Private && current_type_context_ != current) {
                                error("'" + expr->member + "' is inaccessible due to 'private' protection level", expr->line);
                                return std::nullopt;  // Don't return the member if it's inaccessible
                            }
                        }
                    }
                    return found->second;
                }
            }
            auto parent_it = superclass_map_.find(current);
            if (parent_it == superclass_map_.end()) {
                break;
            }
            current = parent_it->second;
        }
        return std::nullopt;
    };

    if (enum_cases_.contains(object.name) && enum_cases_.at(object.name).contains(expr->member)) {
        return TypeInfo::user(object.name);
    }

    // Handle tuple label access
    if (object.kind == TypeKind::Tuple) {
        for (const auto& elem : object.tuple_elements) {
            if (elem.label.has_value() && elem.label.value() == expr->member) {
                return *elem.type;
            }
        }
        error("Tuple has no element with label '" + expr->member + "'", expr->line);
        return TypeInfo::unknown();
    }

    if (auto member = lookup_member(object.name)) {
        return *member;
    }

    error("Unknown member '" + expr->member + "' on type '" + object.name + "'", expr->line);
    return TypeInfo::unknown();
}

TypeChecker::TypeInfo TypeChecker::check_optional_chain_expr(const OptionalChainExpr* expr) {
    TypeInfo object = check_expr(expr->object.get());
    if (!is_unknown(object) && !object.is_optional) {
        error("Optional chaining requires optional base", expr->line);
    }
    TypeInfo base = base_type(object);
    TypeInfo member = TypeInfo::unknown();
    if (!is_unknown(base)) {
        auto lookup_member = [&](const std::string& type_name) -> std::optional<TypeInfo> {
            std::string current = type_name;
            while (!current.empty()) {
                auto prop_it = type_properties_.find(current);
                if (prop_it != type_properties_.end()) {
                    auto member_it = prop_it->second.find(expr->member);
                    if (member_it != prop_it->second.end()) {
                        return member_it->second;
                    }
                }
                auto method_it = type_methods_.find(current);
                if (method_it != type_methods_.end()) {
                    auto found = method_it->second.find(expr->member);
                    if (found != method_it->second.end()) {
                        return found->second;
                    }
                }
                auto parent_it = superclass_map_.find(current);
                if (parent_it == superclass_map_.end()) {
                    break;
                }
                current = parent_it->second;
            }
            return std::nullopt;
        };
        if (enum_cases_.contains(base.name) && enum_cases_.at(base.name).contains(expr->member)) {
            member = TypeInfo::user(base.name);
        } else if (auto found = lookup_member(base.name)) {
            member = *found;
        } else {
            error("Unknown member '" + expr->member + "' on type '" + base.name + "'", expr->line);
        }
    }
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
    TypeInfo element_type = TypeInfo::unknown();
    for (const auto& elem : expr->elements) {
        TypeInfo current = check_expr(elem.get());
        if (is_unknown(element_type)) {
            element_type = current;
        } else if (!is_unknown(current) && !is_assignable(element_type, current)) {
            element_type = TypeInfo::builtin("Any");
        }
    }
    return TypeInfo::builtin("Array");
}

TypeChecker::TypeInfo TypeChecker::check_dict_literal_expr(const DictLiteralExpr* expr) {
    TypeInfo key_type = TypeInfo::unknown();
    TypeInfo value_type = TypeInfo::unknown();
    for (const auto& [key, value] : expr->entries) {
        TypeInfo current_key = check_expr(key.get());
        TypeInfo current_value = check_expr(value.get());
        if (is_unknown(key_type)) {
            key_type = current_key;
        } else if (!is_unknown(current_key) && !is_assignable(key_type, current_key)) {
            error("Dictionary literal keys must have compatible types", expr->line);
        }
        if (is_unknown(value_type)) {
            value_type = current_value;
        } else if (!is_unknown(current_value) && !is_assignable(value_type, current_value)) {
            value_type = TypeInfo::builtin("Any");
        }
    }
    return TypeInfo::builtin("Dictionary");
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
    for (const auto& stmt : expr->body) {
        check_stmt(stmt.get());
    }
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

TypeChecker::TypeInfo TypeChecker::check_tuple_literal_expr(const TupleLiteralExpr* expr) {
    std::vector<TupleElementInfo> elements;
    for (const auto& elem : expr->elements) {
        TupleElementInfo info;
        info.label = elem.label;
        info.type = std::make_shared<TypeInfo>(check_expr(elem.value.get()));
        elements.push_back(std::move(info));
    }
    return TypeInfo::tuple(std::move(elements));
}

TypeChecker::TypeInfo TypeChecker::check_tuple_member_expr(const TupleMemberExpr* expr) {
    TypeInfo tuple_type = check_expr(expr->tuple.get());

    if (tuple_type.kind != TypeKind::Tuple) {
        error("Cannot access tuple member on non-tuple type '" + tuple_type.name + "'", expr->line);
        return TypeInfo::unknown();
    }

    if (std::holds_alternative<size_t>(expr->member)) {
        // Index access: tuple.0, tuple.1
        size_t index = std::get<size_t>(expr->member);
        if (index >= tuple_type.tuple_elements.size()) {
            error("Tuple index " + std::to_string(index) + " out of bounds for tuple with " +
                  std::to_string(tuple_type.tuple_elements.size()) + " elements", expr->line);
            return TypeInfo::unknown();
        }
        return *tuple_type.tuple_elements[index].type;
    } else {
        // Label access: tuple.x, tuple.y
        const std::string& label = std::get<std::string>(expr->member);
        for (const auto& elem : tuple_type.tuple_elements) {
            if (elem.label.has_value() && elem.label.value() == label) {
                return *elem.type;
            }
        }
        error("Tuple has no element with label '" + label + "'", expr->line);
        return TypeInfo::unknown();
    }
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

    if (annotation.is_tuple_type) {
        std::vector<TupleElementInfo> elements;
        for (const auto& elem : annotation.tuple_elements) {
            TupleElementInfo info;
            info.label = elem.label;
            if (elem.type) {
                info.type = std::make_shared<TypeInfo>(type_from_annotation(*elem.type, line));
            } else {
                info.type = std::make_shared<TypeInfo>(TypeInfo::unknown());
            }
            elements.push_back(std::move(info));
        }
        TypeInfo tuple_type = TypeInfo::tuple(std::move(elements));
        if (annotation.is_optional) {
            tuple_type.is_optional = true;
        }
        return tuple_type;
    }

    for (const auto& generic_arg : annotation.generic_args) {
        type_from_annotation(generic_arg, line);
    }

    if (is_generic_param(annotation.name)) {
        return TypeInfo::generic(annotation.name, annotation.is_optional);
    }

    auto it = known_types_.find(annotation.name);
    if (it == known_types_.end()) {
        error("Unknown type '" + annotation.name + "'", line);
        return TypeInfo::unknown();
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

    if (expected.kind == TypeKind::User && actual.kind == TypeKind::User) {
        return is_subclass_of(actual.name, expected.name);
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
    return type.kind == TypeKind::Unknown || type.kind == TypeKind::GenericParameter;
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

bool TypeChecker::is_subclass_of(const std::string& subclass, const std::string& superclass) const {
    if (subclass == superclass) {
        return true;
    }
    auto current = superclass_map_.find(subclass);
    while (current != superclass_map_.end()) {
        if (current->second == superclass) {
            return true;
        }
        current = superclass_map_.find(current->second);
    }
    return false;
}

std::string TypeChecker::mangle_generic_name(const std::string& base_name, const std::vector<TypeAnnotation>& type_args) const {
    std::string mangled = base_name;
    for (const auto& arg : type_args) {
        mangled += "_" + arg.name;
    }
    return mangled;
}

void TypeChecker::specialize_generic_struct(const std::string& base_name, const std::vector<TypeAnnotation>& type_args, uint32_t line) {
// Check if template exists
auto template_it = generic_struct_templates_.find(base_name);
if (template_it == generic_struct_templates_.end()) {
    return; // Not a generic struct
}
    
const StructDeclStmt* template_decl = template_it->second;
    
// Check parameter count
if (template_decl->generic_params.size() != type_args.size()) {
    error("Generic parameter count mismatch for " + base_name, line);
    return;
}
    
// Validate generic constraints
for (size_t i = 0; i < template_decl->generic_params.size(); ++i) {
    const std::string& param_name = template_decl->generic_params[i];
    const std::string& actual_type = type_args[i].name;
        
    // Find constraints for this parameter
    for (const auto& constraint : template_decl->generic_constraints) {
        if (constraint.param_name == param_name) {
            // Check if actual_type conforms to the required protocol
            if (!protocol_conforms(actual_type, constraint.protocol_name)) {
                error("Type '" + actual_type + "' does not conform to protocol '" + 
                      constraint.protocol_name + "' (required by generic constraint on '" + 
                      param_name + "')", line);
            }
        }
    }
}
    
// Generate specialized name
std::string specialized_name = mangle_generic_name(base_name, type_args);
    
// Check if already specialized
if (known_types_.contains(specialized_name)) {
    return; // Already specialized
}
    
// Create type substitution map
std::unordered_map<std::string, std::string> type_substitution;
for (size_t i = 0; i < template_decl->generic_params.size(); ++i) {
    type_substitution[template_decl->generic_params[i]] = type_args[i].name;
    }
    
    // Register specialized type
    add_known_type(specialized_name, TypeKind::User, line);
    
    // Specialize properties
    for (const auto& property : template_decl->properties) {
        if (!property || !property->type_annotation.has_value()) {
            continue;
        }
        
        TypeAnnotation specialized_type = property->type_annotation.value();
        // Substitute generic parameters
        auto sub_it = type_substitution.find(specialized_type.name);
        if (sub_it != type_substitution.end()) {
            specialized_type.name = sub_it->second;
        }
        
        type_properties_[specialized_name].emplace(
            property->name,
            type_from_annotation(specialized_type, property->line));
        member_access_levels_[specialized_name][property->name] = property->access_level;
    }
    
    // Specialize methods
    for (const auto& method : template_decl->methods) {
        if (!method) {
            continue;
        }
        
        std::vector<TypeInfo> params;
        params.reserve(method->params.size());
        for (const auto& param : method->params) {
            TypeAnnotation specialized_type = param.type;
            auto sub_it = type_substitution.find(specialized_type.name);
            if (sub_it != type_substitution.end()) {
                specialized_type.name = sub_it->second;
            }
            params.push_back(type_from_annotation(specialized_type, line));
        }
        
        TypeInfo return_type = TypeInfo::builtin("Void");
        if (method->return_type.has_value()) {
            TypeAnnotation specialized_return = method->return_type.value();
            auto sub_it = type_substitution.find(specialized_return.name);
            if (sub_it != type_substitution.end()) {
                specialized_return.name = sub_it->second;
            }
            return_type = type_from_annotation(specialized_return, line);
        }
        
        type_methods_[specialized_name].emplace(
            method->name,
            TypeInfo::function(params, return_type));
        member_access_levels_[specialized_name][method->name] = method->access_level;
        
        if (method->is_mutating) {
            mutating_methods_[specialized_name].insert(method->name);
        }
    }
}

void TypeChecker::error(const std::string& message, uint32_t line) const {
    errors_.emplace_back(message, line);
}

void TypeChecker::warn(const std::string& message, uint32_t line) const {
    std::ostringstream oss;
    if (line > 0) {
        oss << "Warning (line " << line << "): " << message;
    } else {
        oss << "Warning: " << message;
    }
    warnings_.push_back(oss.str());
}

void TypeChecker::emit_warnings() const {
    for (const auto& warning : warnings_) {
        std::cerr << warning << '\n';
    }
    warnings_.clear();
}

void TypeChecker::throw_if_errors() {
    if (errors_.empty()) {
        return;
    }
    std::ostringstream oss;
    for (size_t i = 0; i < errors_.size(); ++i) {
        if (i > 0) {
            oss << '\n';
        }
        oss << errors_[i].what();
    }
    uint32_t line = errors_.front().line();
    errors_.clear();
    throw TypeCheckError(oss.str(), line);
}

} // namespace swiftscript
