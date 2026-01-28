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
    bool is_function_type{false};
    std::vector<TypeAnnotation> param_types;
    std::shared_ptr<TypeAnnotation> return_type; // nullptr if not a function type
};

// ---- Forward declarations ----
struct Expr;
struct Stmt;
struct FuncDeclStmt;
struct ClassDeclStmt;

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
    Range,
    Ternary,         // condition ? then : else
    ArrayLiteral,    // [1, 2, 3]
    DictLiteral,     // ["key": value]
    Subscript,       // array[0], dict["key"]
    Closure,         // { (params) -> ReturnType in body }
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
    TokenType op;  // ����: Equal �Ǵ� PlusEqual, MinusEqual ��
    
    AssignExpr() : Expr(ExprKind::Assign), op(TokenType::Equal) {}
    AssignExpr(std::string n, ExprPtr v, TokenType operation = TokenType::Equal)
        : Expr(ExprKind::Assign), name(std::move(n)), value(std::move(v)), op(operation) {}
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

// Range expression
struct RangeExpr : Expr {
    ExprPtr start;
    ExprPtr end;
    bool inclusive;  // true = ..., false = ..<

    RangeExpr() : Expr(ExprKind::Range), inclusive(true) {}
    RangeExpr(ExprPtr s, ExprPtr e, bool incl)
        : Expr(ExprKind::Range), start(std::move(s)), end(std::move(e)), inclusive(incl) {}
};

// Ternary expression: condition ? then_expr : else_expr
struct TernaryExpr : Expr {
    ExprPtr condition;
    ExprPtr then_expr;
    ExprPtr else_expr;
    TernaryExpr() : Expr(ExprKind::Ternary) {}
    TernaryExpr(ExprPtr cond, ExprPtr then_e, ExprPtr else_e)
        : Expr(ExprKind::Ternary), condition(std::move(cond)),
          then_expr(std::move(then_e)), else_expr(std::move(else_e)) {}
};

// ---- Collection expressions ----

// Array literal: [1, 2, 3]
struct ArrayLiteralExpr : Expr {
    std::vector<ExprPtr> elements;

    ArrayLiteralExpr() : Expr(ExprKind::ArrayLiteral) {}
    explicit ArrayLiteralExpr(std::vector<ExprPtr> elems)
        : Expr(ExprKind::ArrayLiteral), elements(std::move(elems)) {}
};

// Dictionary literal: ["key": value, "key2": value2]
struct DictLiteralExpr : Expr {
    std::vector<std::pair<ExprPtr, ExprPtr>> entries;

    DictLiteralExpr() : Expr(ExprKind::DictLiteral) {}
    explicit DictLiteralExpr(std::vector<std::pair<ExprPtr, ExprPtr>> ents)
        : Expr(ExprKind::DictLiteral), entries(std::move(ents)) {}
};

// Subscript access: array[0], dict["key"]
struct SubscriptExpr : Expr {
    ExprPtr object;
    ExprPtr index;

    SubscriptExpr() : Expr(ExprKind::Subscript) {}
    SubscriptExpr(ExprPtr obj, ExprPtr idx)
        : Expr(ExprKind::Subscript), object(std::move(obj)), index(std::move(idx)) {}
};

// Closure expression: { (a: Int, b: Int) -> Int in return a + b }
struct ClosureExpr : Expr {
    std::vector<std::pair<std::string, TypeAnnotation>> params;
    std::optional<TypeAnnotation> return_type;
    std::vector<StmtPtr> body;
    
    ClosureExpr() : Expr(ExprKind::Closure) {}
};

// ============================================================
//  Statements
// ============================================================

enum class StmtKind {
    Expression,
    Print,
    Block,
    VarDecl,
    ClassDecl,
    If,
    IfLet,
    GuardLet,
    While,
    ForIn,      // �߰�
    Break,      // �߰�
    Continue,   // �߰�
    Switch,     // �߰�
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
    std::optional<TypeAnnotation> type_annotation;
    ExprPtr initializer;
    bool is_let{false};
    VarDeclStmt() : Stmt(StmtKind::VarDecl) {}
};

struct IfStmt : Stmt {
    ExprPtr condition;
    StmtPtr then_branch;
    StmtPtr else_branch;
    IfStmt() : Stmt(StmtKind::If) {}
};

struct IfLetStmt : Stmt {
    std::string binding_name;
    ExprPtr optional_expr;
    StmtPtr then_branch;
    StmtPtr else_branch;
    IfLetStmt() : Stmt(StmtKind::IfLet) {}
};

struct GuardLetStmt : Stmt {
    std::string binding_name;
    ExprPtr optional_expr;
    StmtPtr else_branch;
    GuardLetStmt() : Stmt(StmtKind::GuardLet) {}
};

struct WhileStmt : Stmt {
    ExprPtr condition;
    StmtPtr body;
    WhileStmt() : Stmt(StmtKind::While) {}
};

// �߰�: For-In ��
struct ForInStmt : Stmt {
    std::string variable;
    ExprPtr iterable;
    StmtPtr body;
    
    ForInStmt() : Stmt(StmtKind::ForIn) {}
    ForInStmt(std::string var, ExprPtr iter, StmtPtr b)
        : Stmt(StmtKind::ForIn), variable(std::move(var)), 
          iterable(std::move(iter)), body(std::move(b)) {}
};

// �߰�: Break ��
struct BreakStmt : Stmt {
    BreakStmt() : Stmt(StmtKind::Break) {}
};

// �߰�: Continue ��
struct ContinueStmt : Stmt {
    ContinueStmt() : Stmt(StmtKind::Continue) {}
};

// Switch case clause
struct CaseClause {
    std::vector<ExprPtr> patterns;  // Can be values or ranges
    std::vector<StmtPtr> statements;
    bool is_default{false};
};

// Switch statement
struct SwitchStmt : Stmt {
    ExprPtr value;
    std::vector<CaseClause> cases;
    
    SwitchStmt() : Stmt(StmtKind::Switch) {}
};

struct ReturnStmt : Stmt {
    ExprPtr value;
    ReturnStmt() : Stmt(StmtKind::Return) {}
    explicit ReturnStmt(ExprPtr v) : Stmt(StmtKind::Return), value(std::move(v)) {}
};

struct FuncDeclStmt : Stmt {
    std::string name;
    std::vector<std::pair<std::string, TypeAnnotation>> params;
    std::unique_ptr<BlockStmt> body;
    std::optional<TypeAnnotation> return_type;
    FuncDeclStmt() : Stmt(StmtKind::FuncDecl) {}
};

struct ClassDeclStmt : Stmt {
    std::string name;
    std::vector<std::unique_ptr<FuncDeclStmt>> methods;
    ClassDeclStmt() : Stmt(StmtKind::ClassDecl) {}
};

} // namespace swiftscript
