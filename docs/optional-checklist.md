# Optional 타입 시스템 구현 체크리스트

## 1단계: 토큰 확장
- [x] `ss_token.hpp` — NilCoalesce(`??`), OptionalChain(`?.`), Nil, Guard 토큰 추가
- [x] `ss_token.cpp` — kTokenTypeNames 배열 업데이트
- [x] `ss_token.cpp` — keyword_type() 맵에 nil, guard 추가
- [x] `ss_token.cpp` — operator_precedence() 업데이트
- [x] 빌드 확인

## 2단계: 렉서
- [ ] `ss_lexer.hpp` — Lexer 클래스 정의
- [ ] `ss_lexer.cpp` — 렉서 구현 (숫자, 문자열, 식별자, 연산자)
- [ ] `ss_lexer.cpp` — `?` / `??` / `?.` 멀티캐릭터 토큰 처리
- [ ] `ss_lexer.cpp` — `.` / `..` / `...` 구분 처리
- [ ] `ss_lexer.cpp` — `!` / `!=` 구분 처리
- [ ] 빌드 확인

## 3단계: AST 노드
- [ ] `ss_ast.hpp` — TypeAnnotation 구조체
- [ ] `ss_ast.hpp` — 기본 Expr 노드 (Literal, Identifier, Unary, Binary, Assign, Call, Member)
- [ ] `ss_ast.hpp` — Optional Expr 노드 (ForceUnwrap, OptionalChain, NilCoalesce)
- [ ] `ss_ast.hpp` — 기본 Stmt 노드 (ExprStmt, PrintStmt, Block, Return, If, While)
- [ ] `ss_ast.hpp` — 선언 Stmt 노드 (VarDecl, FuncDecl)
- [ ] `ss_ast.hpp` — Optional Stmt 노드 (IfLet, GuardLet)
- [ ] 빌드 확인

## 4단계: 파서
- [ ] `ss_parser.hpp` — Parser 클래스 정의
- [ ] `ss_parser.cpp` — 유틸리티 (advance, peek, match, consume, error)
- [ ] `ss_parser.cpp` — primary/postfix/unary 파싱
- [ ] `ss_parser.cpp` — 이항 연산자 파싱 (산술, 비교, 논리)
- [ ] `ss_parser.cpp` — nil coalesce (`??`) 파싱 (right-associative)
- [ ] `ss_parser.cpp` — assignment 파싱
- [ ] `ss_parser.cpp` — var/let 선언 + 타입 어노테이션 파싱
- [ ] `ss_parser.cpp` — if / if let 문 파싱
- [ ] `ss_parser.cpp` — guard let 문 파싱
- [ ] `ss_parser.cpp` — while, return, print, block 파싱
- [ ] `ss_parser.cpp` — func 선언 파싱
- [ ] 빌드 확인

## 5단계: 바이트코드 & 컴파일러
- [ ] `ss_chunk.hpp` — OpCode enum 정의
- [ ] `ss_chunk.hpp` — Chunk 구조체 (code, constants, lines)
- [ ] `ss_compiler.hpp` — Compiler 클래스 정의
- [ ] `ss_compiler.cpp` — Expression 컴파일 (Literal, Identifier, Unary, Binary)
- [ ] `ss_compiler.cpp` — Optional Expression 컴파일 (ForceUnwrap, NilCoalesce, OptionalChain)
- [ ] `ss_compiler.cpp` — Statement 컴파일 (VarDecl, If, While, Block, Print, Return)
- [ ] `ss_compiler.cpp` — Optional Statement 컴파일 (IfLet, GuardLet)
- [ ] `ss_compiler.cpp` — 스코프 관리 (begin_scope, end_scope, resolve_local)
- [ ] `ss_compiler.cpp` — FuncDecl 컴파일
- [ ] 빌드 확인

## 6단계: VM 인터프리터 확장
- [ ] `ss_vm.hpp` — Chunk*, ip_, interpret(), execute(), run() 선언 추가
- [ ] `ss_vm.cpp` — read_byte(), read_short(), read_constant() 구현
- [ ] `ss_vm.cpp` — 디스패치 루프: 상수/스택 ops (CONSTANT, NIL, TRUE, FALSE, POP)
- [ ] `ss_vm.cpp` — 디스패치 루프: 산술 ops (ADD, SUB, MUL, DIV, MOD, NEGATE)
- [ ] `ss_vm.cpp` — 디스패치 루프: 비교/논리 ops
- [ ] `ss_vm.cpp` — 디스패치 루프: 변수 ops (GET/SET_GLOBAL, GET/SET_LOCAL)
- [ ] `ss_vm.cpp` — 디스패치 루프: 제어 흐름 (JUMP, JUMP_IF_FALSE, LOOP)
- [ ] `ss_vm.cpp` — 디스패치 루프: Optional ops (UNWRAP, JUMP_IF_NIL, NIL_COALESCE, OPTIONAL_CHAIN)
- [ ] `ss_vm.cpp` — 디스패치 루프: PRINT, CALL, RETURN, HALT
- [ ] `ss_vm.cpp` — interpret() 통합 (source → lexer → parser → compiler → execute)
- [ ] 빌드 확인

## 7단계: 테스트 & 통합
- [ ] `test/test_optional.cpp` — nil coalesce 테스트
- [ ] `test/test_optional.cpp` — force unwrap 테스트 (성공/실패)
- [ ] `test/test_optional.cpp` — if let 테스트
- [ ] `test/test_optional.cpp` — guard let 테스트
- [ ] `test/test_optional.cpp` — optional chaining 테스트
- [ ] `test/test_optional.cpp` — 복합 시나리오 테스트
- [ ] `SwiftScript.vcxproj` — 신규 파일 프로젝트에 등록
- [ ] 전체 빌드 확인 (Debug x64)
- [ ] 기존 test_basic.cpp 테스트 통과 확인
- [ ] test_optional.cpp 실행 및 전체 통과 확인
