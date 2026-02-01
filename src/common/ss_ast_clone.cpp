#include "pch.h"
#include "ss_ast.hpp"

namespace swiftscript {

    // ============================================================================
    // Expression Cloning
    // ============================================================================

    ExprPtr clone_expr(const Expr* expr) {
        if (!expr) return nullptr;

        switch (expr->kind) {
        case ExprKind::Literal: {
            const auto* lit = static_cast<const LiteralExpr*>(expr);
            auto copy = std::make_unique<LiteralExpr>();
            copy->line = lit->line;
            copy->value = lit->value;
            copy->string_value = lit->string_value;
            return copy;
        }

        case ExprKind::Identifier: {
            const auto* id = static_cast<const IdentifierExpr*>(expr);
            auto copy = std::make_unique<IdentifierExpr>(id->name);
            copy->line = id->line;
            copy->generic_args = id->generic_args;
            return copy;
        }

        case ExprKind::Unary: {
            const auto* unary = static_cast<const UnaryExpr*>(expr);
            auto copy = std::make_unique<UnaryExpr>();
            copy->line = unary->line;
            copy->op = unary->op;
            if (unary->operand) copy->operand = clone_expr(unary->operand.get());
            return copy;
        }

        case ExprKind::Binary: {
            const auto* binary = static_cast<const BinaryExpr*>(expr);
            auto copy = std::make_unique<BinaryExpr>();
            copy->line = binary->line;
            copy->op = binary->op;
            if (binary->left) copy->left = clone_expr(binary->left.get());
            if (binary->right) copy->right = clone_expr(binary->right.get());
            return copy;
        }

        case ExprKind::Call: {
            const auto* call = static_cast<const CallExpr*>(expr);
            auto copy = std::make_unique<CallExpr>();
            copy->line = call->line;
            if (call->callee) copy->callee = clone_expr(call->callee.get());
            for (const auto& arg : call->arguments) {
                if (arg) copy->arguments.push_back(clone_expr(arg.get()));
            }
            copy->argument_names = call->argument_names;
            return copy;
        }

        case ExprKind::Member: {
            const auto* member = static_cast<const MemberExpr*>(expr);
            auto copy = std::make_unique<MemberExpr>();
            copy->line = member->line;
            if (member->object) copy->object = clone_expr(member->object.get());
            copy->member = member->member;
            return copy;
        }

                             // Add more cases as needed
        default:
            // For unsupported expression types, return null
            // This will cause compilation to fail, which is better than silent bugs
            return nullptr;
        }
    }

    std::vector<Attribute> clone_attributes(const std::vector<Attribute>& attributes) {
        std::vector<Attribute> copy;
        copy.reserve(attributes.size());
        for (const auto& attribute : attributes) {
            Attribute attr_copy;
            attr_copy.name = attribute.name;
            attr_copy.line = attribute.line;
            for (const auto& arg : attribute.arguments) {
                if (arg) {
                    attr_copy.arguments.push_back(clone_expr(arg.get()));
                }
            }
            copy.push_back(std::move(attr_copy));
        }
        return copy;
    }

    // ============================================================================
    // Statement Cloning
    // ============================================================================

    StmtPtr clone_stmt(const Stmt* stmt);

    StmtPtr clone_block_stmt(const BlockStmt* block) {
        auto copy = std::make_unique<BlockStmt>();
        copy->line = block->line;
        for (const auto& stmt : block->statements) {
            if (stmt) {
                copy->statements.push_back(clone_stmt(stmt.get()));
            }
        }
        return copy;
    }

    StmtPtr clone_stmt(const Stmt* stmt) {
        if (!stmt) return nullptr;

        switch (stmt->kind) {
        case StmtKind::Expression: {
            const auto* expr_stmt = static_cast<const ExprStmt*>(stmt);
            auto copy = std::make_unique<ExprStmt>();
            copy->line = expr_stmt->line;
            if (expr_stmt->expression) {
                copy->expression = clone_expr(expr_stmt->expression.get());
            }
            return copy;
        }

        case StmtKind::Print: {
            const auto* print = static_cast<const PrintStmt*>(stmt);
            auto copy = std::make_unique<PrintStmt>();
            copy->line = print->line;
            if (print->expression) {
                copy->expression = clone_expr(print->expression.get());
            }
            return copy;
        }

        case StmtKind::VarDecl: {
            const auto* var = static_cast<const VarDeclStmt*>(stmt);
            auto copy = std::make_unique<VarDeclStmt>();
            copy->line = var->line;
            copy->name = var->name;
            copy->is_let = var->is_let;
            copy->is_static = var->is_static;
            copy->is_lazy = var->is_lazy;
            copy->access_level = var->access_level;
            copy->attributes = clone_attributes(var->attributes);
            copy->type_annotation = var->type_annotation;
            if (var->initializer) {
                copy->initializer = clone_expr(var->initializer.get());
            }
            return copy;
        }

        case StmtKind::TupleDestructuring: {
            const auto* td = static_cast<const TupleDestructuringStmt*>(stmt);
            auto copy = std::make_unique<TupleDestructuringStmt>();
            copy->line = td->line;
            copy->bindings = td->bindings;
            copy->is_let = td->is_let;
            if (td->initializer) {
                copy->initializer = clone_expr(td->initializer.get());
            }
            return copy;
        }

        case StmtKind::Return: {
            const auto* ret = static_cast<const ReturnStmt*>(stmt);
            auto copy = std::make_unique<ReturnStmt>();
            copy->line = ret->line;
            if (ret->value) {
                copy->value = clone_expr(ret->value.get());
            }
            return copy;
        }

        case StmtKind::If: {
            const auto* if_stmt = static_cast<const IfStmt*>(stmt);
            auto copy = std::make_unique<IfStmt>();
            copy->line = if_stmt->line;
            if (if_stmt->condition) {
                copy->condition = clone_expr(if_stmt->condition.get());
            }
            if (if_stmt->then_branch) {
                copy->then_branch = clone_stmt(if_stmt->then_branch.get());
            }
            if (if_stmt->else_branch) {
                copy->else_branch = clone_stmt(if_stmt->else_branch.get());
            }
            return copy;
        }

        case StmtKind::Block: {
            return clone_block_stmt(static_cast<const BlockStmt*>(stmt));
        }

                            // Add more cases as needed
        default:
            // For unsupported statement types, return null
            return nullptr;
        }
    }

}
