#pragma once

#include "ss_token.hpp"
#include "ss_value.hpp"
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <utility>

namespace swiftscript {

// ---- Type annotation ----
struct TypeAnnotation {
    std::string name;
    bool is_optional{false};
};

// ---- Forward declarations ----
struct Expr;
struct Stmt;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ============================================================
//  Expressions
// ============================================================

enum class ExprKind {
    Literal,
    Identifier,
    Unary,
    Binary,
    Assign,
    Call,
    Member,
    ForceUnwrap,
    OptionalChain,
    NilCoalesce,
};

struct Expr {
    ExprKind kind;
    uint32_t line{0};
    virtual ~Expr() = default;
protected:
    explicit Expr(ExprKind k) : kind(k) {}
};

struct LiteralExpr : Expr {
    Value value;
    std::optional<std::string> string_value;
    LiteralExpr() : Expr(ExprKind::Literal) {}
    explicit LiteralExpr(Value v) : Expr(ExprKind::Literal), value(v) {}
    explicit LiteralExpr(std::string s)
        : Expr(ExprKind::Literal), value(Value::null()), string_value(std::move(s)) {}
};

struct IdentifierExpr : Expr {
    std::string name;
    IdentifierExpr() : Expr(ExprKind::Identifier) {}
    explicit IdentifierExpr(std::string n) : Expr(ExprKind::Identifier), name(std::move(n)) {}
};

struct UnaryExpr : Expr {
    TokenType op;
    ExprPtr operand;
    UnaryExpr() : Expr(ExprKind::Unary), op(TokenType::Minus) {}
    UnaryExpr(TokenType o, ExprPtr operand)
        : Expr(ExprKind::Unary), op(o), operand(std::move(operand)) {}
};

struct BinaryExpr : Expr {
    TokenType op;
    ExprPtr left;
    ExprPtr right;
    BinaryExpr() : Expr(ExprKind::Binary), op(TokenType::Plus) {}
    BinaryExpr(TokenType o, ExprPtr l, ExprPtr r)
        : Expr(ExprKind::Binary), op(o), left(std::move(l)), right(std::move(r)) {}
};

struct AssignExpr : Expr {
    std::string name;
    ExprPtr value;
    AssignExpr() : Expr(ExprKind::Assign) {}
    AssignExpr(std::string n, ExprPtr v)
        : Expr(ExprKind::Assign), name(std::move(n)), value(std::move(v)) {}
};

struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
    CallExpr() : Expr(ExprKind::Call) {}
};

struct MemberExpr : Expr {
    ExprPtr object;
    std::string member;
    MemberExpr() : Expr(ExprKind::Member) {}
    MemberExpr(ExprPtr obj, std::string m)
        : Expr(ExprKind::Member), object(std::move(obj)), member(std::move(m)) {}
};

// ---- Optional-specific expressions ----

struct ForceUnwrapExpr : Expr {
    ExprPtr operand;
    ForceUnwrapExpr() : Expr(ExprKind::ForceUnwrap) {}
    explicit ForceUnwrapExpr(ExprPtr op)
        : Expr(ExprKind::ForceUnwrap), operand(std::move(op)) {}
};

struct OptionalChainExpr : Expr {
    ExprPtr object;
    std::string member;
    OptionalChainExpr() : Expr(ExprKind::OptionalChain) {}
    OptionalChainExpr(ExprPtr obj, std::string m)
        : Expr(ExprKind::OptionalChain), object(std::move(obj)), member(std::move(m)) {}
};

struct NilCoalesceExpr : Expr {
    ExprPtr optional_expr;
    ExprPtr fallback;
    NilCoalesceExpr() : Expr(ExprKind::NilCoalesce) {}
    NilCoalesceExpr(ExprPtr opt, ExprPtr fb)
        : Expr(ExprKind::NilCoalesce), optional_expr(std::move(opt)), fallback(std::move(fb)) {}
};

// ============================================================
//  Statements
// ============================================================

enum class StmtKind {
    Expression,
    Print,
    Block,
    VarDecl,
    If,
    IfLet,
    GuardLet,
    While,
    Return,
    FuncDecl,
};

struct Stmt {
    StmtKind kind;
    uint32_t line{0};
    virtual ~Stmt() = default;
protected:
    explicit Stmt(StmtKind k) : kind(k) {}
};

struct ExprStmt : Stmt {
    ExprPtr expression;
    ExprStmt() : Stmt(StmtKind::Expression) {}
    explicit ExprStmt(ExprPtr e) : Stmt(StmtKind::Expression), expression(std::move(e)) {}
};

struct PrintStmt : Stmt {
    ExprPtr expression;
    PrintStmt() : Stmt(StmtKind::Print) {}
    explicit PrintStmt(ExprPtr e) : Stmt(StmtKind::Print), expression(std::move(e)) {}
};

struct BlockStmt : Stmt {
    std::vector<StmtPtr> statements;
    BlockStmt() : Stmt(StmtKind::Block) {}
};

struct VarDeclStmt : Stmt {
    std::string name;
    bool is_let{false};
    std::optional<TypeAnnotation> type_annotation;
    ExprPtr initializer;  // may be null
    VarDeclStmt() : Stmt(StmtKind::VarDecl) {}
};

struct IfStmt : Stmt {
    ExprPtr condition;
    StmtPtr then_branch;
    StmtPtr else_branch;  // may be null
    IfStmt() : Stmt(StmtKind::If) {}
};

struct IfLetStmt : Stmt {
    std::string binding_name;
    ExprPtr optional_expr;
    StmtPtr then_branch;
    StmtPtr else_branch;  // may be null
    IfLetStmt() : Stmt(StmtKind::IfLet) {}
};

struct GuardLetStmt : Stmt {
    std::string binding_name;
    ExprPtr optional_expr;
    StmtPtr else_branch;  // required
    GuardLetStmt() : Stmt(StmtKind::GuardLet) {}
};

struct WhileStmt : Stmt {
    ExprPtr condition;
    StmtPtr body;
    WhileStmt() : Stmt(StmtKind::While) {}
};

struct ReturnStmt : Stmt {
    ExprPtr value;  // may be null
    ReturnStmt() : Stmt(StmtKind::Return) {}
};

struct FuncDeclStmt : Stmt {
    std::string name;
    std::vector<std::pair<std::string, TypeAnnotation>> params;
    std::optional<TypeAnnotation> return_type;
    std::unique_ptr<BlockStmt> body;
    FuncDeclStmt() : Stmt(StmtKind::FuncDecl) {}
};

} // namespace swiftscript
