#include "pch.h"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_type_checker.hpp"

// Forward declarations for AST cloning
namespace swiftscript {
    StmtPtr clone_stmt(const Stmt* stmt);
    ExprPtr clone_expr(const Expr* expr);
}

namespace swiftscript {





Assembly Compiler::compile(const std::vector<StmtPtr>& program) {
// Step 1: Specialize generics (currently just passes through)
auto specialized_program = specialize_generics(program);
    
// Step 2: Type check - this handles generic specialization
TypeChecker checker;
checker.set_base_directory(base_directory_);
checker.set_module_resolver(module_resolver_);
checker.check(specialized_program);

chunk_ = Assembly{};
locals_.clear();
scope_depth_ = 0;
recursion_depth_ = 0;
method_body_lookup_.clear();

// Step 3: Compile statements (skip generic templates, they're handled by type checker)
for (const auto& stmt : specialized_program) {
    if (!stmt) {
        throw CompilerError("Null statement in program");
    }
        
    // Skip generic struct templates - they are abstract and not compiled
    if (stmt->kind == StmtKind::StructDecl) {
        auto* struct_decl = static_cast<StructDeclStmt*>(stmt.get());
        if (!struct_decl->generic_params.empty()) {
            // Generic template - skip compilation
            continue;
        }
    }
        
    compile_stmt(stmt.get());
}

    emit_auto_entry_main_call();

    emit_op(OpCode::OP_NIL, 0);
    emit_op(OpCode::OP_HALT, 0);
    chunk_.expand_to_assembly();
    populate_metadata_tables(specialized_program);
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
        case StmtKind::TupleDestructuring:
            visit(static_cast<TupleDestructuringStmt*>(stmt));
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
        case StmtKind::RepeatWhile:
            visit(static_cast<RepeatWhileStmt*>(stmt));
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
        case StmtKind::Import:
            visit(static_cast<ImportStmt*>(stmt));
            break;
        case StmtKind::Return:
            visit(static_cast<ReturnStmt*>(stmt));
            break;
        case StmtKind::Throw:
            visit(static_cast<ThrowStmt*>(stmt));
            break;
        case StmtKind::FuncDecl:
            visit(static_cast<FuncDeclStmt*>(stmt));
            break;
        case StmtKind::AttributeDecl:
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
        case StmtKind::StructDecl:
            visit(static_cast<StructDeclStmt*>(stmt));
            break;
        case StmtKind::EnumDecl:
            visit(static_cast<EnumDeclStmt*>(stmt));
            break;
        case StmtKind::ProtocolDecl:
            visit(static_cast<ProtocolDeclStmt*>(stmt));
            break;
        case StmtKind::ExtensionDecl:
            visit(static_cast<ExtensionDeclStmt*>(stmt));
            break;
        default:
            throw CompilerError("Unknown statement kind", stmt->line);
    }
}

void Compiler::visit(ClassDeclStmt* stmt) {
    size_t name_idx = identifier_constant(stmt->name);

    bool has_superclass = stmt->superclass_name.has_value();
    if (has_superclass && stmt->superclass_name.value() == stmt->name) {
        throw CompilerError("Class cannot inherit from itself", stmt->line);
    }

    if (has_superclass) {
        emit_variable_get(stmt->superclass_name.value(), stmt->line);
    }

    emit_op(OpCode::OP_CLASS, stmt->line);
    emit_short(static_cast<uint16_t>(name_idx), stmt->line);
    if (scope_depth_ > 0) {
        declare_local(stmt->name, false);
        mark_local_initialized();
    }

    if (has_superclass) {
        emit_op(OpCode::OP_INHERIT, stmt->line);
    }

    std::vector<std::string> property_names;
    std::unordered_set<std::string> property_lookup;
    property_names.reserve(stmt->properties.size());
    for (const auto& property : stmt->properties) {
        property_names.push_back(property->name);
        property_lookup.insert(property->name);
    }

    // Stored and computed properties
    for (const auto& property : stmt->properties) {
        if (property->is_computed) {
            // Computed property: compile getter and setter as functions
            size_t property_name_idx = identifier_constant(property->name);
            if (property_name_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many property identifiers", property->line);
            }
            
            // Compile getter
            FunctionPrototype getter_proto;
            getter_proto.name = "get:" + property->name;
            getter_proto.params.push_back("self");
            getter_proto.param_labels.push_back("");
            getter_proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
            getter_proto.chunk = std::make_shared<Assembly>();
            
            Compiler getter_compiler;
            getter_compiler.chunk_ = Assembly{};
            getter_compiler.locals_.clear();
            getter_compiler.scope_depth_ = 1;
            getter_compiler.recursion_depth_ = 0;
            getter_compiler.current_class_properties_ = &property_lookup;
            getter_compiler.allow_implicit_self_property_ = true;
            
            // Add 'self' as local
            getter_compiler.declare_local("self", false);
            getter_compiler.mark_local_initialized();
            
            // Compile getter body
            for (const auto& stmt_in_getter : property->getter_body->statements) {
                getter_compiler.compile_stmt(stmt_in_getter.get());
            }
            
            // Implicit return nil if no explicit return
            getter_compiler.emit_op(OpCode::OP_NIL, property->line);
            getter_compiler.emit_op(OpCode::OP_RETURN, property->line);
            
            getter_proto.chunk = finalize_function_chunk(std::move(getter_compiler.chunk_));
            record_method_body(stmt->name, getter_proto.name, property->is_static, {}, *getter_proto.chunk);
            size_t getter_idx = chunk_.add_function(std::move(getter_proto));
            
            // Compile setter (if present)
            size_t setter_idx = 0xFFFF;
            if (property->setter_body) {
                FunctionPrototype setter_proto;
                setter_proto.name = "set:" + property->name;
                setter_proto.params.push_back("self");
                setter_proto.params.push_back("newValue");
                setter_proto.param_labels.push_back("");
                setter_proto.param_labels.push_back("");
                setter_proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
                setter_proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
                setter_proto.chunk = std::make_shared<Assembly>();
                
                Compiler setter_compiler;
                setter_compiler.chunk_ = Assembly{};
                setter_compiler.locals_.clear();
                setter_compiler.scope_depth_ = 1;
                setter_compiler.recursion_depth_ = 0;
                setter_compiler.current_class_properties_ = &property_lookup;
                setter_compiler.allow_implicit_self_property_ = true;
                
                // Add 'self' and 'newValue' as locals
                setter_compiler.declare_local("self", false);
                setter_compiler.mark_local_initialized();
                setter_compiler.declare_local("newValue", false);
                setter_compiler.mark_local_initialized();
                
                // Compile setter body
                for (const auto& stmt_in_setter : property->setter_body->statements) {
                    setter_compiler.compile_stmt(stmt_in_setter.get());
                }
                
                // Return newValue (parameter 1, after self which is parameter 0)
                setter_compiler.emit_op(OpCode::OP_GET_LOCAL, property->line);
                setter_compiler.emit_short(1, property->line);  // newValue is at local index 1
                setter_compiler.emit_op(OpCode::OP_RETURN, property->line);
                
                setter_proto.chunk = finalize_function_chunk(std::move(setter_compiler.chunk_));
                record_method_body(stmt->name,
                                   setter_proto.name,
                                   property->is_static,
                                   build_accessor_param_types(property->type_annotation),
                                   *setter_proto.chunk);
                setter_idx = chunk_.add_function(std::move(setter_proto));
            }
            
            // Emit computed property definition
            emit_op(OpCode::OP_DEFINE_COMPUTED_PROPERTY, property->line);
            emit_short(static_cast<uint16_t>(property_name_idx), property->line);
            emit_short(static_cast<uint16_t>(getter_idx), property->line);
            emit_short(static_cast<uint16_t>(setter_idx), property->line);
        } else if (property->will_set_body || property->did_set_body) {
            // Stored property with observers
            if (property->initializer) {
                compile_expr(property->initializer.get());
            } else {
                emit_op(OpCode::OP_NIL, property->line);
            }

            size_t property_name_idx = identifier_constant(property->name);
            if (property_name_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many property identifiers", property->line);
            }

            // Compile willSet observer if present
            uint16_t will_set_idx = 0xFFFF;
            if (property->will_set_body) {
                FunctionPrototype will_set_proto;
                will_set_proto.name = property->name + "_willSet";
                will_set_proto.params = {"self", "newValue"};
                will_set_proto.param_labels = {"", ""};
                will_set_proto.param_defaults = {FunctionPrototype::ParamDefaultValue{}, FunctionPrototype::ParamDefaultValue{}};
                will_set_proto.is_initializer = false;
                will_set_proto.is_override = false;

                Compiler will_set_compiler;
                will_set_compiler.enclosing_ = this;
                will_set_compiler.chunk_ = Assembly{};
                will_set_compiler.locals_.clear();
                will_set_compiler.scope_depth_ = 1;
                will_set_compiler.recursion_depth_ = 0;
                will_set_compiler.current_class_properties_ = &property_lookup;
                will_set_compiler.allow_implicit_self_property_ = true;

                // Add 'self' and 'newValue' as locals
                will_set_compiler.declare_local("self", false);
                will_set_compiler.mark_local_initialized();
                will_set_compiler.declare_local("newValue", false);
                will_set_compiler.mark_local_initialized();

                // Compile willSet body
                for (const auto& stmt_in_will_set : property->will_set_body->statements) {
                    will_set_compiler.compile_stmt(stmt_in_will_set.get());
                }

                will_set_compiler.emit_op(OpCode::OP_NIL, property->line);
                will_set_compiler.emit_op(OpCode::OP_RETURN, property->line);

                will_set_proto.chunk = finalize_function_chunk(std::move(will_set_compiler.chunk_));
                will_set_idx = chunk_.add_function(std::move(will_set_proto));
            }

            // Compile didSet observer if present
            uint16_t did_set_idx = 0xFFFF;
            if (property->did_set_body) {
                FunctionPrototype did_set_proto;
                did_set_proto.name = property->name + "_didSet";
                did_set_proto.params = {"self", "oldValue"};
                did_set_proto.param_labels = {"", ""};
                did_set_proto.param_defaults = {FunctionPrototype::ParamDefaultValue{}, FunctionPrototype::ParamDefaultValue{}};
                did_set_proto.is_initializer = false;
                did_set_proto.is_override = false;

                Compiler did_set_compiler;
                did_set_compiler.enclosing_ = this;
                did_set_compiler.chunk_ = Assembly{};
                did_set_compiler.locals_.clear();
                did_set_compiler.scope_depth_ = 1;
                did_set_compiler.recursion_depth_ = 0;
                did_set_compiler.current_class_properties_ = &property_lookup;
                did_set_compiler.allow_implicit_self_property_ = true;

                // Add 'self' and 'oldValue' as locals
                did_set_compiler.declare_local("self", false);
                did_set_compiler.mark_local_initialized();
                did_set_compiler.declare_local("oldValue", false);
                did_set_compiler.mark_local_initialized();

                // Compile didSet body
                for (const auto& stmt_in_did_set : property->did_set_body->statements) {
                    did_set_compiler.compile_stmt(stmt_in_did_set.get());
                }

                did_set_compiler.emit_op(OpCode::OP_NIL, property->line);
                did_set_compiler.emit_op(OpCode::OP_RETURN, property->line);

                did_set_proto.chunk = finalize_function_chunk(std::move(did_set_compiler.chunk_));
                did_set_idx = chunk_.add_function(std::move(did_set_proto));
            }

            // Emit property definition with observers
            emit_op(OpCode::OP_DEFINE_PROPERTY_WITH_OBSERVERS, property->line);
            emit_short(static_cast<uint16_t>(property_name_idx), property->line);
            uint8_t flags = (property->is_let ? 0x1 : 0x0) | (property->is_static ? 0x2 : 0x0) | (property->is_lazy ? 0x4 : 0x0);
            emit_byte(flags, property->line);
            emit_short(static_cast<uint16_t>(will_set_idx), property->line);
            emit_short(static_cast<uint16_t>(did_set_idx), property->line);
        } else {
            // Stored property
            if (property->initializer) {
                compile_expr(property->initializer.get());
            } else {
                emit_op(OpCode::OP_NIL, property->line);
            }

            size_t property_name_idx = identifier_constant(property->name);
            if (property_name_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many property identifiers", property->line);
            }

            emit_op(OpCode::OP_DEFINE_PROPERTY, property->line);
            emit_short(static_cast<uint16_t>(property_name_idx), property->line);
            // flags: bit 0 = is_let, bit 1 = is_static, bit 2 = is_lazy
            uint8_t flags = (property->is_let ? 0x1 : 0x0) | (property->is_static ? 0x2 : 0x0) | (property->is_lazy ? 0x4 : 0x0);
            emit_byte(flags, property->line);
        }
    }

    // Methods: class object remains on stack top
    for (const auto& method : stmt->methods) {
        if (method->is_static && method->name == "main" && method->params.empty()) {
            record_entry_main_static(stmt->name, method->line);
        }

        if (method->is_override && !has_superclass) {
            throw CompilerError("'override' used in class without superclass", method->line);
        }
        if (method->is_static && method->name == "init") {
            throw CompilerError("'init' cannot be static", method->line);
        }

        FunctionPrototype proto;
        proto.name = method->name;

        // Static methods don't have implicit 'self' parameter
        if (!method->is_static) {
            proto.params.reserve(method->params.size() + 1);
            proto.param_labels.reserve(method->params.size() + 1);
            proto.param_defaults.reserve(method->params.size() + 1);
            proto.params.push_back("self");
            proto.param_labels.push_back("");
            proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
        } else {
            proto.params.reserve(method->params.size());
            proto.param_labels.reserve(method->params.size());
            proto.param_defaults.reserve(method->params.size());
        }
        for (const auto& param : method->params) {
            proto.params.push_back(param.internal_name);
            proto.param_labels.push_back(param.external_name);
            proto.param_defaults.push_back(build_param_default(param));
        }
        proto.is_initializer = (method->name == "init");
        proto.is_override = method->is_override;

        Compiler method_compiler;
        method_compiler.enclosing_ = this;
        method_compiler.chunk_ = Assembly{};
        method_compiler.locals_.clear();
        method_compiler.scope_depth_ = 1;
        method_compiler.recursion_depth_ = 0;
        method_compiler.current_class_properties_ = &property_lookup;
        method_compiler.current_class_has_super_ = has_superclass;

        // Static methods don't have 'self' access
        if (!method->is_static) {
            method_compiler.allow_implicit_self_property_ = true;
            method_compiler.declare_local("self", false);
            method_compiler.mark_local_initialized();
        } else {
            method_compiler.allow_implicit_self_property_ = false;
        }

        for (const auto& param : method->params) {
            method_compiler.declare_local(param.internal_name, param.type.is_optional);
            method_compiler.mark_local_initialized();
        }

        if (method->body) {
            for (const auto& statement : method->body->statements) {
                method_compiler.compile_stmt(statement.get());
            }
        }

        method_compiler.emit_op(OpCode::OP_NIL, method->line);
        method_compiler.emit_op(OpCode::OP_RETURN, method->line);

        proto.chunk = finalize_function_chunk(std::move(method_compiler.chunk_));
        record_method_body(stmt->name, proto.name, method->is_static, extract_param_types(method->params), *proto.chunk);
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

    // Deinit: compile as special method
    if (stmt->deinit_body) {
        FunctionPrototype proto;
        proto.name = "deinit";
        proto.params.push_back("self");
        proto.is_initializer = false;
        proto.is_override = false;

        Compiler deinit_compiler;
        deinit_compiler.enclosing_ = this;
        deinit_compiler.chunk_ = Assembly{};
        deinit_compiler.locals_.clear();
        deinit_compiler.scope_depth_ = 1;
        deinit_compiler.recursion_depth_ = 0;
        deinit_compiler.current_class_properties_ = &property_lookup;
        deinit_compiler.allow_implicit_self_property_ = true;
        deinit_compiler.current_class_has_super_ = has_superclass;

        // Implicit self
        deinit_compiler.declare_local("self", false);
        deinit_compiler.mark_local_initialized();

        for (const auto& statement : stmt->deinit_body->statements) {
            deinit_compiler.compile_stmt(statement.get());
        }

        deinit_compiler.emit_op(OpCode::OP_NIL, stmt->line);
        deinit_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

        proto.chunk = finalize_function_chunk(std::move(deinit_compiler.chunk_));
        record_method_body(stmt->name, proto.name, false, {}, *proto.chunk);
        proto.upvalues.reserve(deinit_compiler.upvalues_.size());
        for (const auto& uv : deinit_compiler.upvalues_) {
            proto.upvalues.push_back({uv.index, uv.is_local});
        }

        size_t function_index = chunk_.add_function(std::move(proto));
        if (function_index > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many functions in chunk", stmt->line);
        }

        bool has_captures = !deinit_compiler.upvalues_.empty();
        emit_op(has_captures ? OpCode::OP_CLOSURE : OpCode::OP_FUNCTION, stmt->line);
        emit_short(static_cast<uint16_t>(function_index), stmt->line);

        size_t deinit_name_idx = identifier_constant("deinit");
        if (deinit_name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many method identifiers", stmt->line);
        }
        emit_op(OpCode::OP_METHOD, stmt->line);
        emit_short(static_cast<uint16_t>(deinit_name_idx), stmt->line);
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

void Compiler::visit(StructDeclStmt* stmt) {
size_t name_idx = identifier_constant(stmt->name);

// Emit OP_STRUCT to create struct type object
emit_op(OpCode::OP_STRUCT, stmt->line);
emit_short(static_cast<uint16_t>(name_idx), stmt->line);

if (scope_depth_ > 0) {
    declare_local(stmt->name, false);
    mark_local_initialized();
}

    // Collect property names for implicit self access
    std::vector<std::string> property_names;
    std::unordered_set<std::string> property_lookup;
    property_names.reserve(stmt->properties.size());
    for (const auto& property : stmt->properties) {
        property_names.push_back(property->name);
        property_lookup.insert(property->name);
    }

    // Define stored properties
    for (const auto& property : stmt->properties) {
        if (property->is_static) {
            // Static properties are handled differently - skip for now in OP_DEFINE_PROPERTY
            // They will be initialized and stored in static_properties map
            continue;
        }
        
        // Check if property has observers or is computed
        if (property->is_computed) {
            // Computed properties handled separately
            continue;
        }
        
        if (property->will_set_body || property->did_set_body) {
            // Stored property with observers
            if (property->initializer) {
                compile_expr(property->initializer.get());
            } else {
                emit_op(OpCode::OP_NIL, property->line);
            }

            size_t property_name_idx = identifier_constant(property->name);
            if (property_name_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many property identifiers", property->line);
            }

            // Compile willSet observer if present
            uint16_t will_set_idx = 0xFFFF;
            if (property->will_set_body) {
                FunctionPrototype will_set_proto;
                will_set_proto.name = property->name + "_willSet";
                will_set_proto.params = {"self", "newValue"};
                will_set_proto.param_labels = {"", ""};
                will_set_proto.param_defaults = {FunctionPrototype::ParamDefaultValue{}, FunctionPrototype::ParamDefaultValue{}};
                will_set_proto.is_initializer = false;
                will_set_proto.is_override = false;

                Compiler will_set_compiler;
                will_set_compiler.enclosing_ = this;
                will_set_compiler.chunk_ = Assembly{};
                will_set_compiler.locals_.clear();
                will_set_compiler.scope_depth_ = 1;
                will_set_compiler.recursion_depth_ = 0;
                will_set_compiler.current_class_properties_ = &property_lookup;
                will_set_compiler.allow_implicit_self_property_ = true;

                // Add 'self' and 'newValue' as locals
                will_set_compiler.declare_local("self", false);
                will_set_compiler.mark_local_initialized();
                will_set_compiler.declare_local("newValue", false);
                will_set_compiler.mark_local_initialized();

                // Compile willSet body
                for (const auto& stmt_in_will_set : property->will_set_body->statements) {
                    will_set_compiler.compile_stmt(stmt_in_will_set.get());
                }

                will_set_compiler.emit_op(OpCode::OP_NIL, property->line);
                will_set_compiler.emit_op(OpCode::OP_RETURN, property->line);

                will_set_proto.chunk = finalize_function_chunk(std::move(will_set_compiler.chunk_));
                will_set_idx = chunk_.add_function(std::move(will_set_proto));
            }

            // Compile didSet observer if present
            uint16_t did_set_idx = 0xFFFF;
            if (property->did_set_body) {
                FunctionPrototype did_set_proto;
                did_set_proto.name = property->name + "_didSet";
                did_set_proto.params = {"self", "oldValue"};
                did_set_proto.param_labels = {"", ""};
                did_set_proto.param_defaults = {FunctionPrototype::ParamDefaultValue{}, FunctionPrototype::ParamDefaultValue{}};
                did_set_proto.is_initializer = false;
                did_set_proto.is_override = false;

                Compiler did_set_compiler;
                did_set_compiler.enclosing_ = this;
                did_set_compiler.chunk_ = Assembly{};
                did_set_compiler.locals_.clear();
                did_set_compiler.scope_depth_ = 1;
                did_set_compiler.recursion_depth_ = 0;
                did_set_compiler.current_class_properties_ = &property_lookup;
                did_set_compiler.allow_implicit_self_property_ = true;

                // Add 'self' and 'oldValue' as locals
                did_set_compiler.declare_local("self", false);
                did_set_compiler.mark_local_initialized();
                did_set_compiler.declare_local("oldValue", false);
                did_set_compiler.mark_local_initialized();

                // Compile didSet body
                for (const auto& stmt_in_did_set : property->did_set_body->statements) {
                    did_set_compiler.compile_stmt(stmt_in_did_set.get());
                }

                did_set_compiler.emit_op(OpCode::OP_NIL, property->line);
                did_set_compiler.emit_op(OpCode::OP_RETURN, property->line);

                did_set_proto.chunk = finalize_function_chunk(std::move(did_set_compiler.chunk_));
                did_set_idx = chunk_.add_function(std::move(did_set_proto));
            }

            // Emit property definition with observers
            emit_op(OpCode::OP_DEFINE_PROPERTY_WITH_OBSERVERS, property->line);
            emit_short(static_cast<uint16_t>(property_name_idx), property->line);
            uint8_t flags = (property->is_let ? 0x1 : 0x0) | (property->is_static ? 0x2 : 0x0) | (property->is_lazy ? 0x4 : 0x0);
            emit_byte(flags, property->line);
            emit_short(static_cast<uint16_t>(will_set_idx), property->line);
            emit_short(static_cast<uint16_t>(did_set_idx), property->line);
        } else {
            // Regular stored property without observers
            if (property->initializer) {
                compile_expr(property->initializer.get());
            } else {
                emit_op(OpCode::OP_NIL, property->line);
            }

            size_t property_name_idx = identifier_constant(property->name);
            if (property_name_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many property identifiers", property->line);
            }

            emit_op(OpCode::OP_DEFINE_PROPERTY, property->line);
            emit_short(static_cast<uint16_t>(property_name_idx), property->line);
            uint8_t flags = property->is_let ? 1 : 0;
            emit_byte(flags, property->line);
        }
    }
    
    // Define static properties
    for (const auto& property : stmt->properties) {
        if (!property->is_static) continue;
        
        // Compile initializer
        if (property->initializer) {
            compile_expr(property->initializer.get());
        } else {
            emit_op(OpCode::OP_NIL, property->line);
        }
        
        size_t property_name_idx = identifier_constant(property->name);
        if (property_name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many property identifiers", property->line);
        }
        
        // Store in static_properties using OP_SET_PROPERTY-like approach
        // For now, emit OP_DEFINE_PROPERTY with static flag
        emit_op(OpCode::OP_DEFINE_PROPERTY, property->line);
        emit_short(static_cast<uint16_t>(property_name_idx), property->line);
        uint8_t flags = (property->is_let ? 1 : 0) | (1 << 1); // bit 1 = is_static
        emit_byte(flags, property->line);
    }

    // Compile methods (separate static and instance methods)
    for (const auto& method : stmt->methods) {
        if (method->is_static && method->name == "main" && method->params.empty()) {
            record_entry_main_static(stmt->name, method->line);
        }

        if (method->is_static) {
            // Static method: no 'self' parameter
            FunctionPrototype proto;
            proto.name = method->name;
            proto.params.reserve(method->params.size());
            proto.param_labels.reserve(method->params.size());
            proto.param_defaults.reserve(method->params.size());
            for (const auto& param : method->params) {
                proto.params.push_back(param.internal_name);
                proto.param_labels.push_back(param.external_name);
                proto.param_defaults.push_back(build_param_default(param));
            }
            proto.is_initializer = false;
            proto.is_override = false;

            Compiler method_compiler;
            method_compiler.enclosing_ = this;
            method_compiler.chunk_ = Assembly{};
            method_compiler.locals_.clear();
            method_compiler.scope_depth_ = 1;
            method_compiler.recursion_depth_ = 0;

            // No 'self' for static methods
            for (const auto& param : method->params) {
                method_compiler.declare_local(param.internal_name, param.type.is_optional);
                method_compiler.mark_local_initialized();
            }

            if (method->body) {
                for (const auto& statement : method->body->statements) {
                    method_compiler.compile_stmt(statement.get());
                }
            }

            method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
            method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

            proto.chunk = finalize_function_chunk(std::move(method_compiler.chunk_));
            record_method_body(stmt->name, proto.name, true, extract_param_types(method->params), *proto.chunk);
            proto.upvalues.reserve(method_compiler.upvalues_.size());
            for (const auto& uv : method_compiler.upvalues_) {
                proto.upvalues.push_back({uv.index, uv.is_local});
            }

            size_t function_index = chunk_.add_function(std::move(proto));
            if (function_index > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many functions in chunk", stmt->line);
            }

            bool has_captures = !method_compiler.upvalues_.empty();
            emit_op(has_captures ? OpCode::OP_CLOSURE : OpCode::OP_FUNCTION, stmt->line);
            emit_short(static_cast<uint16_t>(function_index), stmt->line);

            size_t method_name_idx = identifier_constant(method->name);
            if (method_name_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many method identifiers", stmt->line);
            }

            // Use OP_METHOD with is_static flag
            emit_op(OpCode::OP_METHOD, stmt->line);
            emit_short(static_cast<uint16_t>(method_name_idx), stmt->line);
            continue;
        }
        
        // Instance method (original code)
        FunctionPrototype proto;
        proto.name = method->name;
        proto.params.reserve(method->params.size() + 1);
        proto.param_labels.reserve(method->params.size() + 1);
        proto.param_defaults.reserve(method->params.size() + 1);
        proto.params.push_back("self");  // Implicit self parameter
        proto.param_labels.push_back("");
        proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
        for (const auto& param : method->params) {
            proto.params.push_back(param.internal_name);
            proto.param_labels.push_back(param.external_name);
            proto.param_defaults.push_back(build_param_default(param));
        }
        proto.is_initializer = false;
        proto.is_override = false;

        Compiler method_compiler;
        method_compiler.enclosing_ = this;
        method_compiler.chunk_ = Assembly{};
        method_compiler.locals_.clear();
        method_compiler.scope_depth_ = 1;
        method_compiler.recursion_depth_ = 0;
        method_compiler.current_class_properties_ = &property_lookup;
        method_compiler.allow_implicit_self_property_ = true;
        method_compiler.in_struct_method_ = true;
        method_compiler.in_mutating_method_ = method->is_mutating;

        // Implicit self
        method_compiler.declare_local("self", false);
        method_compiler.mark_local_initialized();

        for (const auto& param : method->params) {
            method_compiler.declare_local(param.internal_name, param.type.is_optional);
            method_compiler.mark_local_initialized();
        }

        if (method->body) {
            for (const auto& statement : method->body->statements) {
                method_compiler.compile_stmt(statement.get());
            }
        }

        method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
        method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

        proto.chunk = finalize_function_chunk(std::move(method_compiler.chunk_));
        record_method_body(stmt->name, proto.name, false, extract_param_types(method->params), *proto.chunk);
        proto.upvalues.reserve(method_compiler.upvalues_.size());
        for (const auto& uv : method_compiler.upvalues_) {
            proto.upvalues.push_back({uv.index, uv.is_local});
        }

        size_t function_index = chunk_.add_function(std::move(proto));
        if (function_index > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many functions in chunk", stmt->line);
        }

        bool has_captures = !method_compiler.upvalues_.empty();
        emit_op(has_captures ? OpCode::OP_CLOSURE : OpCode::OP_FUNCTION, stmt->line);
        emit_short(static_cast<uint16_t>(function_index), stmt->line);

        size_t method_name_idx = identifier_constant(method->name);
        if (method_name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many method identifiers", stmt->line);
        }

        // Use OP_STRUCT_METHOD with mutating flag
        emit_op(OpCode::OP_STRUCT_METHOD, stmt->line);
        emit_short(static_cast<uint16_t>(method_name_idx), stmt->line);
        emit_byte(method->is_mutating ? 1 : 0, stmt->line);
    }

    // Compile initializers
    for (const auto& init_method : stmt->initializers) {
        FunctionPrototype proto;
        proto.name = "init";
        proto.params.reserve(init_method->params.size() + 1);
        proto.param_labels.reserve(init_method->params.size() + 1);
        proto.param_defaults.reserve(init_method->params.size() + 1);
        proto.params.push_back("self");
        proto.param_labels.push_back("");
        proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
        for (const auto& param : init_method->params) {
            proto.params.push_back(param.internal_name);
            proto.param_labels.push_back(param.external_name);
            proto.param_defaults.push_back(build_param_default(param));
        }
        proto.is_initializer = true;
        proto.is_override = false;

        Compiler init_compiler;
        init_compiler.enclosing_ = this;
        init_compiler.chunk_ = Assembly{};
        init_compiler.locals_.clear();
        init_compiler.scope_depth_ = 1;
        init_compiler.recursion_depth_ = 0;
        init_compiler.current_class_properties_ = &property_lookup;
        init_compiler.allow_implicit_self_property_ = true;
        init_compiler.in_struct_method_ = true;
        init_compiler.in_mutating_method_ = true;  // init can always modify self

        // Implicit self
        init_compiler.declare_local("self", false);
        init_compiler.mark_local_initialized();

        for (const auto& param : init_method->params) {
            init_compiler.declare_local(param.internal_name, param.type.is_optional);
            init_compiler.mark_local_initialized();
        }

        if (init_method->body) {
            for (const auto& statement : init_method->body->statements) {
                init_compiler.compile_stmt(statement.get());
            }
        }

        init_compiler.emit_op(OpCode::OP_NIL, init_method->line);
        init_compiler.emit_op(OpCode::OP_RETURN, init_method->line);

        proto.chunk = finalize_function_chunk(std::move(init_compiler.chunk_));
        record_method_body(stmt->name, proto.name, false, extract_param_types(init_method->params), *proto.chunk);
        proto.upvalues.reserve(init_compiler.upvalues_.size());
        for (const auto& uv : init_compiler.upvalues_) {
            proto.upvalues.push_back({uv.index, uv.is_local});
        }

        size_t function_index = chunk_.add_function(std::move(proto));
        if (function_index > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many functions in chunk", init_method->line);
        }

        bool has_captures = !init_compiler.upvalues_.empty();
        emit_op(has_captures ? OpCode::OP_CLOSURE : OpCode::OP_FUNCTION, init_method->line);
        emit_short(static_cast<uint16_t>(function_index), init_method->line);

        size_t method_name_idx = identifier_constant("init");
        if (method_name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many method identifiers", init_method->line);
        }

        emit_op(OpCode::OP_STRUCT_METHOD, init_method->line);
        emit_short(static_cast<uint16_t>(method_name_idx), init_method->line);
        emit_byte(1, init_method->line);  // init is always mutating
    }

    // Store struct in variable
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


void Compiler::visit(EnumDeclStmt* stmt) {
    size_t name_idx = identifier_constant(stmt->name);

    // Emit OP_ENUM to create enum type object
    emit_op(OpCode::OP_ENUM, stmt->line);
    emit_short(static_cast<uint16_t>(name_idx), stmt->line);

    if (scope_depth_ > 0) {
        declare_local(stmt->name, false);
        mark_local_initialized();
    }

    // Define enum cases
    for (const auto& case_decl : stmt->cases) {
        size_t case_name_idx = identifier_constant(case_decl.name);
        if (case_name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many enum case identifiers", stmt->line);
        }

        // Push raw value if present
        if (case_decl.raw_value.has_value()) {
            emit_constant(case_decl.raw_value.value(), stmt->line);
        } else {
            emit_op(OpCode::OP_NIL, stmt->line);
        }

        // Define the enum case
        emit_op(OpCode::OP_ENUM_CASE, stmt->line);
        emit_short(static_cast<uint16_t>(case_name_idx), stmt->line);
        
        // For associated values (if any), store their count
        emit_byte(static_cast<uint8_t>(case_decl.associated_values.size()), stmt->line);
        for (const auto& [assoc_name, assoc_type] : case_decl.associated_values) {
            if (assoc_name == "_") {
                emit_short(std::numeric_limits<uint16_t>::max(), stmt->line);
                continue;
            }
            size_t label_idx = identifier_constant(assoc_name);
            if (label_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many associated value identifiers", stmt->line);
            }
            emit_short(static_cast<uint16_t>(label_idx), stmt->line);
        }
    }

    // Collect property names for implicit self access (for methods)
    std::unordered_set<std::string> property_lookup;

    // PASS 1: Compile computed properties first (like Class does)
    for (const auto& method : stmt->methods) {
        if (!method->is_computed_property) continue;
        
        // Computed property: var description: String { ... }
        size_t property_name_idx = identifier_constant(method->name);
        if (property_name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many property identifiers", stmt->line);
        }
        
        // Compile getter
        FunctionPrototype getter_proto;
        getter_proto.name = "get:" + method->name;
        getter_proto.params.push_back("self");
        getter_proto.param_labels.push_back("");
        getter_proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
        
        Compiler getter_compiler;
        getter_compiler.enclosing_ = this;
        getter_compiler.chunk_ = Assembly{};
        getter_compiler.locals_.clear();
        getter_compiler.scope_depth_ = 1;
        getter_compiler.recursion_depth_ = 0;
        getter_compiler.current_class_properties_ = &property_lookup;
        getter_compiler.allow_implicit_self_property_ = true;
        
        // Add 'self' as local
        getter_compiler.declare_local("self", false);
        getter_compiler.mark_local_initialized();
        
        // Compile getter body
        if (method->body) {
            for (const auto& statement : method->body->statements) {
                getter_compiler.compile_stmt(statement.get());
            }
        }
        
        // Implicit return nil if no explicit return
        getter_compiler.emit_op(OpCode::OP_NIL, stmt->line);
        getter_compiler.emit_op(OpCode::OP_RETURN, stmt->line);
        
        getter_proto.chunk = finalize_function_chunk(std::move(getter_compiler.chunk_));
        record_method_body(stmt->name, getter_proto.name, method->is_static, {}, *getter_proto.chunk);
        getter_proto.upvalues.reserve(getter_compiler.upvalues_.size());
        for (const auto& uv : getter_compiler.upvalues_) {
            getter_proto.upvalues.push_back({uv.index, uv.is_local});
        }
        
        size_t getter_idx = chunk_.add_function(std::move(getter_proto));
        
        // Enum computed properties are read-only (no setter)
        size_t setter_idx = 0xFFFF;
        
        // Emit computed property definition
        emit_op(OpCode::OP_DEFINE_COMPUTED_PROPERTY, stmt->line);
        emit_short(static_cast<uint16_t>(property_name_idx), stmt->line);
        emit_short(static_cast<uint16_t>(getter_idx), stmt->line);
        emit_short(static_cast<uint16_t>(setter_idx), stmt->line);
    }

    // PASS 2: Compile methods (like Class does)
    for (const auto& method : stmt->methods) {
        if (method->is_computed_property) continue;
        
        // Regular method: func describe() -> String { ... }
        FunctionPrototype proto;
        proto.name = method->name;
        proto.params.reserve(method->params.size() + 1);
        proto.param_labels.reserve(method->params.size() + 1);
        proto.param_defaults.reserve(method->params.size() + 1);
        proto.params.push_back("self");  // Implicit self parameter
        proto.param_labels.push_back("");
        proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
        for (const auto& param : method->params) {
            proto.params.push_back(param.internal_name);
            proto.param_labels.push_back(param.external_name);
            proto.param_defaults.push_back(build_param_default(param));
        }
        proto.is_initializer = false;
        proto.is_override = false;

        Compiler method_compiler;
        method_compiler.enclosing_ = this;
        method_compiler.chunk_ = Assembly{};
        method_compiler.locals_.clear();
        method_compiler.scope_depth_ = 1;
        method_compiler.recursion_depth_ = 0;
        method_compiler.current_class_properties_ = &property_lookup;
        method_compiler.allow_implicit_self_property_ = true;

        // Implicit self
        method_compiler.declare_local("self", false);
        method_compiler.mark_local_initialized();

        for (const auto& param : method->params) {
            method_compiler.declare_local(param.internal_name, param.type.is_optional);
            method_compiler.mark_local_initialized();
        }

        if (method->body) {
            for (const auto& statement : method->body->statements) {
                method_compiler.compile_stmt(statement.get());
            }
        }

        method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
        method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

        proto.chunk = finalize_function_chunk(std::move(method_compiler.chunk_));
        record_method_body(stmt->name, proto.name, method->is_static, extract_param_types(method->params), *proto.chunk);
        proto.upvalues.reserve(method_compiler.upvalues_.size());
        for (const auto& uv : method_compiler.upvalues_) {
            proto.upvalues.push_back({uv.index, uv.is_local});
        }

        size_t function_index = chunk_.add_function(std::move(proto));
        if (function_index > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many functions in chunk", stmt->line);
        }

        bool has_captures = !method_compiler.upvalues_.empty();
        emit_op(has_captures ? OpCode::OP_CLOSURE : OpCode::OP_FUNCTION, stmt->line);
        emit_short(static_cast<uint16_t>(function_index), stmt->line);

        size_t method_name_idx = identifier_constant(method->name);
        if (method_name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many method identifiers", stmt->line);
        }

        // Use OP_METHOD for enum methods
        emit_op(OpCode::OP_METHOD, stmt->line);
        emit_short(static_cast<uint16_t>(method_name_idx), stmt->line);
    }

    // Store enum in variable
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
        case ExprKind::Super:
            visit(static_cast<SuperExpr*>(expr));
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
        case ExprKind::TypeCast:
            visit(static_cast<TypeCastExpr*>(expr));
            break;
        case ExprKind::TypeCheck:
            visit(static_cast<TypeCheckExpr*>(expr));
            break;
        case ExprKind::TupleLiteral:
            visit(static_cast<TupleLiteralExpr*>(expr));
            break;
        case ExprKind::TupleMember:
            visit(static_cast<TupleMemberExpr*>(expr));
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
    emit_op(OpCode::OP_COPY_VALUE, stmt->line);

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

void Compiler::visit(TupleDestructuringStmt* stmt) {
    // Compile the initializer (tuple expression)
    compile_expr(stmt->initializer.get());

    // For each binding, extract the tuple element and store it
    for (size_t i = 0; i < stmt->bindings.size(); ++i) {
        const auto& binding = stmt->bindings[i];

        // Skip wildcard patterns
        if (binding.name == "_") {
            continue;
        }

        // Duplicate the tuple on the stack
        emit_op(OpCode::OP_DUP, stmt->line);

        // Get the element by index or label
        if (binding.label.has_value()) {
            // Access by label
            emit_op(OpCode::OP_GET_TUPLE_LABEL, stmt->line);
            size_t label_idx = identifier_constant(binding.label.value());
            if (label_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many identifiers", stmt->line);
            }
            emit_short(static_cast<uint16_t>(label_idx), stmt->line);
        } else {
            // Access by index
            emit_op(OpCode::OP_GET_TUPLE_INDEX, stmt->line);
            if (i > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Tuple index too large", stmt->line);
            }
            emit_short(static_cast<uint16_t>(i), stmt->line);
        }

        emit_op(OpCode::OP_COPY_VALUE, stmt->line);

        // Declare the local variable
        if (scope_depth_ > 0) {
            declare_local(binding.name, false);
            mark_local_initialized();
        } else {
            size_t name_idx = identifier_constant(binding.name);
            if (name_idx > std::numeric_limits<uint16_t>::max()) {
                throw CompilerError("Too many global variables", stmt->line);
            }
            emit_op(OpCode::OP_SET_GLOBAL, stmt->line);
            emit_short(static_cast<uint16_t>(name_idx), stmt->line);
            emit_op(OpCode::OP_POP, stmt->line);
        }
    }

    // Pop the original tuple from the stack
    emit_op(OpCode::OP_POP, stmt->line);
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
    size_t loop_start = chunk_.code_size();
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

void Compiler::visit(RepeatWhileStmt* stmt) {
    // repeat-while: body executes at least once, then checks condition
    loop_stack_.push_back({});
    size_t loop_start = chunk_.code_size();
    loop_stack_.back().loop_start = loop_start;
    loop_stack_.back().scope_depth_at_start = scope_depth_;
    
    // Execute body first
    compile_stmt(stmt->body.get());
    
    // continue jumps come here (before condition check)
    for (size_t jump : loop_stack_.back().continue_jumps) {
        patch_jump(jump);
    }
    
    // Check condition
    compile_expr(stmt->condition.get());
    
    // If true, loop back to start
    size_t exit_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
    emit_op(OpCode::OP_POP, stmt->line);
    emit_loop(loop_start, stmt->line);
    
    // If false, exit
    patch_jump(exit_jump);
    emit_op(OpCode::OP_POP, stmt->line);
    
    // Patch break jumps
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
        size_t loop_start = chunk_.code_size();
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
        
        // where clause filtering
        size_t where_skip_jump = 0;
        if (stmt->where_condition) {
            compile_expr(stmt->where_condition.get());
            where_skip_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
            emit_op(OpCode::OP_POP, stmt->line);
        }
        
        compile_stmt(stmt->body.get());
        
        // Skip to increment if where condition was false
        if (stmt->where_condition) {
            patch_jump(where_skip_jump);
            emit_op(OpCode::OP_POP, stmt->line);
        }
        
        
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
    size_t loop_start = chunk_.code_size();
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
        
        // where clause filtering
        size_t where_skip_jump = 0;
        if (stmt->where_condition) {
            compile_expr(stmt->where_condition.get());
            where_skip_jump = emit_jump(OpCode::OP_JUMP_IF_FALSE, stmt->line);
            emit_op(OpCode::OP_POP, stmt->line);
        }
        
        // Execute loop body
        compile_stmt(stmt->body.get());
        
        // Skip to increment if where condition was false
        if (stmt->where_condition) {
            patch_jump(where_skip_jump);
            emit_op(OpCode::OP_POP, stmt->line);
        }
        
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
        const EnumCasePattern* binding_pattern = nullptr;
        
        for (size_t i = 0; i < case_clause.patterns.size(); ++i) {
            const auto& pattern = case_clause.patterns[i];
            
            // Load switch value
            emit_op(OpCode::OP_GET_LOCAL, stmt->line);
            emit_short(static_cast<uint16_t>(switch_var), stmt->line);
            
            if (pattern->kind == PatternKind::EnumCase) {
                auto* enum_pattern = static_cast<EnumCasePattern*>(pattern.get());
                if (!enum_pattern->bindings.empty()) {
                    if (binding_pattern && binding_pattern != enum_pattern) {
                        throw CompilerError("Multiple enum case patterns with bindings in one case are not supported.", stmt->line);
                    }
                    binding_pattern = enum_pattern;
                }

                size_t case_name_idx = identifier_constant(enum_pattern->case_name);
                if (case_name_idx > std::numeric_limits<uint16_t>::max()) {
                    throw CompilerError("Too many enum case identifiers", stmt->line);
                }
                emit_op(OpCode::OP_MATCH_ENUM_CASE, stmt->line);
                emit_short(static_cast<uint16_t>(case_name_idx), stmt->line);
            } else {
                auto* expr_pattern = static_cast<ExpressionPattern*>(pattern.get());
                // Check if pattern is a range
                if (expr_pattern->expression->kind == ExprKind::Range) {
                    auto* range = static_cast<RangeExpr*>(expr_pattern->expression.get());
                
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
                    compile_expr(expr_pattern->expression.get());
                    emit_op(OpCode::OP_EQUAL, stmt->line);
                }
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

        bool has_binding_scope = false;
        if (binding_pattern && !binding_pattern->bindings.empty()) {
            if (case_clause.patterns.size() > 1) {
                throw CompilerError("Enum case bindings require a single pattern per case.", stmt->line);
            }
            begin_scope();
            has_binding_scope = true;

            for (size_t i = 0; i < binding_pattern->bindings.size(); ++i) {
                const std::string& binding_name = binding_pattern->bindings[i];
                declare_local(binding_name, false);
                emit_op(OpCode::OP_GET_LOCAL, stmt->line);
                emit_short(static_cast<uint16_t>(switch_var), stmt->line);
                emit_op(OpCode::OP_GET_ASSOCIATED, stmt->line);
                emit_short(static_cast<uint16_t>(i), stmt->line);
                mark_local_initialized();
            }
        }
        
        // Execute case body
        for (const auto& case_stmt : case_clause.statements) {
            compile_stmt(case_stmt.get());
        }

        if (has_binding_scope) {
            end_scope();
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

void Compiler::visit(ImportStmt* stmt) {
    const std::string& module_key = stmt->module_path;

    // Check for circular dependency
    if (compiling_modules_.find(module_key) != compiling_modules_.end()) {
        throw CompilerError("Circular import detected: " + module_key, stmt->line);
    }
    
    // Check if already imported
    if (imported_modules_.find(module_key) != imported_modules_.end()) {
        // Module already imported, skip
        return;
    }
    
    // Mark as imported
    imported_modules_.insert(module_key);
    compiling_modules_.insert(module_key);
    
    try {
        std::string full_path;
        std::string source;

        if (module_resolver_) {
            std::string err;
            if (!module_resolver_->ResolveAndLoad(module_key, full_path, source, err)) {
                 throw CompilerError("Cannot resolve import '" + module_key + "': " + err, stmt->line);
            }
        } else {
            // Fallback (existing behavior): base_directory + module_path (append .ss automatically if needed)
            full_path = module_key;
            if (!base_directory_.empty() && module_key[0] != '/' && module_key[0] != '\\') {
                // Resolve relative path
                full_path = base_directory_ + "/" + module_key;
            }
            
            // Auto-append .ss extension
            if (full_path.size() < 3 || full_path.substr(full_path.size()-3) != ".ss") {
                full_path += ".ss";
            }
            
            // Read the file content
            std::ifstream file(full_path);
            if (!file.is_open()) {
                throw CompilerError("Cannot open import file: " + full_path, stmt->line);
            }
            
            source.assign((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
            file.close();
        }
        
        // Tokenize and parse the imported module
        Lexer lexer(source);
        auto tokens = lexer.tokenize_all();
        Parser parser(std::move(tokens));
        auto imported_program = parser.parse();
        
        std::vector<std::string> module_exports;
        std::unordered_set<std::string> seen_exports;
        module_exports.reserve(imported_program.size());

        // Compile the imported module statements into current chunk
        for (const auto& imported_stmt : imported_program) {
            if (imported_stmt) {
                if (imported_stmt->kind == StmtKind::FuncDecl) {
                    const auto* func_decl = static_cast<const FuncDeclStmt*>(imported_stmt.get());
                    if (seen_exports.insert(func_decl->name).second) {
                        module_exports.push_back(func_decl->name);
                    }
                }
                compile_stmt(imported_stmt.get());
            }
        }

        emit_module_namespace(module_key, module_exports, stmt->line);
        
        // Remove from compiling set
        compiling_modules_.erase(module_key);
        
    } catch (const std::exception& e) {
        compiling_modules_.erase(module_key);
        throw CompilerError("Error importing module '" + module_key + "': " + e.what(), stmt->line);
    }
}

void Compiler::visit(ProtocolDeclStmt* stmt) {
    // Protocols are a compile-time/type-system feature
    // At runtime, we store protocol definitions as metadata
    // For now, we'll create a protocol object that stores requirements
    
    // Create protocol metadata
    auto protocol = std::make_shared<Protocol>();
    protocol->name = stmt->name;
    
    // Store method requirements
    for (const auto& method_req : stmt->method_requirements) {
        ProtocolMethodReq req;
        req.name = method_req.name;
        req.is_mutating = method_req.is_mutating;
        for (const auto& param : method_req.params) {
            req.param_names.push_back(param.internal_name);
        }
        protocol->method_requirements.push_back(req);
    }
    
    // Store property requirements
    for (const auto& prop_req : stmt->property_requirements) {
        ProtocolPropertyReq req;
        req.name = prop_req.name;
        req.has_getter = prop_req.has_getter;
        req.has_setter = prop_req.has_setter;
        protocol->property_requirements.push_back(req);
    }
    
    // Store inherited protocols
    protocol->inherited_protocols = stmt->inherited_protocols;
    
    // Add protocol to constants as a protocol value
    size_t protocol_idx = chunk_.add_protocol(protocol);
    if (protocol_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many protocols in chunk", stmt->line);
    }
    
    // Define protocol in global scope
    emit_op(OpCode::OP_PROTOCOL, stmt->line);
    emit_short(static_cast<uint16_t>(protocol_idx), stmt->line);
    
    if (scope_depth_ == 0) {
        size_t name_idx = identifier_constant(stmt->name);
        emit_op(OpCode::OP_DEFINE_GLOBAL, stmt->line);
        emit_short(static_cast<uint16_t>(name_idx), stmt->line);
    } else {
        declare_local(stmt->name, false);
        mark_local_initialized();
    }
}

void Compiler::visit(ExtensionDeclStmt* stmt) {
    // Extension adds methods to an existing type
    // 1. Load the type (class, struct, or enum) from globals
    emit_variable_get(stmt->extended_type, stmt->line);
    
    // 2. Compile each method and add it to the type
    for (const auto& method : stmt->methods) {
        // Load the type again (for each method attachment)
        emit_variable_get(stmt->extended_type, stmt->line);
        
        if (method->is_computed_property) {
            // Computed property
            size_t method_name_idx = identifier_constant(method->name);
            
            // Compile getter
            std::string getter_name = "$get_" + method->name;
            FunctionPrototype getter_proto;
            getter_proto.name = getter_name;
            getter_proto.is_override = false;
            getter_proto.params.push_back("self");
            getter_proto.param_labels.push_back("");
            getter_proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
            
            Compiler method_compiler;
            method_compiler.enclosing_ = this;
            method_compiler.allow_implicit_self_property_ = true;
            
            // Allow access to 'self' in computed property getter
            method_compiler.begin_scope();
            method_compiler.declare_local("self", false);
            method_compiler.mark_local_initialized();
            
            // Compile body
            for (const auto& body_stmt : method->body->statements) {
                method_compiler.compile_stmt(body_stmt.get());
            }
            
            // Implicit return nil if no explicit return
            method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
            method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);
            
            getter_proto.chunk = finalize_function_chunk(std::move(method_compiler.chunk_));
            record_method_body(stmt->extended_type, getter_proto.name, method->is_static, {}, *getter_proto.chunk);
            size_t func_idx = chunk_.add_function(std::move(getter_proto));
            
            // Emit OP_DEFINE_COMPUTED_PROPERTY with indices
            emit_op(OpCode::OP_DEFINE_COMPUTED_PROPERTY, stmt->line);
            emit_short(static_cast<uint16_t>(method_name_idx), stmt->line);
            emit_short(static_cast<uint16_t>(func_idx), stmt->line); // getter index
            emit_short(0xFFFF, stmt->line); // No setter
        } else {
            // Regular method
            size_t method_name_idx = identifier_constant(method->name);

            FunctionPrototype func_proto;
            func_proto.name = method->name;
            func_proto.is_override = false;

            // Compile method body
            Compiler method_compiler;
            method_compiler.enclosing_ = this;
            method_compiler.in_struct_method_ = method->is_mutating;
            method_compiler.in_mutating_method_ = method->is_mutating;

            method_compiler.begin_scope();

            // Static methods don't have 'self' parameter
            if (!method->is_static) {
                method_compiler.allow_implicit_self_property_ = true;
                method_compiler.declare_local("self", false);
                method_compiler.mark_local_initialized();
                func_proto.params.push_back("self");
                func_proto.param_labels.push_back("");
                func_proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
            } else {
                method_compiler.allow_implicit_self_property_ = false;
            }

            // Add method parameters
            for (const auto& param : method->params) {
                method_compiler.declare_local(param.internal_name, param.type.is_optional);
                method_compiler.mark_local_initialized();
                func_proto.params.push_back(param.internal_name);
                func_proto.param_labels.push_back(param.external_name);
                func_proto.param_defaults.push_back(build_param_default(param));
            }

            // Compile body
            for (const auto& body_stmt : method->body->statements) {
                method_compiler.compile_stmt(body_stmt.get());
            }

            // Implicit return nil if no explicit return
            method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
            method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

            func_proto.chunk = finalize_function_chunk(std::move(method_compiler.chunk_));
            record_method_body(stmt->extended_type, func_proto.name, method->is_static, extract_param_types(method->params), *func_proto.chunk);
            size_t func_idx = chunk_.add_function(std::move(func_proto));

            emit_op(OpCode::OP_FUNCTION, stmt->line);
            emit_short(static_cast<uint16_t>(func_idx), stmt->line);

            // Attach method to type (works for class, struct, and enum)
            if (method->is_mutating) {
                emit_op(OpCode::OP_STRUCT_METHOD, stmt->line);
                emit_short(static_cast<uint16_t>(method_name_idx), stmt->line);
                emit_byte(1, stmt->line); // mutating flag
            } else {
                emit_op(OpCode::OP_METHOD, stmt->line);
                emit_short(static_cast<uint16_t>(method_name_idx), stmt->line);
            }
        }
        
        // Pop the type object after attaching method
        emit_op(OpCode::OP_POP, stmt->line);
    }
    
    // Pop the final type reference
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

void Compiler::visit(ThrowStmt* stmt) {
    // Compile the error value
    if (stmt->value) {
        compile_expr(stmt->value.get());
    } else {
        emit_op(OpCode::OP_NIL, stmt->line);
    }
    
    // For now, throw just causes a runtime error
    // In a full implementation, this would unwind the stack to the nearest catch
    emit_op(OpCode::OP_THROW, stmt->line);
}

void Compiler::visit(FuncDeclStmt* stmt) {
    FunctionPrototype proto;
    proto.name = stmt->name;
    proto.params.reserve(stmt->params.size());
    proto.param_labels.reserve(stmt->params.size());
    proto.param_defaults.reserve(stmt->params.size());
    for (const auto& param : stmt->params) {
        proto.params.push_back(param.internal_name);
        proto.param_labels.push_back(param.external_name);
        proto.param_defaults.push_back(build_param_default(param));
    }

    Compiler function_compiler;
    function_compiler.enclosing_ = this;
    function_compiler.chunk_ = Assembly{};
    function_compiler.locals_.clear();
    function_compiler.scope_depth_ = 1;
    function_compiler.recursion_depth_ = 0;

    for (const auto& param : stmt->params) {
        function_compiler.declare_local(param.internal_name, param.type.is_optional);
        function_compiler.mark_local_initialized();
    }

    if (stmt->body) {
        for (const auto& statement : stmt->body->statements) {
            function_compiler.compile_stmt(statement.get());
        }
    }

    function_compiler.emit_op(OpCode::OP_NIL, stmt->line);
    function_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

    proto.chunk = finalize_function_chunk(std::move(function_compiler.chunk_));
    record_method_body("", proto.name, false, extract_param_types(stmt->params), *proto.chunk);
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
        
        if (stmt->name == "main" && stmt->params.empty()) {
            record_entry_main_global(stmt);
        }

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
// Check if this is a generic type instantiation
std::string actual_name = expr->name;
if (!expr->generic_args.empty()) {
    // Mangle the name: Box<Int> -> Box_Int
    actual_name = mangle_generic_name(expr->name, expr->generic_args);
}
    
int local = resolve_local(actual_name);
if (local != -1) {
    emit_op(OpCode::OP_GET_LOCAL, expr->line);
    emit_short(static_cast<uint16_t>(local), expr->line);
    return;
}
    
    
    
    int upvalue = resolve_upvalue(actual_name);
    if (upvalue != -1) {
        emit_op(OpCode::OP_GET_UPVALUE, expr->line);
        emit_short(static_cast<uint16_t>(upvalue), expr->line);
        return;
    }

    if (is_implicit_property(actual_name)) {
        emit_self_property_get(actual_name, expr->line);
        return;
    }

    size_t name_idx = identifier_constant(actual_name);
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
    // Special case: member assignment (obj.prop = value)
    if (expr->op == TokenType::Equal && expr->left->kind == ExprKind::Member) {
        auto* member = static_cast<MemberExpr*>(expr->left.get());
        
        // Compile object
        compile_expr(member->object.get());
        
        // Compile value
        compile_expr(expr->right.get());
        emit_op(OpCode::OP_COPY_VALUE, expr->line);
        
        // Emit SET_PROPERTY
        size_t name_idx = identifier_constant(member->member);
        if (name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many identifiers", expr->line);
        }
        emit_op(OpCode::OP_SET_PROPERTY, expr->line);
        emit_short(static_cast<uint16_t>(name_idx), expr->line);
        return;
    }
    
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
        case TokenType::BitwiseAnd:
            emit_op(OpCode::OP_BITWISE_AND, expr->line);
            break;
        case TokenType::BitwiseOr:
            emit_op(OpCode::OP_BITWISE_OR, expr->line);
            break;
        case TokenType::BitwiseXor:
            emit_op(OpCode::OP_BITWISE_XOR, expr->line);
            break;
        case TokenType::LeftShift:
            emit_op(OpCode::OP_LEFT_SHIFT, expr->line);
            break;
        case TokenType::RightShift:
            emit_op(OpCode::OP_RIGHT_SHIFT, expr->line);
            break;
        default:
            throw CompilerError("Unsupported binary operator", expr->line);
    }
}

void Compiler::visit(AssignExpr* expr) {
    bool is_property = is_implicit_property(expr->name);

    // ���� ���� ������ ó��
    if (expr->op != TokenType::Equal) {
        // x += 5 -> x = x + 5 로 변환
        if (is_property) {
            size_t name_idx = identifier_constant(expr->name);
            emit_load_self(expr->line);            // for final set
            emit_load_self(expr->line);            // for current value
            emit_op(OpCode::OP_GET_PROPERTY, expr->line);
            emit_short(static_cast<uint16_t>(name_idx), expr->line);

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
                case TokenType::PercentEqual:
                    emit_op(OpCode::OP_MODULO, expr->line);
                    break;
                case TokenType::AndEqual:
                    emit_op(OpCode::OP_BITWISE_AND, expr->line);
                    break;
                case TokenType::OrEqual:
                    emit_op(OpCode::OP_BITWISE_OR, expr->line);
                    break;
                case TokenType::XorEqual:
                    emit_op(OpCode::OP_BITWISE_XOR, expr->line);
                    break;
                case TokenType::LeftShiftEqual:
                    emit_op(OpCode::OP_LEFT_SHIFT, expr->line);
                    break;
                case TokenType::RightShiftEqual:
                    emit_op(OpCode::OP_RIGHT_SHIFT, expr->line);
                    break;
                default:
                    throw CompilerError("Unsupported compound assignment", expr->line);
            }

            emit_op(OpCode::OP_COPY_VALUE, expr->line);
            emit_op(OpCode::OP_SET_PROPERTY, expr->line);
            emit_short(static_cast<uint16_t>(name_idx), expr->line);
            return;
        }

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
            case TokenType::PercentEqual:
                emit_op(OpCode::OP_MODULO, expr->line);
                break;
            case TokenType::AndEqual:
                emit_op(OpCode::OP_BITWISE_AND, expr->line);
                break;
            case TokenType::OrEqual:
                emit_op(OpCode::OP_BITWISE_OR, expr->line);
                break;
            case TokenType::XorEqual:
                emit_op(OpCode::OP_BITWISE_XOR, expr->line);
                break;
            case TokenType::LeftShiftEqual:
                emit_op(OpCode::OP_LEFT_SHIFT, expr->line);
                break;
            case TokenType::RightShiftEqual:
                emit_op(OpCode::OP_RIGHT_SHIFT, expr->line);
                break;
            default:
                throw CompilerError("Unsupported compound assignment", expr->line);
        }
        emit_op(OpCode::OP_COPY_VALUE, expr->line);
    } else {
        // 일반 할당
        if (is_property) {
            size_t name_idx = identifier_constant(expr->name);
            emit_load_self(expr->line);
            compile_expr(expr->value.get());
            emit_op(OpCode::OP_COPY_VALUE, expr->line);
            emit_op(OpCode::OP_SET_PROPERTY, expr->line);
            emit_short(static_cast<uint16_t>(name_idx), expr->line);
            return;
        }
        compile_expr(expr->value.get());
        emit_op(OpCode::OP_COPY_VALUE, expr->line);
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

void Compiler::visit(SuperExpr* expr) {
    if (!current_class_has_super_) {
        throw CompilerError("'super' is only available inside subclasses", expr->line);
    }
    emit_load_self(expr->line);
    size_t name_idx = identifier_constant(expr->method);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", expr->line);
    }
    emit_op(OpCode::OP_SUPER, expr->line);
    emit_short(static_cast<uint16_t>(name_idx), expr->line);
}

void Compiler::visit(CallExpr* expr) {
    if (expr->callee->kind == ExprKind::Identifier) {
        auto* identifier = static_cast<IdentifierExpr*>(expr->callee.get());
        if (identifier->name == "readLine" && expr->arguments.empty()) {
            emit_op(OpCode::OP_READ_LINE, expr->line);
            return;
        }
    }

    compile_expr(expr->callee.get());

    if (expr->arguments.size() > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many arguments in function call", expr->line);
    }

    bool has_named_args = false;
    for (size_t i = 0; i < expr->arguments.size(); ++i) {
        compile_expr(expr->arguments[i].get());
        emit_op(OpCode::OP_COPY_VALUE, expr->line);
        if (!expr->argument_names[i].empty()) {
            has_named_args = true;
        }
    }

    if (has_named_args) {
        emit_op(OpCode::OP_CALL_NAMED, expr->line);
        emit_short(static_cast<uint16_t>(expr->arguments.size()), expr->line);
        for (const auto& arg_name : expr->argument_names) {
            if (arg_name.empty()) {
                emit_short(std::numeric_limits<uint16_t>::max(), expr->line);
            } else {
                size_t name_idx = identifier_constant(arg_name);
                if (name_idx > std::numeric_limits<uint16_t>::max()) {
                    throw CompilerError("Too many argument name identifiers", expr->line);
                }
                emit_short(static_cast<uint16_t>(name_idx), expr->line);
            }
        }
    } else {
        emit_op(OpCode::OP_CALL, expr->line);
        emit_short(static_cast<uint16_t>(expr->arguments.size()), expr->line);
    }
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

void Compiler::visit(TupleLiteralExpr* expr) {
    // Push all elements onto the stack
    for (const auto& elem : expr->elements) {
        compile_expr(elem.value.get());
    }

    if (expr->elements.size() > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many elements in tuple literal", expr->line);
    }

    // Emit OP_TUPLE with element count
    emit_op(OpCode::OP_TUPLE, expr->line);
    emit_short(static_cast<uint16_t>(expr->elements.size()), expr->line);

    // Emit labels (as string constants)
    for (const auto& elem : expr->elements) {
        if (elem.label.has_value()) {
            size_t label_idx = identifier_constant(elem.label.value());
            emit_short(static_cast<uint16_t>(label_idx), expr->line);
        } else {
            emit_short(static_cast<uint16_t>(0xFFFF), expr->line);  // No label marker
        }
    }
}

void Compiler::visit(TupleMemberExpr* expr) {
    compile_expr(expr->tuple.get());

    if (std::holds_alternative<size_t>(expr->member)) {
        // Index access: tuple.0, tuple.1
        size_t index = std::get<size_t>(expr->member);
        emit_op(OpCode::OP_GET_TUPLE_INDEX, expr->line);
        emit_short(static_cast<uint16_t>(index), expr->line);
    } else {
        // Label access: tuple.x, tuple.y
        const std::string& label = std::get<std::string>(expr->member);
        size_t label_idx = identifier_constant(label);
        emit_op(OpCode::OP_GET_TUPLE_LABEL, expr->line);
        emit_short(static_cast<uint16_t>(label_idx), expr->line);
    }
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
    proto.param_labels.reserve(expr->params.size());
    proto.param_defaults.reserve(expr->params.size());
    for (const auto& [param_name, param_type] : expr->params) {
        proto.params.push_back(param_name);
        proto.param_labels.push_back("");
        proto.param_defaults.push_back(FunctionPrototype::ParamDefaultValue{});
    }

    Compiler closure_compiler;
    closure_compiler.enclosing_ = this;
    closure_compiler.chunk_ = Assembly{};
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

    proto.chunk = finalize_function_chunk(std::move(closure_compiler.chunk_));
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

void Compiler::visit(TypeCastExpr* expr) {
    // Compile the value expression
    compile_expr(expr->value.get());
    
    // Store the target type name
    size_t type_name_idx = identifier_constant(expr->target_type.name);
    if (type_name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Type name index out of range", expr->line);
    }
    
    // Emit appropriate OPCODE
    if (expr->is_optional) {
        emit_op(OpCode::OP_TYPE_CAST_OPTIONAL, expr->line);
    } else if (expr->is_forced) {
        emit_op(OpCode::OP_TYPE_CAST_FORCED, expr->line);
    } else {
        emit_op(OpCode::OP_TYPE_CAST, expr->line);
    }
    emit_short(static_cast<uint16_t>(type_name_idx), expr->line);
}

void Compiler::visit(TypeCheckExpr* expr) {
    // Compile the value expression
    compile_expr(expr->value.get());
    
    // Store the target type name
    size_t type_name_idx = identifier_constant(expr->target_type.name);
    if (type_name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Type name index out of range", expr->line);
    }
    
    emit_op(OpCode::OP_TYPE_CHECK, expr->line);
    emit_short(static_cast<uint16_t>(type_name_idx), expr->line);
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

bool Compiler::is_implicit_property(const std::string& name) const {
    return allow_implicit_self_property_
        && current_class_properties_
        && current_class_properties_->find(name) != current_class_properties_->end();
}

int Compiler::resolve_self_index() const {
    for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; --i) {
        if (locals_[i].name == "self") {
            if (locals_[i].depth == -1) {
                throw CompilerError("Cannot read 'self' before initialization");
            }
            return i;
        }
    }
    return -1;
}

void Compiler::emit_load_self(uint32_t line) {
    int self_index = resolve_self_index();
    if (self_index == -1) {
        throw CompilerError("'self' not available in this context", line);
    }
    emit_op(OpCode::OP_GET_LOCAL, line);
    emit_short(static_cast<uint16_t>(self_index), line);
}

void Compiler::emit_self_property_get(const std::string& name, uint32_t line) {
    emit_load_self(line);
    size_t name_idx = identifier_constant(name);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", line);
    }
    emit_op(OpCode::OP_GET_PROPERTY, line);
    emit_short(static_cast<uint16_t>(name_idx), line);
}

void Compiler::emit_self_property_set(const std::string& name, uint32_t line) {
    size_t name_idx = identifier_constant(name);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", line);
    }
    emit_op(OpCode::OP_SET_PROPERTY, line);
    emit_short(static_cast<uint16_t>(name_idx), line);
}

void Compiler::emit_variable_get(const std::string& name, uint32_t line) {
    int local = resolve_local(name);
    if (local != -1) {
        emit_op(OpCode::OP_GET_LOCAL, line);
        emit_short(static_cast<uint16_t>(local), line);
        return;
    }

    int upvalue = resolve_upvalue(name);
    if (upvalue != -1) {
        emit_op(OpCode::OP_GET_UPVALUE, line);
        emit_short(static_cast<uint16_t>(upvalue), line);
        return;
    }

    size_t name_idx = identifier_constant(name);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", line);
    }
    emit_op(OpCode::OP_GET_GLOBAL, line);
    emit_short(static_cast<uint16_t>(name_idx), line);
}

void Compiler::emit_module_namespace(const std::string& module_key,
                                     const std::vector<std::string>& exports,
                                     uint32_t line) {
    std::filesystem::path module_path(module_key);
    std::string module_name = module_path.stem().string();
    if (module_name.empty()) {
        module_name = module_key;
    }

    for (const auto& name : exports) {
        emit_string(name, line);
        emit_variable_get(name, line);
    }

    if (exports.size() > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many exports in module namespace", line);
    }

    emit_op(OpCode::OP_DICT, line);
    emit_short(static_cast<uint16_t>(exports.size()), line);

    size_t name_idx = identifier_constant(module_name);
    if (name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Too many identifiers", line);
    }
    emit_op(OpCode::OP_SET_GLOBAL, line);
    emit_short(static_cast<uint16_t>(name_idx), line);
    emit_op(OpCode::OP_POP, line);
}

FunctionPrototype::ParamDefaultValue Compiler::build_param_default(const ParamDecl& param) {
    FunctionPrototype::ParamDefaultValue def;
    if (!param.default_value) {
        return def;
    }

    def.has_default = true;
    Expr* expr = param.default_value.get();

    if (expr->kind == ExprKind::Literal) {
        auto* literal = static_cast<LiteralExpr*>(expr);
        if (literal->string_value) {
            def.string_value = *literal->string_value;
            return def;
        }
        def.value = literal->value;
        return def;
    }

    if (expr->kind == ExprKind::Unary) {
        auto* unary = static_cast<UnaryExpr*>(expr);
        if (unary->op == TokenType::Minus && unary->operand->kind == ExprKind::Literal) {
            auto* literal = static_cast<LiteralExpr*>(unary->operand.get());
            if (literal->value.is_int()) {
                def.value = Value::from_int(-literal->value.as_int());
                return def;
            }
            if (literal->value.is_float()) {
                def.value = Value::from_float(-literal->value.as_float());
                return def;
            }
        }
    }

    throw CompilerError("Default parameter values must be literal constants", expr->line);
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
    if (chunk_.code_size() < loop_start) {
        throw CompilerError("Invalid loop offset calculation", line);
    }
    
    emit_op(OpCode::OP_LOOP, line);
    size_t offset = chunk_.code_size() - loop_start + 2;
    
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

std::string Compiler::build_method_key(const std::string& type_name,
                                       const std::string& method_name,
                                       bool is_static,
                                       const std::vector<TypeAnnotation>& param_types) const {
    std::string key = type_name;
    key += "::";
    if (is_static) {
        key += "static ";
    }
    key += method_name;
    key += "(";
    key += std::to_string(param_types.size());
    key += ":";
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (i > 0) {
            key += ",";
        }
        std::string type_name_part = param_types[i].name.empty() ? "_" : param_types[i].name;
        if (param_types[i].is_optional) {
            type_name_part += "?";
        }
        key += type_name_part;
    }
    key += ")";
    return key;
}

std::string Compiler::build_method_key(const std::string& type_name,
                                       const std::string& method_name,
                                       bool is_static,
                                       const std::vector<ParamDecl>& params) const {
    return build_method_key(type_name, method_name, is_static, extract_param_types(params));
}

std::vector<TypeAnnotation> Compiler::extract_param_types(const std::vector<ParamDecl>& params) const {
    std::vector<TypeAnnotation> param_types;
    param_types.reserve(params.size());
    for (const auto& param : params) {
        param_types.push_back(param.type);
    }
    return param_types;
}

std::vector<TypeAnnotation> Compiler::build_accessor_param_types(const std::optional<TypeAnnotation>& type) const {
    std::vector<TypeAnnotation> params;
    if (type.has_value()) {
        params.push_back(type.value());
    } else {
        TypeAnnotation placeholder{};
        placeholder.name = "_";
        params.push_back(placeholder);
    }
    return params;
}

body_idx Compiler::store_method_body(const Assembly& body_chunk) {
    MethodBody body{};
    body.bytecode = body_chunk.bytecode();
    body.line_info = body_chunk.line_info();
    body.max_stack_depth = 0;
    chunk_.method_bodies.push_back(std::move(body));
    return static_cast<body_idx>(chunk_.method_bodies.size() - 1);
}

void Compiler::record_method_body(const std::string& type_name,
                                  const std::string& method_name,
                                  bool is_static,
                                  const std::vector<TypeAnnotation>& param_types,
                                  const Assembly& body_chunk) {
    body_idx idx = store_method_body(body_chunk);
    method_body_lookup_[build_method_key(type_name, method_name, is_static, param_types)] = {idx};
}

std::shared_ptr<Assembly> Compiler::finalize_function_chunk(Assembly&& chunk) {
    chunk.expand_to_assembly();
    return std::make_shared<Assembly>(std::move(chunk));
}

Assembly Compiler::compile_function_body(const FuncDeclStmt& stmt) {
    Compiler function_compiler;
    function_compiler.chunk_ = Assembly{};
    function_compiler.locals_.clear();
    function_compiler.scope_depth_ = 1;
    function_compiler.recursion_depth_ = 0;

    for (const auto& param : stmt.params) {
        function_compiler.declare_local(param.internal_name, param.type.is_optional);
        function_compiler.mark_local_initialized();
    }

    if (stmt.body) {
        for (const auto& statement : stmt.body->statements) {
            function_compiler.compile_stmt(statement.get());
        }
    }

    function_compiler.emit_op(OpCode::OP_NIL, stmt.line);
    function_compiler.emit_op(OpCode::OP_RETURN, stmt.line);
    function_compiler.chunk_.expand_to_assembly();
    record_method_body("", stmt.name, false, extract_param_types(stmt.params), function_compiler.chunk_);
    return std::move(function_compiler.chunk_);
}

Assembly Compiler::compile_struct_method_body(const StructMethodDecl& method, bool is_mutating) {
    Compiler method_compiler;
    method_compiler.chunk_ = Assembly{};
    method_compiler.locals_.clear();
    method_compiler.scope_depth_ = 1;
    method_compiler.recursion_depth_ = 0;
    method_compiler.in_struct_method_ = true;
    method_compiler.in_mutating_method_ = is_mutating;

    // self is implicit first parameter
    method_compiler.declare_local("self", false);
    method_compiler.mark_local_initialized();

    for (const auto& param : method.params) {
        method_compiler.declare_local(param.internal_name, param.type.is_optional);
        method_compiler.mark_local_initialized();
    }

    if (method.body) {
        for (const auto& statement : method.body->statements) {
            method_compiler.compile_stmt(statement.get());
        }
    }

    method_compiler.emit_op(OpCode::OP_NIL, 0);
    method_compiler.emit_op(OpCode::OP_RETURN, 0);
    method_compiler.chunk_.expand_to_assembly();
    record_method_body("", method.name, method.is_static, extract_param_types(method.params), method_compiler.chunk_);
    return std::move(method_compiler.chunk_);
}

void Compiler::populate_metadata_tables(const std::vector<StmtPtr>& program) {
    struct PendingMethod {
        std::string name;
        uint32_t flags{0};
        std::vector<TypeAnnotation> params;
        std::optional<TypeAnnotation> return_type;
    };

    struct PendingField {
        std::string name;
        uint32_t flags{0};
        std::optional<TypeAnnotation> type;
    };

    struct PendingProperty {
        std::string name;
        uint32_t flags{0};
        std::optional<TypeAnnotation> type;
        bool has_getter{true};
        bool has_setter{false};
        std::string getter_name;
        std::string setter_name;
    };

    struct PendingType {
        uint32_t flags{0};
        std::optional<std::string> base_type;
        std::vector<std::string> interfaces;
        std::vector<PendingMethod> methods;
        std::vector<PendingField> fields;
        std::vector<PendingProperty> properties;
    };

    std::unordered_map<std::string, PendingType> pending_types;
    std::vector<std::string> type_order;

    auto ensure_pending_type = [&](const std::string& name) -> PendingType& {
        auto it = pending_types.find(name);
        if (it != pending_types.end()) {
            return it->second;
        }
        type_order.push_back(name);
        return pending_types.emplace(name, PendingType{}).first->second;
    };

    auto access_type_flags = [](AccessLevel access) -> uint32_t {
        switch (access) {
        case AccessLevel::Public:
            return static_cast<uint32_t>(TypeFlags::Public);
        case AccessLevel::Private:
        case AccessLevel::Fileprivate:
            return static_cast<uint32_t>(TypeFlags::Private);
        case AccessLevel::Internal:
        default:
            return 0;
        }
    };

    auto access_field_flags = [](AccessLevel access) -> uint32_t {
        switch (access) {
        case AccessLevel::Public:
            return static_cast<uint32_t>(FieldFlags::Public);
        case AccessLevel::Private:
        case AccessLevel::Fileprivate:
            return static_cast<uint32_t>(FieldFlags::Private);
        case AccessLevel::Internal:
        default:
            return 0;
        }
    };

    auto access_property_flags = [](AccessLevel access) -> uint32_t {
        switch (access) {
        case AccessLevel::Public:
            return static_cast<uint32_t>(PropertyFlags::Public);
        case AccessLevel::Private:
        case AccessLevel::Fileprivate:
            return static_cast<uint32_t>(PropertyFlags::Private);
        case AccessLevel::Internal:
        default:
            return 0;
        }
    };

    auto access_method_flags = [](AccessLevel) -> uint32_t {
        return 0;
    };

    for (const auto& stmt : program) {
        if (!stmt) {
            continue;
        }

        switch (stmt->kind) {
        case StmtKind::ClassDecl: {
            auto* class_decl = static_cast<ClassDeclStmt*>(stmt.get());
            auto& meta = ensure_pending_type(class_decl->name);
            meta.flags |= access_type_flags(class_decl->access_level) | static_cast<uint32_t>(TypeFlags::Class);
            if (class_decl->superclass_name.has_value()) {
                meta.base_type = class_decl->superclass_name.value();
            }
            for (const auto& proto : class_decl->protocol_conformances) {
                meta.interfaces.push_back(proto);
            }

            for (const auto& prop : class_decl->properties) {
                if (!prop) {
                    continue;
                }
                if (prop->is_computed) {
                    PendingProperty pending{};
                    pending.name = prop->name;
                    pending.type = prop->type_annotation;
                    pending.flags = access_property_flags(prop->access_level);
                    if (prop->is_static) {
                        pending.flags |= static_cast<uint32_t>(PropertyFlags::Static);
                    }
                    pending.has_getter = true;
                    pending.has_setter = prop->setter_body != nullptr;
                    pending.getter_name = "get:" + prop->name;
                    pending.setter_name = "set:" + prop->name;
                    meta.properties.push_back(std::move(pending));

                    PendingMethod getter_method{};
                    getter_method.name = "get:" + prop->name;
                    getter_method.flags = access_method_flags(prop->access_level);
                    if (prop->is_static) {
                        getter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                    }
                    getter_method.return_type = prop->type_annotation;
                    meta.methods.push_back(std::move(getter_method));

                    if (prop->setter_body) {
                        PendingMethod setter_method{};
                        setter_method.name = "set:" + prop->name;
                        setter_method.flags = access_method_flags(prop->access_level);
                        if (prop->is_static) {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                        }
                        setter_method.params = build_accessor_param_types(prop->type_annotation);
                        meta.methods.push_back(std::move(setter_method));
                    }
                } else {
                    PendingField pending{};
                    pending.name = prop->name;
                    pending.type = prop->type_annotation;
                    pending.flags = access_field_flags(prop->access_level);
                    if (prop->is_static) {
                        pending.flags |= static_cast<uint32_t>(FieldFlags::Static);
                    }
                    if (!prop->is_let) {
                        pending.flags |= static_cast<uint32_t>(FieldFlags::Mutable);
                    }
                    meta.fields.push_back(std::move(pending));
                }
            }

            for (const auto& method : class_decl->methods) {
                if (!method) {
                    continue;
                }
                PendingMethod pending{};
                pending.name = method->name;
                pending.flags = access_method_flags(method->access_level);
                if (method->is_static) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Static);
                }
                if (method->is_override) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Override);
                }
                pending.return_type = method->return_type;
                pending.params.reserve(method->params.size());
                for (const auto& param : method->params) {
                    pending.params.push_back(param.type);
                }
                meta.methods.push_back(std::move(pending));
            }

            if (class_decl->deinit_body) {
                PendingMethod pending{};
                pending.name = "deinit";
                pending.flags = static_cast<uint32_t>(MethodFlags::Virtual);
                meta.methods.push_back(std::move(pending));
            }
            break;
        }
        case StmtKind::StructDecl: {
            auto* struct_decl = static_cast<StructDeclStmt*>(stmt.get());
            auto& meta = ensure_pending_type(struct_decl->name);
            meta.flags |= access_type_flags(struct_decl->access_level) | static_cast<uint32_t>(TypeFlags::Struct);
            for (const auto& proto : struct_decl->protocol_conformances) {
                meta.interfaces.push_back(proto);
            }

            for (const auto& prop : struct_decl->properties) {
                if (!prop) {
                    continue;
                }
                if (prop->is_computed) {
                    PendingProperty pending{};
                    pending.name = prop->name;
                    pending.type = prop->type_annotation;
                    pending.flags = access_property_flags(prop->access_level);
                    if (prop->is_static) {
                        pending.flags |= static_cast<uint32_t>(PropertyFlags::Static);
                    }
                    pending.has_getter = true;
                    pending.has_setter = prop->setter_body != nullptr;
                    pending.getter_name = "get:" + prop->name;
                    pending.setter_name = "set:" + prop->name;
                    meta.properties.push_back(std::move(pending));

                    PendingMethod getter_method{};
                    getter_method.name = "get:" + prop->name;
                    getter_method.flags = access_method_flags(prop->access_level);
                    if (prop->is_static) {
                        getter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                    }
                    getter_method.return_type = prop->type_annotation;
                    meta.methods.push_back(std::move(getter_method));

                    if (prop->setter_body) {
                        PendingMethod setter_method{};
                        setter_method.name = "set:" + prop->name;
                        setter_method.flags = access_method_flags(prop->access_level);
                        if (prop->is_static) {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                        } else {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Mutating);
                        }
                        setter_method.params = build_accessor_param_types(prop->type_annotation);
                        meta.methods.push_back(std::move(setter_method));
                    }
                } else {
                    PendingField pending{};
                    pending.name = prop->name;
                    pending.type = prop->type_annotation;
                    pending.flags = access_field_flags(prop->access_level);
                    if (prop->is_static) {
                        pending.flags |= static_cast<uint32_t>(FieldFlags::Static);
                    }
                    if (!prop->is_let) {
                        pending.flags |= static_cast<uint32_t>(FieldFlags::Mutable);
                    }
                    meta.fields.push_back(std::move(pending));
                }
            }

            for (const auto& method : struct_decl->methods) {
                if (!method) {
                    continue;
                }
                if (method->is_computed_property) {
                    PendingProperty pending{};
                    pending.name = method->name;
                    pending.type = method->return_type;
                    pending.flags = access_property_flags(method->access_level);
                    if (method->is_static) {
                        pending.flags |= static_cast<uint32_t>(PropertyFlags::Static);
                    }
                    pending.has_getter = true;
                    pending.has_setter = method->is_mutating;
                    pending.getter_name = "get:" + method->name;
                    pending.setter_name = "set:" + method->name;
                    meta.properties.push_back(std::move(pending));

                    PendingMethod getter_method{};
                    getter_method.name = "get:" + method->name;
                    getter_method.flags = access_method_flags(method->access_level);
                    if (method->is_static) {
                        getter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                    }
                    getter_method.return_type = method->return_type;
                    meta.methods.push_back(std::move(getter_method));

                    if (method->is_mutating) {
                        PendingMethod setter_method{};
                        setter_method.name = "set:" + method->name;
                        setter_method.flags = access_method_flags(method->access_level);
                        if (method->is_static) {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                        } else {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Mutating);
                        }
                        setter_method.params = build_accessor_param_types(method->return_type);
                        meta.methods.push_back(std::move(setter_method));
                    }
                    continue;
                }
                PendingMethod pending{};
                pending.name = method->name;
                pending.flags = access_method_flags(method->access_level);
                if (method->is_static) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Static);
                }
                if (method->is_mutating) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Mutating);
                }
                pending.return_type = method->return_type;
                pending.params.reserve(method->params.size());
                for (const auto& param : method->params) {
                    pending.params.push_back(param.type);
                }
                meta.methods.push_back(std::move(pending));
            }

            for (const auto& init_method : struct_decl->initializers) {
                if (!init_method) {
                    continue;
                }
                PendingMethod pending{};
                pending.name = "init";
                pending.flags = static_cast<uint32_t>(MethodFlags::Mutating);
                TypeAnnotation return_type{};
                return_type.name = struct_decl->name;
                pending.return_type = return_type;
                pending.params.reserve(init_method->params.size());
                for (const auto& param : init_method->params) {
                    pending.params.push_back(param.type);
                }
                meta.methods.push_back(std::move(pending));
            }
            break;
        }
        case StmtKind::EnumDecl: {
            auto* enum_decl = static_cast<EnumDeclStmt*>(stmt.get());
            auto& meta = ensure_pending_type(enum_decl->name);
            meta.flags |= access_type_flags(enum_decl->access_level) | static_cast<uint32_t>(TypeFlags::Enum);
            for (const auto& method : enum_decl->methods) {
                if (!method) {
                    continue;
                }
                if (method->is_computed_property) {
                    PendingProperty pending{};
                    pending.name = method->name;
                    pending.type = method->return_type;
                    pending.flags = access_property_flags(method->access_level);
                    if (method->is_static) {
                        pending.flags |= static_cast<uint32_t>(PropertyFlags::Static);
                    }
                    pending.has_getter = true;
                    pending.has_setter = method->is_mutating;
                    pending.getter_name = "get:" + method->name;
                    pending.setter_name = "set:" + method->name;
                    meta.properties.push_back(std::move(pending));

                    PendingMethod getter_method{};
                    getter_method.name = "get:" + method->name;
                    getter_method.flags = access_method_flags(method->access_level);
                    if (method->is_static) {
                        getter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                    }
                    getter_method.return_type = method->return_type;
                    meta.methods.push_back(std::move(getter_method));

                    if (method->is_mutating) {
                        PendingMethod setter_method{};
                        setter_method.name = "set:" + method->name;
                        setter_method.flags = access_method_flags(method->access_level);
                        if (method->is_static) {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                        } else {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Mutating);
                        }
                        setter_method.params = build_accessor_param_types(method->return_type);
                        meta.methods.push_back(std::move(setter_method));
                    }
                    continue;
                }
                PendingMethod pending{};
                pending.name = method->name;
                pending.flags = access_method_flags(method->access_level);
                if (method->is_static) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Static);
                }
                if (method->is_mutating) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Mutating);
                }
                pending.return_type = method->return_type;
                pending.params.reserve(method->params.size());
                for (const auto& param : method->params) {
                    pending.params.push_back(param.type);
                }
                meta.methods.push_back(std::move(pending));
            }
            break;
        }
        case StmtKind::ProtocolDecl: {
            auto* proto_decl = static_cast<ProtocolDeclStmt*>(stmt.get());
            auto& meta = ensure_pending_type(proto_decl->name);
            meta.flags |= access_type_flags(proto_decl->access_level) | static_cast<uint32_t>(TypeFlags::Interface);
            for (const auto& proto : proto_decl->inherited_protocols) {
                meta.interfaces.push_back(proto);
            }
            for (const auto& requirement : proto_decl->method_requirements) {
                PendingMethod pending{};
                pending.name = requirement.name;
                pending.flags = static_cast<uint32_t>(MethodFlags::Virtual);
                if (requirement.is_mutating) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Mutating);
                }
                pending.return_type = requirement.return_type;
                pending.params.reserve(requirement.params.size());
                for (const auto& param : requirement.params) {
                    pending.params.push_back(param.type);
                }
                meta.methods.push_back(std::move(pending));
            }
            for (const auto& requirement : proto_decl->property_requirements) {
                PendingProperty pending{};
                pending.name = requirement.name;
                pending.type = requirement.type;
                pending.flags = static_cast<uint32_t>(PropertyFlags::Public);
                pending.has_getter = requirement.has_getter;
                pending.has_setter = requirement.has_setter;
                pending.getter_name = "get:" + requirement.name;
                pending.setter_name = "set:" + requirement.name;
                meta.properties.push_back(std::move(pending));

                if (requirement.has_getter) {
                    PendingMethod getter_method{};
                    getter_method.name = "get:" + requirement.name;
                    getter_method.flags = static_cast<uint32_t>(MethodFlags::Virtual);
                    getter_method.return_type = requirement.type;
                    meta.methods.push_back(std::move(getter_method));
                }

                if (requirement.has_setter) {
                    PendingMethod setter_method{};
                    setter_method.name = "set:" + requirement.name;
                    setter_method.flags = static_cast<uint32_t>(MethodFlags::Virtual);
                    setter_method.params = build_accessor_param_types(requirement.type);
                    meta.methods.push_back(std::move(setter_method));
                }
            }
            break;
        }
        case StmtKind::ExtensionDecl: {
            auto* ext_decl = static_cast<ExtensionDeclStmt*>(stmt.get());
            auto& meta = ensure_pending_type(ext_decl->extended_type);
            for (const auto& proto : ext_decl->protocol_conformances) {
                meta.interfaces.push_back(proto);
            }
            for (const auto& method : ext_decl->methods) {
                if (!method) {
                    continue;
                }
                if (method->is_computed_property) {
                    PendingProperty pending{};
                    pending.name = method->name;
                    pending.type = method->return_type;
                    pending.flags = access_property_flags(method->access_level);
                    if (method->is_static) {
                        pending.flags |= static_cast<uint32_t>(PropertyFlags::Static);
                    }
                    pending.has_getter = true;
                    pending.has_setter = method->is_mutating;
                    pending.getter_name = "$get_" + method->name;
                    pending.setter_name = "$set_" + method->name;
                    meta.properties.push_back(std::move(pending));

                    PendingMethod getter_method{};
                    getter_method.name = "$get_" + method->name;
                    getter_method.flags = access_method_flags(method->access_level);
                    if (method->is_static) {
                        getter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                    }
                    getter_method.return_type = method->return_type;
                    meta.methods.push_back(std::move(getter_method));

                    if (method->is_mutating) {
                        PendingMethod setter_method{};
                        setter_method.name = "$set_" + method->name;
                        setter_method.flags = access_method_flags(method->access_level);
                        if (method->is_static) {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Static);
                        } else {
                            setter_method.flags |= static_cast<uint32_t>(MethodFlags::Mutating);
                        }
                        setter_method.params = build_accessor_param_types(method->return_type);
                        meta.methods.push_back(std::move(setter_method));
                    }
                    continue;
                }
                PendingMethod pending{};
                pending.name = method->name;
                pending.flags = access_method_flags(method->access_level);
                if (method->is_static) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Static);
                }
                if (method->is_mutating) {
                    pending.flags |= static_cast<uint32_t>(MethodFlags::Mutating);
                }
                pending.return_type = method->return_type;
                pending.params.reserve(method->params.size());
                for (const auto& param : method->params) {
                    pending.params.push_back(param.type);
                }
                meta.methods.push_back(std::move(pending));
            }
            break;
        }
        default:
            break;
        }
    }

    std::unordered_map<std::string, type_idx> type_indices;

    auto ensure_type_def = [&](const std::string& name, uint32_t flags) -> type_idx {
        auto it = type_indices.find(name);
        if (it != type_indices.end()) {
            auto& def = chunk_.type_definitions[it->second];
            def.flags |= flags;
            return it->second;
        }
        TypeDef def{};
        def.name = static_cast<string_idx>(chunk_.add_string(name));
        def.flags = flags;
        chunk_.type_definitions.push_back(std::move(def));
        type_idx idx = static_cast<type_idx>(chunk_.type_definitions.size() - 1);
        type_indices.emplace(name, idx);
        return idx;
    };

    auto resolve_type_annotation = [&](const std::optional<TypeAnnotation>& annotation) -> type_idx {
        if (!annotation.has_value()) {
            return 0;
        }
        return ensure_type_def(annotation->name, 0);
    };

    auto resolve_type_annotation_value = [&](const TypeAnnotation& annotation) -> type_idx {
        if (annotation.name.empty()) {
            return 0;
        }
        return ensure_type_def(annotation.name, 0);
    };

    auto append_signature = [&](const std::vector<TypeAnnotation>& params,
                                const std::optional<TypeAnnotation>& return_type) -> signature_idx {
        signature_idx offset = static_cast<signature_idx>(chunk_.signature_blob.size());
        auto append_u32 = [&](uint32_t value) {
            chunk_.signature_blob.push_back(static_cast<uint8_t>(value & 0xFF));
            chunk_.signature_blob.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            chunk_.signature_blob.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
            chunk_.signature_blob.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        };
        append_u32(static_cast<uint32_t>(params.size()));
        append_u32(static_cast<uint32_t>(resolve_type_annotation(return_type)));
        for (const auto& param : params) {
            append_u32(static_cast<uint32_t>(resolve_type_annotation_value(param)));
        }
        return offset;
    };

    constexpr body_idx kInvalidBody = std::numeric_limits<body_idx>::max();
    constexpr method_idx kInvalidMethod = std::numeric_limits<method_idx>::max();

    for (const auto& type_name : type_order) {
        auto meta_it = pending_types.find(type_name);
        if (meta_it == pending_types.end()) {
            continue;
        }
        auto& meta = meta_it->second;
        type_idx idx = ensure_type_def(type_name, meta.flags);
        TypeDef& def = chunk_.type_definitions[idx];
        def.flags |= meta.flags;

        if (meta.base_type.has_value()) {
            def.base_type = ensure_type_def(meta.base_type.value(), 0);
        }

        def.interfaces.clear();
        def.interfaces.reserve(meta.interfaces.size());
        for (const auto& iface : meta.interfaces) {
            def.interfaces.push_back(ensure_type_def(iface, 0));
        }

        size_t field_start = chunk_.field_definitions.size();
        for (const auto& field : meta.fields) {
            FieldDef def_field{};
            def_field.name = static_cast<string_idx>(chunk_.add_string(field.name));
            def_field.flags = field.flags;
            def_field.type = resolve_type_annotation(field.type);
            chunk_.field_definitions.push_back(std::move(def_field));
        }
        def.field_list.start = static_cast<uint32_t>(field_start);
        def.field_list.count = static_cast<uint32_t>(chunk_.field_definitions.size() - field_start);

        size_t method_start = chunk_.method_definitions.size();
        std::unordered_map<std::string, method_idx> method_lookup;
        for (const auto& method : meta.methods) {
            MethodDef def_method{};
            def_method.name = static_cast<string_idx>(chunk_.add_string(method.name));
            def_method.flags = method.flags;
            def_method.signature = append_signature(method.params, method.return_type);
            bool is_static = (method.flags & static_cast<uint32_t>(MethodFlags::Static)) != 0;
            std::string method_key = build_method_key(type_name, method.name, is_static, method.params);
            auto body_it = method_body_lookup_.find(method_key);
            def_method.body_ptr = body_it != method_body_lookup_.end() ? body_it->second.body : kInvalidBody;
            chunk_.method_definitions.push_back(std::move(def_method));
            method_lookup.emplace(method_key, static_cast<method_idx>(chunk_.method_definitions.size() - 1));
        }
        def.method_list.start = static_cast<uint32_t>(method_start);
        def.method_list.count = static_cast<uint32_t>(chunk_.method_definitions.size() - method_start);

        size_t property_start = chunk_.property_definitions.size();
        for (const auto& prop : meta.properties) {
            PropertyDef def_prop{};
            def_prop.name = static_cast<string_idx>(chunk_.add_string(prop.name));
            def_prop.flags = prop.flags;
            def_prop.type = resolve_type_annotation(prop.type);
            def_prop.getter = kInvalidMethod;
            def_prop.setter = kInvalidMethod;
            bool is_static = (prop.flags & static_cast<uint32_t>(PropertyFlags::Static)) != 0;
            if (prop.has_getter && !prop.getter_name.empty()) {
                std::vector<TypeAnnotation> getter_params;
                std::string getter_key = build_method_key(type_name, prop.getter_name, is_static, getter_params);
                auto it = method_lookup.find(getter_key);
                if (it != method_lookup.end()) {
                    def_prop.getter = it->second;
                }
            }
            if (prop.has_setter && !prop.setter_name.empty()) {
                std::vector<TypeAnnotation> setter_params = build_accessor_param_types(prop.type);
                std::string setter_key = build_method_key(type_name, prop.setter_name, is_static, setter_params);
                auto it = method_lookup.find(setter_key);
                if (it != method_lookup.end()) {
                    def_prop.setter = it->second;
                }
            }
            chunk_.property_definitions.push_back(std::move(def_prop));
        }
        def.property_list.start = static_cast<uint32_t>(property_start);
        def.property_list.count = static_cast<uint32_t>(chunk_.property_definitions.size() - property_start);
    }
}

// ============================================================================
// Generic Specialization Implementation
// ============================================================================


std::string Compiler::mangle_generic_name(const std::string& base_name, 
                                          const std::vector<TypeAnnotation>& type_args) {
    std::string mangled = base_name;
    for (const auto& arg : type_args) {
        mangled += "_";
        
        // Recursively handle nested generics: Array<Box<Int>>
        if (!arg.generic_args.empty()) {
            // Nested generic: Box<Int> becomes Box_Int
            mangled += mangle_generic_name(arg.name, arg.generic_args);
        } else {
            // Simple type: Int, String, etc.
            mangled += arg.name;
        }
    }
    return mangled;
}


void Compiler::collect_generic_templates(const std::vector<StmtPtr>& program) {
    generic_struct_templates_.clear();
    
    for (const auto& stmt : program) {
        if (!stmt) continue;
        
        if (stmt->kind == StmtKind::StructDecl) {
            auto* struct_decl = static_cast<StructDeclStmt*>(stmt.get());
            if (!struct_decl->generic_params.empty()) {
                generic_struct_templates_[struct_decl->name] = struct_decl;
            }
        }
    }
}

void Compiler::collect_generic_usages(const std::vector<StmtPtr>& program, 
                                      std::unordered_set<std::string>& needed_specializations) {
    // Helper to recursively collect generic types from TypeAnnotation
    std::function<void(const TypeAnnotation&)> collect_from_type = [&](const TypeAnnotation& type) {
        if (!type.generic_args.empty()) {
            // This type has generic arguments, so it needs specialization
            std::string mangled = mangle_generic_name(type.name, type.generic_args);
            needed_specializations.insert(mangled);
            
            // Recursively collect from nested generic args
            for (const auto& arg : type.generic_args) {
                collect_from_type(arg);
            }
        }
    };
    
    // Scan variable declarations
    for (const auto& stmt : program) {
        if (!stmt) continue;
        
        if (stmt->kind == StmtKind::VarDecl) {
            auto* var_decl = static_cast<VarDeclStmt*>(stmt.get());
            
            // Check type annotation
            if (var_decl->type_annotation.has_value()) {
                collect_from_type(var_decl->type_annotation.value());
            }
            
            // Check initializer
            if (var_decl->initializer && var_decl->initializer->kind == ExprKind::Call) {
                auto* call = static_cast<CallExpr*>(var_decl->initializer.get());
                if (call->callee && call->callee->kind == ExprKind::Identifier) {
                    auto* id = static_cast<IdentifierExpr*>(call->callee.get());
                    if (!id->generic_args.empty()) {
                        std::string mangled = mangle_generic_name(id->name, id->generic_args);
                        needed_specializations.insert(mangled);
                        
                        // Collect nested generics
                        for (const auto& arg : id->generic_args) {
                            collect_from_type(arg);
                        }
                    }
                }
            }
        }
    }
}

StmtPtr Compiler::create_specialized_struct(const StructDeclStmt* template_decl, 
                                   const std::vector<TypeAnnotation>& type_args) {
if (template_decl->generic_params.size() != type_args.size()) {
    throw CompilerError("Generic parameter count mismatch", template_decl->line);
}
    
// Validate generic constraints
// Create type substitution map for validation
std::unordered_map<std::string, std::string> param_to_type;
for (size_t i = 0; i < template_decl->generic_params.size(); ++i) {
    param_to_type[template_decl->generic_params[i]] = type_args[i].name;
}
    
// Check each constraint
for (const auto& constraint : template_decl->generic_constraints) {
    // Find the actual type for this constraint
    auto it = param_to_type.find(constraint.param_name);
    if (it == param_to_type.end()) {
        continue; // Should not happen if parser is correct
    }
        
    std::string actual_type = it->second;
    std::string required_protocol = constraint.protocol_name;
        
    // Note: For now, we'll skip runtime validation and assume correctness
    // Full implementation would check if actual_type conforms to required_protocol
    // This would require access to the type system which is in TypeChecker
}
    
// Create type substitution map: T -> Box<Int> (with full TypeAnnotation)
std::unordered_map<std::string, TypeAnnotation> type_substitution_full;
for (size_t i = 0; i < template_decl->generic_params.size(); ++i) {
    type_substitution_full[template_decl->generic_params[i]] = type_args[i];
}
    
// Helper to substitute type (simplified - no recursion for now)
auto substitute_type = [&](const TypeAnnotation& original) -> TypeAnnotation {
    TypeAnnotation result = original;
        
    // Check if this is a generic parameter that needs substitution
    auto it = type_substitution_full.find(original.name);
    if (it != type_substitution_full.end()) {
        // Replace with the specialized type
        result = it->second;
            
        // If the specialized type has generic args, mangle it
        if (!result.generic_args.empty()) {
            result.name = mangle_generic_name(result.name, result.generic_args);
            result.generic_args.clear(); // Clear after mangling
        }
    } else if (!original.generic_args.empty()) {
        // Not a generic parameter, but has generic args - mangle it
        result.name = mangle_generic_name(original.name, original.generic_args);
        result.generic_args.clear();
    }
        
    return result;
};
    
    
    
    // Create specialized struct
    auto specialized = std::make_unique<StructDeclStmt>();
    specialized->line = template_decl->line;
    specialized->name = mangle_generic_name(template_decl->name, type_args);
    specialized->generic_params.clear();
    specialized->protocol_conformances = template_decl->protocol_conformances;
    
    // Copy properties with type substitution
    for (const auto& prop : template_decl->properties) {
        auto new_prop = std::make_unique<VarDeclStmt>();
        new_prop->line = prop->line;
        new_prop->name = prop->name;
        new_prop->is_let = prop->is_let;
        new_prop->is_static = prop->is_static;
        new_prop->is_lazy = prop->is_lazy;
        new_prop->access_level = prop->access_level;
        
        if (prop->type_annotation.has_value()) {
            new_prop->type_annotation = substitute_type(prop->type_annotation.value());
        }
        
        // Copy initializer (shallow copy for simplicity)
        if (prop->initializer) {
            if (prop->initializer->kind == ExprKind::Literal) {
                auto* lit = static_cast<LiteralExpr*>(prop->initializer.get());
                new_prop->initializer = std::make_unique<LiteralExpr>(lit->value);
                new_prop->initializer->line = lit->line;
            }
        }
        
        specialized->properties.push_back(std::move(new_prop));
    }
    
    // Copy methods from template - share the unique_ptr (transfer ownership)
    // Note: This means the template's methods vector will be emptied!
    // But since we skip compiling the template, this is OK
    for (const auto& method : template_decl->methods) {
        auto new_method = std::make_unique<StructMethodDecl>();
        new_method->name = method->name;
        new_method->is_mutating = method->is_mutating;
        new_method->is_computed_property = method->is_computed_property;
        new_method->is_static = method->is_static;
        new_method->access_level = method->access_level;
        
        // Copy parameters with type substitution
        for (const auto& param : method->params) {
            ParamDecl new_param;
            new_param.external_name = param.external_name;
            new_param.internal_name = param.internal_name;
            new_param.type = substitute_type(param.type);
            new_method->params.push_back(std::move(new_param));
        }
        
        // Copy return type with substitution
        if (method->return_type.has_value()) {
            new_method->return_type = substitute_type(method->return_type.value());
        }
        
        // Deep copy method body
        if (method->body) {
            new_method->body = std::unique_ptr<BlockStmt>(
                static_cast<BlockStmt*>(clone_stmt(method->body.get()).release()));
        }
        
        specialized->methods.push_back(std::move(new_method));
    }
    
    return specialized;
}

std::vector<StmtPtr> Compiler::specialize_generics(const std::vector<StmtPtr>& program) {
// Step 1: Collect generic templates
collect_generic_templates(program);
    
std::cout << "[DEBUG] Templates collected: " << generic_struct_templates_.size() << std::endl;
for (const auto& [name, _] : generic_struct_templates_) {
    std::cout << "  - " << name << std::endl;
}
    
// Step 2: Collect generic usages
std::unordered_set<std::string> needed_specializations;
collect_generic_usages(program, needed_specializations);
    
std::cout << "[DEBUG] Specializations needed: " << needed_specializations.size() << std::endl;
for (const auto& name : needed_specializations) {
    std::cout << "  - " << name << std::endl;
}
    
// Step 3: Create result vector
std::vector<StmtPtr> result;
result.reserve(program.size() + needed_specializations.size());
    
    // Step 4: Insert specialized structs right after their template
    for (size_t i = 0; i < program.size(); ++i) {
        const Stmt* stmt_ptr = program[i].get();
        
        // Move original statement
        result.push_back(std::move(const_cast<StmtPtr&>(program[i])));
        
        // If this is a generic struct template, add specializations
        if (stmt_ptr && stmt_ptr->kind == StmtKind::StructDecl) {
            auto* struct_decl = static_cast<const StructDeclStmt*>(stmt_ptr);
            if (!struct_decl->generic_params.empty()) {
                std::string base_name = struct_decl->name;
                
                // Find all specializations for this template
                for (const auto& mangled_name : needed_specializations) {
                    size_t first_underscore = mangled_name.find('_');
                    if (first_underscore == std::string::npos) continue;
                    
                    std::string spec_base_name = mangled_name.substr(0, first_underscore);
                    if (spec_base_name != base_name) continue;
                    
                    std::string remaining = mangled_name.substr(first_underscore + 1);
                    
                    // Find template
                    auto template_it = generic_struct_templates_.find(base_name);
                    if (template_it == generic_struct_templates_.end()) continue;
                    
                    // Parse type arguments from mangled name
                    // For nested generics like Container_Box_Int, we need to recognize
                    // that Box_Int is a single type argument (Box<Int>)
                    std::vector<TypeAnnotation> type_args;
                    size_t num_params = template_it->second->generic_params.size();

                    // Helper function to parse type args considering nested generics
                    std::function<TypeAnnotation(const std::string&)> parse_mangled_type;
                    parse_mangled_type = [&](const std::string& mangled) -> TypeAnnotation {
                        TypeAnnotation result;
                        result.is_optional = false;
                        result.is_function_type = false;

                        // Check if this is a known generic template
                        size_t first_underscore = mangled.find('_');
                        if (first_underscore != std::string::npos) {
                            std::string potential_template = mangled.substr(0, first_underscore);
                            if (generic_struct_templates_.find(potential_template) != generic_struct_templates_.end()) {
                                // This is a nested generic like Box_Int
                                result.name = mangled; // Use the mangled name directly
                                return result;
                            }
                        }

                        // Simple type
                        result.name = mangled;
                        return result;
                    };

                    // Helper function to consume one type argument from mangled string
                    // Returns the consumed type and updates the remaining string
                    std::function<TypeAnnotation(std::string&)> consume_one_type_arg;
                    consume_one_type_arg = [&](std::string& str) -> TypeAnnotation {
                        TypeAnnotation result;
                        result.is_optional = false;
                        result.is_function_type = false;

                        // Check if str starts with a known generic template
                        for (const auto& [template_name, template_decl] : generic_struct_templates_) {
                            if (str.rfind(template_name + "_", 0) == 0) {
                                // Found a nested generic like Box_...
                                size_t nested_params = template_decl->generic_params.size();

                                // We need to consume template_name + "_" + nested_params types
                                std::string nested_mangled = template_name;
                                str = str.substr(template_name.length() + 1); // skip "Box_"

                                // Recursively consume nested type arguments
                                for (size_t i = 0; i < nested_params; ++i) {
                                    TypeAnnotation nested_arg = consume_one_type_arg(str);
                                    nested_mangled += "_" + nested_arg.name;
                                }

                                result.name = nested_mangled;
                                return result;
                            }
                        }

                        // Simple type: consume until next underscore or end
                        size_t next_underscore = str.find('_');
                        if (next_underscore == std::string::npos) {
                            result.name = str;
                            str = "";
                        } else {
                            result.name = str.substr(0, next_underscore);
                            str = str.substr(next_underscore + 1);
                        }
                        return result;
                    };

                    // Parse remaining string into type_args
                    std::string remaining_copy = remaining;
                    while (!remaining_copy.empty() && type_args.size() < num_params) {
                        type_args.push_back(consume_one_type_arg(remaining_copy));
                    }

                    // Verify parameter count
                    if (type_args.size() != num_params) {
                        continue;
                    }
                    
                    // Create specialized struct
                    try {
                        StmtPtr specialized = create_specialized_struct(template_it->second, type_args);
                        auto* s = static_cast<StructDeclStmt*>(specialized.get());
                        std::cout << "[DEBUG] Created specialized struct: " << s->name << std::endl;
                        result.push_back(std::move(specialized));
                    } catch (const std::exception& e) {
                        std::cout << "[DEBUG] Failed to specialize " << mangled_name << ": " << e.what() << std::endl;
                    }
                }
            }
        }
    }
    
    return result;
}

void Compiler::record_entry_main_global(const FuncDeclStmt* stmt) {
    if (entry_main_.kind != EntryMainInfo::Kind::None) {
        throw CompilerError("Multiple entry main() found", stmt->line);
    }
    entry_main_.kind = EntryMainInfo::Kind::GlobalFunc;
    entry_main_.line = stmt->line;
}

void Compiler::record_entry_main_static(const std::string& type_name, int line) {
    if (entry_main_.kind != EntryMainInfo::Kind::None) {
        throw CompilerError("Multiple entry main() found", line);
    }
    entry_main_.kind = EntryMainInfo::Kind::StaticMethod;
    entry_main_.type_name = type_name;
    entry_main_.line = line;
}

void Compiler::emit_auto_entry_main_call() {
    if (entry_main_.kind == EntryMainInfo::Kind::None) return;

    if (entry_main_.kind == EntryMainInfo::Kind::GlobalFunc) {
        // GET_GLOBAL "main" -> CALL 0 -> POP
        emit_variable_get("main", entry_main_.line);
        emit_op(OpCode::OP_CALL, entry_main_.line);
        emit_short(0, entry_main_.line);
        emit_op(OpCode::OP_POP, entry_main_.line);
        return;
    }

    if (entry_main_.kind == EntryMainInfo::Kind::StaticMethod) {
        // GET_GLOBAL TypeName -> GET_PROPERTY "main" -> CALL 0 -> POP
        emit_variable_get(entry_main_.type_name, entry_main_.line);

        size_t name_idx = identifier_constant("main");
        if (name_idx > std::numeric_limits<uint16_t>::max()) {
            throw CompilerError("Too many identifiers", entry_main_.line);
        }
        emit_op(OpCode::OP_GET_PROPERTY, entry_main_.line);
        emit_short(static_cast<uint16_t>(name_idx), entry_main_.line);

        emit_op(OpCode::OP_CALL, entry_main_.line);
        emit_short(0, entry_main_.line);
        emit_op(OpCode::OP_POP, entry_main_.line);
        return;
    }
}

} // namespace swiftscript
