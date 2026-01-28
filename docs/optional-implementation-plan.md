# SwiftScript Optional 타입 시스템 — 전체 파이프라인 구현 계획

## 핵심 설계 결정

**Optional = Value::Null**. 별도의 Optional 래퍼 객체를 만들지 않는다. 런타임에서 `nil`은 기존 `Value::Null`이고, Optional 여부는 컴파일 타임 타입 시스템이 검증한다. 이렇게 하면 16바이트 Value 제약을 유지하면서 기존 RC 시스템과 완벽히 호환된다.

## 구현 순서 (7단계)

### 1단계: 토큰 확장 (`ss_token.hpp`, `ss_token.cpp`)

**수정 파일:**
- `include/ss_token.hpp` — TokenType enum에 추가:
  ```
  NilCoalesce,     // ??
  OptionalChain,   // ?.
  Nil,             // nil (keyword, 기존 Null을 리터럴용으로 유지)
  Guard,           // guard (keyword)
  ```
- `src/ss_token.cpp` — `kTokenTypeNames` 배열, `keyword_type()` 맵, `operator_precedence()` 업데이트:
  - `??` 우선순위: 2 (ternary `?`와 동일 레벨)
  - `?.` 우선순위: 15 (member access `.`보다 약간 높거나 동일)
  - `guard` → `TokenType::Guard` 키워드 매핑
  - `nil` → `TokenType::Nil` 키워드 매핑

### 2단계: 렉서 (`ss_lexer.hpp`, `ss_lexer.cpp`) — 신규

**신규 파일:**
- `include/ss_lexer.hpp`
- `src/ss_lexer.cpp`

```cpp
class Lexer {
    std::string_view source_;
    uint32_t current_, line_, column_;
public:
    explicit Lexer(std::string_view source);
    Token next_token();
    std::vector<Token> tokenize_all();
private:
    char advance();
    char peek();
    char peek_next();
    bool match(char expected);
    bool is_at_end();
    Token make_token(TokenType type);
    Token scan_number();
    Token scan_string();
    Token scan_identifier();
    void skip_whitespace();
};
```

핵심 렉싱 로직:
- `?` → peek `?`이면 `NilCoalesce`, peek `.`이면 `OptionalChain`, 아니면 `Question`
- `!` → peek `=`이면 `NotEqual`, 아니면 `Not` (force unwrap은 파서에서 postfix로 처리)
- `.` → peek `.`이면 `Range`/`RangeInclusive`, 아니면 `Dot`
- 문자열: `"..."` 지원
- 숫자: 정수/실수 구분
- 식별자/키워드: `keyword_type()` 활용

### 3단계: AST 노드 (`ss_ast.hpp`) — 신규

**신규 파일:**
- `include/ss_ast.hpp`

타입 표현:
```cpp
struct TypeAnnotation {
    std::string name;           // "Int", "String", etc.
    bool is_optional{false};    // Int? → true
};
```

AST 노드 계층 (std::unique_ptr 기반):
```cpp
// --- Expressions ---
struct Expr { virtual ~Expr() = default; uint32_t line; };

struct LiteralExpr : Expr       { Value value; };
struct IdentifierExpr : Expr    { std::string name; };
struct UnaryExpr : Expr         { TokenType op; unique_ptr<Expr> operand; };
struct BinaryExpr : Expr        { TokenType op; unique_ptr<Expr> left, right; };
struct AssignExpr : Expr        { std::string name; unique_ptr<Expr> value; };
struct CallExpr : Expr          { unique_ptr<Expr> callee; vector<unique_ptr<Expr>> args; };
struct MemberExpr : Expr        { unique_ptr<Expr> object; std::string member; };

// Optional-specific expressions
struct ForceUnwrapExpr : Expr   { unique_ptr<Expr> operand; };           // expr!
struct OptionalChainExpr : Expr { unique_ptr<Expr> object; std::string member; }; // expr?.member
struct NilCoalesceExpr : Expr   { unique_ptr<Expr> optional; unique_ptr<Expr> fallback; }; // a ?? b

// --- Statements ---
struct Stmt { virtual ~Stmt() = default; uint32_t line; };

struct ExprStmt : Stmt          { unique_ptr<Expr> expression; };
struct PrintStmt : Stmt         { unique_ptr<Expr> expression; };       // print() 내장
struct BlockStmt : Stmt         { vector<unique_ptr<Stmt>> statements; };
struct ReturnStmt : Stmt        { unique_ptr<Expr> value; };            // nullable
struct IfStmt : Stmt            { unique_ptr<Expr> condition;
                                  unique_ptr<Stmt> then_branch;
                                  unique_ptr<Stmt> else_branch; };      // nullable

// Variable declaration
struct VarDeclStmt : Stmt       { std::string name;
                                  bool is_let;                          // let vs var
                                  std::optional<TypeAnnotation> type_ann;
                                  unique_ptr<Expr> initializer; };      // nullable

// Optional binding
struct IfLetStmt : Stmt         { std::string binding_name;
                                  unique_ptr<Expr> optional_expr;
                                  unique_ptr<Stmt> then_branch;
                                  unique_ptr<Stmt> else_branch; };      // nullable

struct GuardLetStmt : Stmt      { std::string binding_name;
                                  unique_ptr<Expr> optional_expr;
                                  unique_ptr<Stmt> else_branch; };      // else는 필수

// Function declaration
struct FuncDeclStmt : Stmt      { std::string name;
                                  vector<pair<string, TypeAnnotation>> params;
                                  optional<TypeAnnotation> return_type;
                                  unique_ptr<BlockStmt> body; };

// While loop
struct WhileStmt : Stmt         { unique_ptr<Expr> condition;
                                  unique_ptr<Stmt> body; };
```

### 4단계: 파서 (`ss_parser.hpp`, `ss_parser.cpp`) — 신규

**신규 파일:**
- `include/ss_parser.hpp`
- `src/ss_parser.cpp`

```cpp
class Parser {
    std::vector<Token> tokens_;
    size_t current_{0};
public:
    explicit Parser(std::vector<Token> tokens);
    std::vector<std::unique_ptr<Stmt>> parse();   // 프로그램 = 문장의 리스트
private:
    // Statement parsers
    unique_ptr<Stmt> declaration();
    unique_ptr<Stmt> var_declaration();
    unique_ptr<Stmt> func_declaration();
    unique_ptr<Stmt> statement();
    unique_ptr<Stmt> if_statement();        // if let 포함
    unique_ptr<Stmt> guard_statement();
    unique_ptr<Stmt> while_statement();
    unique_ptr<Stmt> return_statement();
    unique_ptr<Stmt> print_statement();
    unique_ptr<BlockStmt> block();
    unique_ptr<Stmt> expression_statement();

    // Expression parsers (Pratt parser / precedence climbing)
    unique_ptr<Expr> expression();
    unique_ptr<Expr> assignment();
    unique_ptr<Expr> nil_coalesce();        // ??  (right-associative)
    unique_ptr<Expr> or_expr();
    unique_ptr<Expr> and_expr();
    unique_ptr<Expr> equality();
    unique_ptr<Expr> comparison();
    unique_ptr<Expr> addition();
    unique_ptr<Expr> multiplication();
    unique_ptr<Expr> unary();
    unique_ptr<Expr> postfix();             // !, ?., .member, (call)
    unique_ptr<Expr> primary();

    // Type annotation parser
    TypeAnnotation parse_type_annotation();  // "Int", "String?", etc.

    // Utilities
    Token advance();
    Token peek();
    Token previous();
    bool check(TokenType type);
    bool match(TokenType type);
    bool match_any(std::initializer_list<TokenType> types);
    Token consume(TokenType type, const std::string& message);
    void error(const Token& token, const std::string& message);
};
```

**파싱 규칙 핵심:**
- `if let x = expr { }` → `if` 다음에 `let`이 오면 `IfLetStmt`로 분기
- `guard let x = expr else { }` → `guard` 다음에 `let`이 오면 `GuardLetStmt`
- `expr!` → postfix에서 `Not` 토큰을 만나면 `ForceUnwrapExpr`로 래핑
- `expr?.member` → postfix에서 `OptionalChain` 토큰을 만나면 `OptionalChainExpr`
- `a ?? b` → `nil_coalesce()` 단계에서 right-associative 파싱
- `var x: Int? = nil` → `var_declaration()`에서 `:` 뒤 타입 어노테이션 파싱, `?` 접미사 처리

### 5단계: 바이트코드 & 컴파일러 (`ss_chunk.hpp`, `ss_compiler.hpp`, `ss_compiler.cpp`) — 신규

**신규 파일:**
- `include/ss_chunk.hpp` — 바이트코드 표현
- `include/ss_compiler.hpp`
- `src/ss_compiler.cpp`

```cpp
// Opcodes
enum class OpCode : uint8_t {
    // Constants & stack
    OP_CONSTANT,        // push constant[index]
    OP_NIL,             // push nil
    OP_TRUE,            // push true
    OP_FALSE,           // push false
    OP_POP,             // discard top

    // Arithmetic
    OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE, OP_MODULO, OP_NEGATE,

    // Comparison
    OP_EQUAL, OP_NOT_EQUAL, OP_LESS, OP_GREATER, OP_LESS_EQUAL, OP_GREATER_EQUAL,

    // Logic
    OP_NOT, OP_AND, OP_OR,

    // Variables
    OP_GET_GLOBAL,      // operand: name index
    OP_SET_GLOBAL,      // operand: name index
    OP_GET_LOCAL,       // operand: stack slot
    OP_SET_LOCAL,       // operand: stack slot

    // Control flow
    OP_JUMP,            // operand: offset (unconditional)
    OP_JUMP_IF_FALSE,   // operand: offset
    OP_LOOP,            // operand: offset (backward jump)

    // Functions
    OP_CALL,            // operand: arg count
    OP_RETURN,

    // Optional-specific
    OP_UNWRAP,          // force unwrap: pop top, if nil → runtime error, else push value
    OP_JUMP_IF_NIL,     // optional binding: if top is nil, jump; else leave value
    OP_NIL_COALESCE,    // pop 2, if first is nil push second, else push first
    OP_OPTIONAL_CHAIN,  // operand: member name index; if obj nil → push nil, else access member

    // Objects
    OP_GET_PROPERTY,    // operand: name index
    OP_SET_PROPERTY,    // operand: name index

    // I/O
    OP_PRINT,

    // End
    OP_HALT,
};

// Chunk = compiled bytecode for one function/script
struct Chunk {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<uint32_t> lines;    // line number per instruction (for errors)

    size_t add_constant(Value val);
    void write(uint8_t byte, uint32_t line);
    void write_op(OpCode op, uint32_t line);
    size_t emit_jump(OpCode op, uint32_t line);     // returns offset to patch
    void patch_jump(size_t offset);
};
```

**Compiler — AST → Chunk:**
```cpp
class Compiler {
    Chunk chunk_;
    struct Local { std::string name; int depth; bool is_optional; };
    std::vector<Local> locals_;
    int scope_depth_{0};
public:
    Chunk compile(const std::vector<unique_ptr<Stmt>>& program);
private:
    void compile_stmt(Stmt* stmt);
    void compile_expr(Expr* expr);

    // Statement visitors
    void visit(VarDeclStmt* s);
    void visit(IfStmt* s);
    void visit(IfLetStmt* s);       // emit OP_JUMP_IF_NIL + local binding
    void visit(GuardLetStmt* s);    // emit OP_JUMP_IF_NIL (inverted) + else block
    void visit(WhileStmt* s);
    void visit(BlockStmt* s);
    void visit(PrintStmt* s);
    void visit(ReturnStmt* s);
    void visit(FuncDeclStmt* s);
    void visit(ExprStmt* s);

    // Expression visitors
    void visit(LiteralExpr* e);
    void visit(IdentifierExpr* e);
    void visit(UnaryExpr* e);
    void visit(BinaryExpr* e);
    void visit(AssignExpr* e);
    void visit(ForceUnwrapExpr* e);     // compile operand + OP_UNWRAP
    void visit(NilCoalesceExpr* e);     // compile both + OP_NIL_COALESCE
    void visit(OptionalChainExpr* e);   // compile object + OP_OPTIONAL_CHAIN
    void visit(MemberExpr* e);
    void visit(CallExpr* e);

    // Scope management
    void begin_scope();
    void end_scope();
    int resolve_local(const std::string& name);
    void add_local(const std::string& name, bool is_optional);
};
```

**Optional 관련 컴파일 전략:**
- `expr!` → `compile(expr)` + `OP_UNWRAP`
- `a ?? b` → `compile(a)` + `OP_NIL_COALESCE` + `compile(b)` 또는 jump 기반
  - 더 효율적: `compile(a)` → `OP_JUMP_IF_NIL(skip)` → `OP_JUMP(end)` → `skip: OP_POP` → `compile(b)` → `end:`
- `if let x = expr { body }` → `compile(expr)` → `OP_JUMP_IF_NIL(else_branch)` → bind local → `compile(body)` → `else_branch:`
- `guard let x = expr else { }` → `compile(expr)` → `OP_JUMP_IF_NIL(else)` → bind local → continue → `else:` → `compile(else_block)`
- `obj?.member` → `compile(obj)` → `OP_OPTIONAL_CHAIN(member_name_idx)`
- `var x: Int? = nil` → optional 타입은 컴파일러가 추적, `nil` 초기화 시 `OP_NIL`

### 6단계: VM 인터프리터 확장 (`ss_vm.hpp`, `ss_vm.cpp`) — 수정

**수정 파일:**
- `include/ss_vm.hpp` — VM에 interpret 메서드, ip 추가
- `src/ss_vm.cpp` — 바이트코드 디스패치 루프 추가

VM 클래스에 추가:
```cpp
class VM {
    // ... 기존 멤버 ...
    // 새로 추가:
    const Chunk* chunk_{nullptr};
    size_t ip_{0};                       // instruction pointer

public:
    // 새로 추가:
    Value interpret(const std::string& source);     // 소스 → 토큰 → AST → 바이트코드 → 실행
    Value execute(const Chunk& chunk);               // 바이트코드 직접 실행

private:
    Value run();                                     // 메인 디스패치 루프
    uint8_t read_byte();
    uint16_t read_short();
    Value read_constant();
};
```

**디스패치 루프 핵심 (run()):**
```
for (;;) {
    switch (read_byte()) {
        case OP_CONSTANT:   push(read_constant()); break;
        case OP_NIL:        push(Value::null()); break;
        case OP_ADD:        { auto b=pop(); auto a=pop(); push(add(a,b)); } break;
        ...
        // Optional ops:
        case OP_UNWRAP: {
            Value v = peek(0);
            if (v.is_null()) runtime_error("Force unwrap of nil value");
            break;  // value stays on stack
        }
        case OP_JUMP_IF_NIL: {
            uint16_t offset = read_short();
            if (peek(0).is_null()) { pop(); ip_ += offset; }
            break;
        }
        case OP_NIL_COALESCE: {
            Value fallback = pop();
            Value optional = pop();
            push(optional.is_null() ? fallback : optional);
            break;
        }
        case OP_OPTIONAL_CHAIN: {
            uint16_t name_idx = read_short();
            Value obj = pop();
            if (obj.is_null()) { push(Value::null()); }
            else { /* access member */ push(get_member(obj, name_idx)); }
            break;
        }
        case OP_HALT: return pop();
    }
}
```

### 7단계: 테스트 (`test/test_optional.cpp`) — 신규

**신규 파일:**
- `test/test_optional.cpp`

테스트 케이스:
```cpp
void test_optional_nil_coalesce();      // var x: Int? = nil; print(x ?? 42)  → 42
void test_optional_force_unwrap();      // var x: Int? = 10; print(x!)  → 10
void test_optional_force_unwrap_nil();  // var x: Int? = nil; x!  → runtime error
void test_if_let();                     // if let v = x { print(v) } else { print("nil") }
void test_guard_let();                  // guard let v = x else { print("nil"); return }
void test_optional_chaining();          // obj?.property  → nil or value
void test_nil_coalesce_chain();         // a ?? b ?? c
void test_optional_assignment();        // var x: Int? = 5; x = nil
```

---

## 파일 변경 요약

| 작업 | 파일 | 설명 |
|------|------|------|
| 수정 | `include/ss_token.hpp` | NilCoalesce, OptionalChain, Nil, Guard 토큰 추가 |
| 수정 | `src/ss_token.cpp` | 토큰 이름, 키워드, 우선순위 업데이트 |
| 신규 | `include/ss_lexer.hpp` | Lexer 클래스 |
| 신규 | `src/ss_lexer.cpp` | 렉서 구현 |
| 신규 | `include/ss_ast.hpp` | AST 노드 정의 (Expr, Stmt, TypeAnnotation) |
| 신규 | `include/ss_parser.hpp` | Parser 클래스 |
| 신규 | `src/ss_parser.cpp` | 재귀 하강 파서 구현 |
| 신규 | `include/ss_chunk.hpp` | OpCode enum, Chunk 구조체 |
| 신규 | `include/ss_compiler.hpp` | Compiler 클래스 |
| 신규 | `src/ss_compiler.cpp` | AST→바이트코드 컴파일러 |
| 수정 | `include/ss_vm.hpp` | interpret(), execute(), run() 추가 |
| 수정 | `src/ss_vm.cpp` | 바이트코드 디스패치 루프 구현 |
| 신규 | `test/test_optional.cpp` | Optional 기능 통합 테스트 |
| 수정 | `SwiftScript.vcxproj` | 새 파일들 프로젝트에 추가 |

총: **신규 7개 파일** + **수정 5개 파일**

## 검증 방법

1. MSBuild로 Debug|x64 빌드 성공 확인
2. `test_optional.cpp`의 main()이 모든 테스트 케이스 통과
3. 다음 Swift-like 코드를 `VM::interpret()`로 실행하여 결과 확인:
   ```swift
   var name: String? = "Swift"
   print(name ?? "unknown")     // → Swift
   name = nil
   print(name ?? "unknown")     // → unknown
   if let n = name {
       print(n)
   } else {
       print("name is nil")     // → name is nil
   }
   ```
4. force unwrap of nil에서 런타임 에러 발생 확인
5. 기존 test_basic.cpp 테스트가 여전히 통과하는지 확인
