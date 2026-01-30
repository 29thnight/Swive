#pragma once

#include "ss_token.hpp"
#include "ss_value.hpp"
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <utility>

namespace swiftscript {

// ---- Access Control ----
enum class AccessLevel {
    Public,       // Accessible from anywhere
    Internal,     // Accessible within the same module (default)
    Fileprivate,  // Accessible within the same file
    Private       // Accessible only within the same declaration
};

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
struct StructDeclStmt;
struct StructMethodDecl;
struct EnumDeclStmt;

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
    Super,
    ForceUnwrap,
    OptionalChain,
    NilCoalesce,
    Range,
    Ternary,         // condition ? then : else
    ArrayLiteral,    // [1, 2, 3]
    DictLiteral,     // ["key": value]
    Subscript,       // array[0], dict["key"]
    Closure,         // { (params) -> ReturnType in body }
    TypeCast,        // as, as?, as!
    TypeCheck,       // is
    Try,             // try expression
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
    std::vector<std::string> argument_names;  // Named parameters (empty string = no name)
    CallExpr() : Expr(ExprKind::Call) {}
};

struct MemberExpr : Expr {
    ExprPtr object;
    std::string member;
    MemberExpr() : Expr(ExprKind::Member) {}
    MemberExpr(ExprPtr obj, std::string m)
        : Expr(ExprKind::Member), object(std::move(obj)), member(std::move(m)) {}
};

struct SuperExpr : Expr {
    std::string method;
    SuperExpr() : Expr(ExprKind::Super) {}
    explicit SuperExpr(std::string m) : Expr(ExprKind::Super), method(std::move(m)) {}
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

// Type casting: expr as Type, expr as? Type, expr as! Type
struct TypeCastExpr : Expr {
    ExprPtr value;
    TypeAnnotation target_type;
    bool is_optional{false};   // as? (returns optional)
    bool is_forced{false};     // as! (force unwrap, runtime error if fails)
    
    TypeCastExpr() : Expr(ExprKind::TypeCast) {}
};

// Type check: expr is Type
struct TypeCheckExpr : Expr {
    ExprPtr value;
    TypeAnnotation target_type;
    
    TypeCheckExpr() : Expr(ExprKind::TypeCheck) {}
};

// Try expression: try expression
struct TryExpr : Expr {
    ExprPtr expression;
    bool is_optional{false};   // try?
    bool is_forced{false};     // try!
    
    TryExpr() : Expr(ExprKind::Try) {}
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

struct ParamDecl {
    std::string external_name;
    std::string internal_name;
    TypeAnnotation type;
    ExprPtr default_value;
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
    StructDecl,  // Struct declaration
    EnumDecl,    // Enum declaration
    ProtocolDecl, // Protocol declaration
    ExtensionDecl, // Extension declaration
    If,
    IfLet,
    GuardLet,
    While,
    RepeatWhile, // repeat-while loop
    ForIn,      // for-in loop
    Break,
    Continue,
    Switch,
    Return,
    FuncDecl,
    Import,     // Import statement
    Throw,      // throw statement
    DoCatch,    // do-catch block
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
    bool is_static{false};  // static properties belong to the type
    bool is_lazy{false};     // lazy properties are initialized on first access
    AccessLevel access_level{AccessLevel::Internal};  // Default is internal
    
    // Computed property support
    bool is_computed{false};
    std::unique_ptr<BlockStmt> getter_body;
    std::unique_ptr<BlockStmt> setter_body;
    
    // Property observers
    std::unique_ptr<BlockStmt> will_set_body;
    std::unique_ptr<BlockStmt> did_set_body;
    
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

struct RepeatWhileStmt : Stmt {
    StmtPtr body;
    ExprPtr condition;
    RepeatWhileStmt() : Stmt(StmtKind::RepeatWhile) {}
};

// �߰�: For-In ��
struct ForInStmt : Stmt {
    std::string variable;
    ExprPtr iterable;
    StmtPtr body;
    ExprPtr where_condition;  // Optional where clause
    
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
enum class PatternKind {
    Expression,
    EnumCase,
};

struct Pattern {
    PatternKind kind;
    uint32_t line{0};
    virtual ~Pattern() = default;
protected:
    explicit Pattern(PatternKind k) : kind(k) {}
};

struct ExpressionPattern : Pattern {
    ExprPtr expression;
    ExpressionPattern() : Pattern(PatternKind::Expression) {}
};

struct EnumCasePattern : Pattern {
    std::string case_name;
    std::vector<std::string> bindings;
    EnumCasePattern() : Pattern(PatternKind::EnumCase) {}
};

using PatternPtr = std::unique_ptr<Pattern>;

// Switch case clause
struct CaseClause {
    std::vector<PatternPtr> patterns;  // Can be values, ranges, or enum patterns
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

struct ThrowStmt : Stmt {
    ExprPtr value;  // Error value to throw
    ThrowStmt() : Stmt(StmtKind::Throw) {}
    explicit ThrowStmt(ExprPtr v) : Stmt(StmtKind::Throw), value(std::move(v)) {}
};

struct CatchClause {
    std::string binding_name;  // Variable name to bind error (default: "error")
    std::vector<StmtPtr> statements;
};

struct DoCatchStmt : Stmt {
    std::unique_ptr<BlockStmt> try_block;
    std::vector<CatchClause> catch_clauses;
    
    DoCatchStmt() : Stmt(StmtKind::DoCatch) {}
};

struct FuncDeclStmt : Stmt {
    std::string name;
    std::vector<ParamDecl> params;
    std::unique_ptr<BlockStmt> body;
    std::optional<TypeAnnotation> return_type;
    bool is_override{false};
    bool is_static{false};  // static functions belong to the type
    bool can_throw{false};  // throws keyword
    AccessLevel access_level{AccessLevel::Internal};  // Default is internal
    FuncDeclStmt() : Stmt(StmtKind::FuncDecl) {}
};

struct ClassDeclStmt : Stmt {
    std::string name;
    std::optional<std::string> superclass_name;
    std::vector<std::string> protocol_conformances;  // Protocols this class conforms to
    std::vector<std::unique_ptr<FuncDeclStmt>> methods;
    std::vector<std::unique_ptr<VarDeclStmt>> properties;
    std::unique_ptr<BlockStmt> deinit_body;  // Optional deinit
    ClassDeclStmt() : Stmt(StmtKind::ClassDecl) {}
};

// Struct method declaration with mutating flag
struct StructMethodDecl {
    std::string name;
    std::vector<ParamDecl> params;
    std::unique_ptr<BlockStmt> body;
    std::optional<TypeAnnotation> return_type;
    bool is_mutating{false};  // mutating methods can modify self
    bool is_computed_property{false};  // true for var name: Type { }, false for func name()
    bool is_static{false};  // static methods belong to the type, not instances
    AccessLevel access_level{AccessLevel::Internal};  // Default is internal
};

// Struct declaration: struct Point { var x: Int; func distance() -> Float { ... } }
struct StructDeclStmt : Stmt {
    std::string name;
    std::vector<std::string> protocol_conformances;  // Protocols this struct conforms to
    std::vector<std::unique_ptr<VarDeclStmt>> properties;  // Stored properties
    std::vector<std::unique_ptr<StructMethodDecl>> methods;
    std::vector<std::unique_ptr<FuncDeclStmt>> initializers;  // init methods

    StructDeclStmt() : Stmt(StmtKind::StructDecl) {}
};

// Enum case declaration
struct EnumCaseDecl {
    std::string name;
    std::optional<Value> raw_value;  // For raw value enums (Int, String, etc.)
    // For associated values (future extension)
    std::vector<std::pair<std::string, TypeAnnotation>> associated_values;
};

// Enum declaration: enum Direction { case north; case south }
struct EnumDeclStmt : Stmt {
    std::string name;
    std::vector<EnumCaseDecl> cases;
    std::optional<TypeAnnotation> raw_type;  // Type of raw values (if any)
    std::vector<std::unique_ptr<StructMethodDecl>> methods;  // Methods and computed properties

    EnumDeclStmt() : Stmt(StmtKind::EnumDecl) {}
};

// Import statement: import "module.ss"
struct ImportStmt : Stmt {
    std::string module_path;  // Path to the module file
    
    ImportStmt() : Stmt(StmtKind::Import) {}
    explicit ImportStmt(std::string path) 
        : Stmt(StmtKind::Import), module_path(std::move(path)) {}
};

// Protocol method requirement
struct ProtocolMethodRequirement {
    std::string name;
    std::vector<ParamDecl> params;
    std::optional<TypeAnnotation> return_type;
    bool is_mutating{false};
};

// Protocol property requirement
struct ProtocolPropertyRequirement {
    std::string name;
    TypeAnnotation type;
    bool has_getter{true};
    bool has_setter{false};  // true for { get set }, false for { get }
};

// Protocol declaration: protocol Drawable { func draw(); var size: Int { get set } }
struct ProtocolDeclStmt : Stmt {
    std::string name;
    std::vector<ProtocolMethodRequirement> method_requirements;
    std::vector<ProtocolPropertyRequirement> property_requirements;
    std::vector<std::string> inherited_protocols;  // Protocol inheritance

    ProtocolDeclStmt() : Stmt(StmtKind::ProtocolDecl) {}
};

// Extension declaration: extension String { func reversed() -> String { ... } }
struct ExtensionDeclStmt : Stmt {
    std::string extended_type;  // Type being extended (e.g., "String", "Int", "MyClass")
    std::vector<std::string> protocol_conformances;  // Protocols added in this extension
    std::vector<std::unique_ptr<StructMethodDecl>> methods;  // Methods (including computed properties)
    
    ExtensionDeclStmt() : Stmt(StmtKind::ExtensionDecl) {}
};

} // namespace swiftscript
