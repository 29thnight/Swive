# Optional 타입 시스템 구현 체크리스트

> **최종 업데이트**: 2026-01-28
> **전체 상태**: ✅ **100% 완료**

---

## 1단계: 토큰 확장 ✅ 완료
- [x] `ss_token.hpp` — NilCoalesce(`??`), OptionalChain(`?.`), Nil, Guard 토큰 추가
- [x] `ss_token.cpp` — kTokenTypeNames 배열 업데이트
- [x] `ss_token.cpp` — keyword_type() 맵에 nil, guard 추가
- [x] `ss_token.cpp` — operator_precedence() 업데이트 (NilCoalesce 우선순위 2)
- [x] `ss_token.hpp` — RangeExclusive (`..<`) 토큰 추가
- [x] 빌드 확인

---

## 2단계: 렉서 ✅ 완료
- [x] `ss_lexer.hpp` — Lexer 클래스 정의 (45줄)
- [x] `ss_lexer.cpp` — 렉서 구현 (248줄) - 숫자, 문자열, 식별자, 연산자
- [x] `ss_lexer.cpp` — `?` / `??` / `?.` 멀티캐릭터 토큰 처리 (226-228줄)
- [x] `ss_lexer.cpp` — `.` / `..` / `...` / `..<` 구분 처리 (183-194줄)
- [x] `ss_lexer.cpp` — `!` / `!=` 구분 처리
- [x] `ss_lexer.cpp` — 라인/컬럼 추적
- [x] `ss_lexer.cpp` — 주석 지원 (`//`, `/* */`)
- [x] 빌드 확인

---

## 3단계: AST 노드 ✅ 완료
- [x] `ss_ast.hpp` — TypeAnnotation 구조체 (`name`, `is_optional`)
- [x] `ss_ast.hpp` — 기본 Expr 노드 (Literal, Identifier, Unary, Binary, Assign, Call, Member)
- [x] `ss_ast.hpp` — Optional Expr 노드 (ForceUnwrap, OptionalChain, NilCoalesce)
- [x] `ss_ast.hpp` — Range Expr 노드 (RangeExpr with `inclusive` flag)
- [x] `ss_ast.hpp` — 기본 Stmt 노드 (ExprStmt, PrintStmt, Block, Return, If, While)
- [x] `ss_ast.hpp` — 선언 Stmt 노드 (VarDecl, FuncDecl)
- [x] `ss_ast.hpp` — Optional Stmt 노드 (IfLet, GuardLet)
- [x] `ss_ast.hpp` — 제어 Stmt 노드 (ForIn, Break, Continue)
- [x] `ss_ast.hpp` — LiteralExpr에 string_value 필드 추가 (문자열 리터럴 지원)
- [x] 빌드 확인

---

## 4단계: 파서 ✅ 완료
- [x] `ss_parser.hpp` — Parser 클래스 정의 (ParseError 포함)
- [x] `ss_parser.cpp` — 유틸리티 (advance, peek, match, consume, error) (648줄)
- [x] `ss_parser.cpp` — primary/postfix/unary 파싱
- [x] `ss_parser.cpp` — 이항 연산자 파싱 (산술, 비교, 논리)
- [x] `ss_parser.cpp` — nil coalesce (`??`) 파싱 (right-associative, 409-412줄)
- [x] `ss_parser.cpp` — assignment 파싱 (복합 대입 연산자 포함: `+=`, `-=`, `*=`, `/=`)
- [x] `ss_parser.cpp` — var/let 선언 + 타입 어노테이션 파싱
- [x] `ss_parser.cpp` — if / if let 문 파싱 (182-223줄)
- [x] `ss_parser.cpp` — guard let 문 파싱 (225-242줄)
- [x] `ss_parser.cpp` — while, return, print, block 파싱
- [x] `ss_parser.cpp` — for-in 문 파싱 (range 지원)
- [x] `ss_parser.cpp` — break, continue 문 파싱
- [x] `ss_parser.cpp` — func 선언 파싱
- [x] `ss_parser.cpp` — postfix에서 force unwrap (`!`) 파싱 (536-541줄)
- [x] `ss_parser.cpp` — postfix에서 optional chain (`?.`) 파싱 (542-548줄)
- [x] 빌드 확인

---

## 5단계: 바이트코드 & 컴파일러 ✅ 완료
- [x] `ss_chunk.hpp` — OpCode enum 정의 (전체 opcode 포함)
- [x] `ss_chunk.hpp` — Chunk 구조체 (code, constants, lines, strings, functions)
- [x] `ss_chunk.hpp` — 헬퍼 메서드 (write, write_op, add_constant, emit_jump, patch_jump)
- [x] `ss_compiler.hpp` — Compiler 클래스 정의
- [x] `ss_compiler.cpp` — Expression 컴파일 (Literal, Identifier, Unary, Binary) (806줄)
- [x] `ss_compiler.cpp` — Optional Expression 컴파일:
  - [x] ForceUnwrap → `OP_UNWRAP` (585-588줄)
  - [x] NilCoalesce → `OP_JUMP_IF_NIL` + `OP_JUMP` (590-601줄)
  - [x] OptionalChain → `OP_OPTIONAL_CHAIN` (603-611줄)
- [x] `ss_compiler.cpp` — Statement 컴파일 (VarDecl, If, While, Block, Print, Return)
- [x] `ss_compiler.cpp` — Optional Statement 컴파일:
  - [x] IfLet → `OP_JUMP_IF_NIL` 기반 바인딩 (165-185줄)
  - [x] GuardLet → else 분기 검증 + 점프 패칭 (187-220줄)
- [x] `ss_compiler.cpp` — 스코프 관리 (begin_scope, end_scope, resolve_local)
- [x] `ss_compiler.cpp` — FuncDecl 컴파일
- [x] `ss_compiler.cpp` — ForIn, Break, Continue 컴파일
- [x] `ss_compiler.cpp` — Range 표현식 컴파일 (`OP_RANGE_INCLUSIVE`, `OP_RANGE_EXCLUSIVE`)
- [x] `ss_compiler.cpp` — 재귀 깊이 가드 (MAX_RECURSION_DEPTH = 256)
- [x] 빌드 확인

---

## 6단계: VM 인터프리터 확장 ✅ 완료
- [x] `ss_vm.hpp` — interpret(), execute(), run() 선언 추가
- [x] `ss_vm.cpp` — read_byte(), read_short(), read_constant() 구현 (583줄)
- [x] `ss_vm.cpp` — 디스패치 루프: 상수/스택 ops (CONSTANT, STRING, NIL, TRUE, FALSE, POP)
- [x] `ss_vm.cpp` — 디스패치 루프: 산술 ops (ADD, SUB, MUL, DIV, MOD, NEGATE)
- [x] `ss_vm.cpp` — 디스패치 루프: 비교/논리 ops
- [x] `ss_vm.cpp` — 디스패치 루프: 변수 ops (GET/SET_GLOBAL, GET/SET_LOCAL)
- [x] `ss_vm.cpp` — 디스패치 루프: 제어 흐름 (JUMP, JUMP_IF_FALSE, LOOP)
- [x] `ss_vm.cpp` — 디스패치 루프: Optional ops:
  - [x] `OP_UNWRAP` — nil 체크 후 예외 발생 (471-477줄)
  - [x] `OP_JUMP_IF_NIL` — nil이면 점프, 값 pop (478-485줄)
  - [x] `OP_NIL_COALESCE` — fallback 값 선택 (486-491줄)
  - [x] `OP_OPTIONAL_CHAIN` — nil이면 nil 반환, 아니면 속성 접근 (492-501줄)
- [x] `ss_vm.cpp` — 디스패치 루프: PRINT, CALL, RETURN, HALT
- [x] `ss_vm.cpp` — interpret() 통합 (source → lexer → parser → compiler → execute)
- [x] `ss_vm.cpp` — 참조 카운팅 통합
- [x] `ss_vm.cpp` — 메모리 통계 추적
- [x] 빌드 확인

---

## 7단계: 테스트 & 통합 ✅ 완료
- [x] `test/test_optional.cpp` — nil coalesce 테스트 (110줄)
- [x] `test/test_optional.cpp` — force unwrap 테스트 (성공 케이스)
- [x] `test/test_optional.cpp` — force unwrap 테스트 (nil 예외 케이스)
- [x] `test/test_optional.cpp` — if let 테스트
- [x] `test/test_optional.cpp` — guard let 테스트 (성공/else 분기)
- [x] `test/test_optional.cpp` — optional chaining 테스트 (nil/non-nil)
- [x] `test/test_optional.cpp` — nil coalesce chain 테스트 (`a ?? b ?? c`)
- [x] `test/test_optional.cpp` — optional assignment 테스트
- [x] `test/test_optional.cpp` — function call script 테스트
- [x] `SwiftScript.vcxproj` — 신규 파일 프로젝트에 등록
- [x] 전체 빌드 확인 (Debug x64)
- [x] 기존 test_basic.cpp 테스트 통과 확인
- [x] test_optional.cpp 실행 및 전체 통과 확인 (11개 테스트 케이스)

---

## 구현 완료된 파일 목록

| 파일 | 상태 | 라인 수 | 설명 |
|------|------|---------|------|
| `include/ss_token.hpp` | ✅ | 152 | 토큰 타입 정의 |
| `src/ss_token.cpp` | ✅ | 261 | 토큰 유틸리티 |
| `include/ss_lexer.hpp` | ✅ | 45 | 렉서 클래스 |
| `src/ss_lexer.cpp` | ✅ | 248 | 렉서 구현 |
| `include/ss_ast.hpp` | ✅ | 241 | AST 노드 정의 |
| `include/ss_parser.hpp` | ✅ | 69 | 파서 클래스 |
| `src/ss_parser.cpp` | ✅ | 648 | 재귀 하강 파서 |
| `include/ss_chunk.hpp` | ✅ | - | OpCode & Chunk |
| `include/ss_compiler.hpp` | ✅ | - | 컴파일러 클래스 |
| `src/ss_compiler.cpp` | ✅ | 806 | AST→바이트코드 |
| `include/ss_vm.hpp` | ✅ | - | VM 클래스 |
| `src/ss_vm.cpp` | ✅ | 583 | VM 디스패치 루프 |
| `test/test_optional.cpp` | ✅ | 110 | Optional 테스트 |

---

## 추가 구현된 기능 (계획 외)

- [x] Range 연산자 (`...` inclusive, `..<` exclusive)
- [x] For-in 루프 (range 반복)
- [x] Break / Continue 문
- [x] 복합 대입 연산자 (`+=`, `-=`, `*=`, `/=`)
- [x] 문자열 리터럴 지원 (LiteralExpr.string_value)
- [x] FunctionObject 클래스 (함수 객체)
- [x] ListObject.append(), MapObject.insert() with RC

---

## 검증된 Optional 기능

| 기능 | 문법 | 상태 |
|------|------|------|
| Force Unwrap | `expr!` | ✅ |
| Optional Chaining | `expr?.member` | ✅ |
| Nil Coalesce | `a ?? b` | ✅ |
| If Let | `if let x = expr { }` | ✅ |
| Guard Let | `guard let x = expr else { }` | ✅ |
| Type Annotation | `var x: Int?` | ✅ |
| Nil Assignment | `x = nil` | ✅ |
| Chained Coalesce | `a ?? b ?? c` | ✅ |

---

**구현 완료: 2026-01-28**
