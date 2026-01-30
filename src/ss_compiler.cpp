#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_type_checker.hpp"
#include <limits>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <sstream>

namespace swiftscript {

Chunk Compiler::compile(const std::vector<StmtPtr>& program) {
    TypeChecker checker;
    checker.check(program);

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
            getter_proto.chunk = std::make_shared<Chunk>();
            
            Compiler getter_compiler;
            getter_compiler.chunk_ = Chunk{};
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
            
            getter_proto.chunk = std::make_shared<Chunk>(std::move(getter_compiler.chunk_));
            size_t getter_idx = chunk_.add_function(std::move(getter_proto));
            
            // Compile setter (if present)
            size_t setter_idx = 0xFFFF;
            if (property->setter_body) {
                FunctionPrototype setter_proto;
                setter_proto.name = "set:" + property->name;
                setter_proto.params.push_back("self");
                setter_proto.params.push_back("newValue");
                setter_proto.chunk = std::make_shared<Chunk>();
                
                Compiler setter_compiler;
                setter_compiler.chunk_ = Chunk{};
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
                
                setter_proto.chunk = std::make_shared<Chunk>(std::move(setter_compiler.chunk_));
                setter_idx = chunk_.add_function(std::move(setter_proto));
            }
            
            // Emit computed property definition
            emit_op(OpCode::OP_DEFINE_COMPUTED_PROPERTY, property->line);
            emit_short(static_cast<uint16_t>(property_name_idx), property->line);
            emit_short(static_cast<uint16_t>(getter_idx), property->line);
            emit_short(static_cast<uint16_t>(setter_idx), property->line);
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
            // flags: bit 0 = is_let, bit 1 = is_static
            uint8_t flags = (property->is_let ? 0x1 : 0x0) | (property->is_static ? 0x2 : 0x0);
            emit_byte(flags, property->line);
        }
    }

    // Methods: class object remains on stack top
    for (const auto& method : stmt->methods) {
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
            proto.params.push_back("self");
        } else {
            proto.params.reserve(method->params.size());
        }
        for (const auto& [param_name, param_type] : method->params) {
            proto.params.push_back(param_name);
        }
        proto.is_initializer = (method->name == "init");
        proto.is_override = method->is_override;

        Compiler method_compiler;
        method_compiler.enclosing_ = this;
        method_compiler.chunk_ = Chunk{};
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

    // Deinit: compile as special method
    if (stmt->deinit_body) {
        FunctionPrototype proto;
        proto.name = "deinit";
        proto.params.push_back("self");
        proto.is_initializer = false;
        proto.is_override = false;

        Compiler deinit_compiler;
        deinit_compiler.enclosing_ = this;
        deinit_compiler.chunk_ = Chunk{};
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

        proto.chunk = std::make_shared<Chunk>(std::move(deinit_compiler.chunk_));
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
        if (method->is_static) {
            // Static method: no 'self' parameter
            FunctionPrototype proto;
            proto.name = method->name;
            proto.params.reserve(method->params.size());
            for (const auto& [param_name, param_type] : method->params) {
                proto.params.push_back(param_name);
            }
            proto.is_initializer = false;
            proto.is_override = false;

            Compiler method_compiler;
            method_compiler.enclosing_ = this;
            method_compiler.chunk_ = Chunk{};
            method_compiler.locals_.clear();
            method_compiler.scope_depth_ = 1;
            method_compiler.recursion_depth_ = 0;

            // No 'self' for static methods
            for (const auto& [param_name, param_type] : method->params) {
                method_compiler.declare_local(param_name, param_type.is_optional);
                method_compiler.mark_local_initialized();
            }

            if (method->body) {
                for (const auto& statement : method->body->statements) {
                    method_compiler.compile_stmt(statement.get());
                }
            }

            method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
            method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

            proto.chunk = std::make_shared<Chunk>(std::move(method_compiler.chunk_));
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
        proto.params.push_back("self");  // Implicit self parameter
        for (const auto& [param_name, param_type] : method->params) {
            proto.params.push_back(param_name);
        }
        proto.is_initializer = false;
        proto.is_override = false;

        Compiler method_compiler;
        method_compiler.enclosing_ = this;
        method_compiler.chunk_ = Chunk{};
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

        for (const auto& [param_name, param_type] : method->params) {
            method_compiler.declare_local(param_name, param_type.is_optional);
            method_compiler.mark_local_initialized();
        }

        if (method->body) {
            for (const auto& statement : method->body->statements) {
                method_compiler.compile_stmt(statement.get());
            }
        }

        method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
        method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

        proto.chunk = std::make_shared<Chunk>(std::move(method_compiler.chunk_));
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
        proto.params.push_back("self");
        for (const auto& [param_name, param_type] : init_method->params) {
            proto.params.push_back(param_name);
        }
        proto.is_initializer = true;
        proto.is_override = false;

        Compiler init_compiler;
        init_compiler.enclosing_ = this;
        init_compiler.chunk_ = Chunk{};
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

        for (const auto& [param_name, param_type] : init_method->params) {
            init_compiler.declare_local(param_name, param_type.is_optional);
            init_compiler.mark_local_initialized();
        }

        if (init_method->body) {
            for (const auto& statement : init_method->body->statements) {
                init_compiler.compile_stmt(statement.get());
            }
        }

        init_compiler.emit_op(OpCode::OP_NIL, init_method->line);
        init_compiler.emit_op(OpCode::OP_RETURN, init_method->line);

        proto.chunk = std::make_shared<Chunk>(std::move(init_compiler.chunk_));
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
        
        Compiler getter_compiler;
        getter_compiler.enclosing_ = this;
        getter_compiler.chunk_ = Chunk{};
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
        
        getter_proto.chunk = std::make_shared<Chunk>(std::move(getter_compiler.chunk_));
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
        proto.params.push_back("self");  // Implicit self parameter
        for (const auto& [param_name, param_type] : method->params) {
            proto.params.push_back(param_name);
        }
        proto.is_initializer = false;
        proto.is_override = false;

        Compiler method_compiler;
        method_compiler.enclosing_ = this;
        method_compiler.chunk_ = Chunk{};
        method_compiler.locals_.clear();
        method_compiler.scope_depth_ = 1;
        method_compiler.recursion_depth_ = 0;
        method_compiler.current_class_properties_ = &property_lookup;
        method_compiler.allow_implicit_self_property_ = true;

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

        method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
        method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

        proto.chunk = std::make_shared<Chunk>(std::move(method_compiler.chunk_));
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

void Compiler::visit(RepeatWhileStmt* stmt) {
    // repeat-while: body executes at least once, then checks condition
    loop_stack_.push_back({});
    size_t loop_start = chunk_.code.size();
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

void Compiler::visit(ImportStmt* stmt) {
    // Check for circular dependency
    if (compiling_modules_.find(stmt->module_path) != compiling_modules_.end()) {
        throw CompilerError("Circular import detected: " + stmt->module_path, stmt->line);
    }
    
    // Check if already imported
    if (imported_modules_.find(stmt->module_path) != imported_modules_.end()) {
        // Module already imported, skip
        return;
    }
    
    // Mark as imported
    imported_modules_.insert(stmt->module_path);
    compiling_modules_.insert(stmt->module_path);
    
    try {
        // Load and parse the imported module
        std::string full_path = stmt->module_path;
        if (!base_directory_.empty() && stmt->module_path[0] != '/' && stmt->module_path[0] != '\\') {
            // Resolve relative path
            full_path = base_directory_ + "/" + stmt->module_path;
        }
        
        // Read the file content
        std::ifstream file(full_path);
        if (!file.is_open()) {
            throw CompilerError("Cannot open import file: " + full_path, stmt->line);
        }
        
        std::string source((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();
        
        // Tokenize and parse the imported module
        Lexer lexer(source);
        auto tokens = lexer.tokenize_all();
        Parser parser(std::move(tokens));
        auto imported_program = parser.parse();
        
        // Compile the imported module statements into current chunk
        for (const auto& imported_stmt : imported_program) {
            if (imported_stmt) {
                compile_stmt(imported_stmt.get());
            }
        }
        
        // Remove from compiling set
        compiling_modules_.erase(stmt->module_path);
        
    } catch (const std::exception& e) {
        compiling_modules_.erase(stmt->module_path);
        throw CompilerError("Error importing module '" + stmt->module_path + "': " + e.what(), stmt->line);
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
        for (const auto& [param_name, param_type] : method_req.params) {
            req.param_names.push_back(param_name);
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
            
            getter_proto.chunk = std::make_shared<Chunk>(std::move(method_compiler.chunk_));
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
            } else {
                method_compiler.allow_implicit_self_property_ = false;
            }

            // Add method parameters
            for (const auto& [param_name, param_type] : method->params) {
                method_compiler.declare_local(param_name, param_type.is_optional);
                method_compiler.mark_local_initialized();
                func_proto.params.push_back(param_name);
            }

            // Compile body
            for (const auto& body_stmt : method->body->statements) {
                method_compiler.compile_stmt(body_stmt.get());
            }

            // Implicit return nil if no explicit return
            method_compiler.emit_op(OpCode::OP_NIL, stmt->line);
            method_compiler.emit_op(OpCode::OP_RETURN, stmt->line);

            func_proto.chunk = std::make_shared<Chunk>(std::move(method_compiler.chunk_));
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

    if (is_implicit_property(expr->name)) {
        emit_self_property_get(expr->name, expr->line);
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
    // Special case: member assignment (obj.prop = value)
    if (expr->op == TokenType::Equal && expr->left->kind == ExprKind::Member) {
        auto* member = static_cast<MemberExpr*>(expr->left.get());
        
        // Compile object
        compile_expr(member->object.get());
        
        // Compile value
        compile_expr(expr->right.get());
        
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
    } else {
        // 일반 할당
        if (is_property) {
            size_t name_idx = identifier_constant(expr->name);
            emit_load_self(expr->line);
            compile_expr(expr->value.get());
            emit_op(OpCode::OP_SET_PROPERTY, expr->line);
            emit_short(static_cast<uint16_t>(name_idx), expr->line);
            return;
        }
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

void Compiler::visit(TypeCastExpr* expr) {
    // Compile the value expression
    compile_expr(expr->value.get());
    
    // Store the target type name
    size_t type_name_idx = identifier_constant(expr->target_type.name);
    if (type_name_idx > std::numeric_limits<uint16_t>::max()) {
        throw CompilerError("Type name index out of range", expr->line);
    }
    
    // Emit appropriate opcode
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

Chunk Compiler::compile_struct_method_body(const StructMethodDecl& method, bool is_mutating) {
    Compiler method_compiler;
    method_compiler.chunk_ = Chunk{};
    method_compiler.locals_.clear();
    method_compiler.scope_depth_ = 1;
    method_compiler.recursion_depth_ = 0;
    method_compiler.in_struct_method_ = true;
    method_compiler.in_mutating_method_ = is_mutating;

    // self is implicit first parameter
    method_compiler.declare_local("self", false);
    method_compiler.mark_local_initialized();

    for (const auto& [param_name, param_type] : method.params) {
        method_compiler.declare_local(param_name, param_type.is_optional);
        method_compiler.mark_local_initialized();
    }

    if (method.body) {
        for (const auto& statement : method.body->statements) {
            method_compiler.compile_stmt(statement.get());
        }
    }

    method_compiler.emit_op(OpCode::OP_NIL, 0);
    method_compiler.emit_op(OpCode::OP_RETURN, 0);
    return std::move(method_compiler.chunk_);
}

} // namespace swiftscript
