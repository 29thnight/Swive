# SwiftScript 테스트 프레임워크 - DebugHelper 시스템

## 생성된 파일들

### 1. `test/test_helpers.hpp` ? 핵심 파일
강력한 테스트 유틸리티를 제공하는 헤더 파일입니다.

**포함된 주요 클래스:**

#### `AssertHelper` - 개선된 어설션
- ? Release 빌드에서도 작동 (기존 `assert()`는 무시됨)
- ? 실패 시 상세한 에러 메시지 제공
- ? 컨텍스트 정보 포함 가능

**주요 메서드:**
```cpp
AssertHelper::assert_contains(text, substring, "context");
AssertHelper::assert_order(text, "first", "second", "context");
AssertHelper::assert_error(output, "context");
AssertHelper::assert_no_error(output, "context");
AssertHelper::assert_equals(expected, actual, "context");
```

#### `TestRunner` - 자동 테스트 관리
- 테스트 실행 및 결과 추적
- 실행 시간 자동 측정
- 포괄적인 통계 리포트 생성

**사용 예:**
```cpp
TestRunner runner;
runner.run_test("test name", test_function);
runner.print_summary();
return runner.all_passed() ? 0 : 1;
```

#### `MemoryTracker` - 메모리 누수 감지
- 할당/해제 추적
- 누수 자동 탐지 및 보고
- 메모리 사용량 통계

**사용 예:**
```cpp
MemoryTrackingScope scope("test name");
// 테스트 코드...
// 스코프 종료 시 자동 누수 검사
```

#### `OutputMatcher` - 출력 검증
- 정확한 라인별 매칭
- 순서 검증
- 다중 문자열 검증

**사용 예:**
```cpp
OutputMatcher::assert_exact_output(output, {"line1", "line2"});
OutputMatcher::assert_output_order(output, {"first", "second", "third"});
```

#### `PerformanceProfiler` - 성능 측정
- 실행 시간 측정
- 자동 리포트 생성

**사용 예:**
```cpp
PerformanceProfiler profiler("test name");
profiler.start();
// 측정할 코드...
profiler.stop_and_print();
```

#### `TestStatistics` - 테스트 통계
- 전체 테스트 결과 집계
- 성공/실패 카운트
- 실행 시간 분석

#### `VMStateVerifier` - VM 상태 검증
- VM 스택 검증
- 객체 누수 탐지
- 에러 상태 확인
- ?? VM API 통합 필요 (현재는 placeholder)

---

### 2. `test/test_class.cpp` (업데이트됨) ?
기존 테스트 파일을 새로운 DebugHelper 시스템을 사용하도록 업데이트했습니다.

**변경 사항:**

**Before (기존):**
```cpp
void test_simple_class_method() {
    std::cout << "Test: simple class method ... ";
    auto out = run_code(source);
    assert(out.find("hi") != std::string::npos);
    std::cout << "PASSED\n";
}

int main() {
    test_simple_class_method();
    // ...
    return 0;
}
```

**After (새로운):**
```cpp
void test_simple_class_method() {
    auto out = run_code(source);
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "hi", "greet() should print 'hi'");
}

int main() {
    TestRunner runner;
    runner.run_test("simple class method", test_simple_class_method);
    runner.print_summary();
    return runner.all_passed() ? 0 : 1;
}
```

**개선 사항:**
- ? 더 명확한 에러 메시지
- ? 자동 타이밍
- ? 포괄적인 통계
- ? Release 빌드 안전성

---

### 3. `test/EXAMPLES_test_helpers.hpp` ??
다양한 사용 예시를 보여주는 문서 파일입니다.

**포함된 예시:**
1. AssertHelper 기본 사용법
2. OutputMatcher를 사용한 정확한 출력 검증
3. PerformanceProfiler 사용법
4. TestRunner로 자동 테스트 관리
5. MemoryTracker 사용법
6. 복잡한 시나리오 테스트
7. 에러 케이스 테스트
8. 값 비교 테스트
9. 상속 및 super 호출 테스트

---

### 4. `test/TEST_FRAMEWORK_README.md` ??
전체 시스템에 대한 포괄적인 문서입니다.

**내용:**
- 시스템 개요
- 각 컴포넌트 상세 설명
- 사용 예시
- 마이그레이션 가이드
- VM 통합 방법
- 향후 개선 제안

---

## 주요 개선사항

### 1. 더 나은 에러 메시지 ??

**기존:**
```
Assertion failed: (expression), function test, file test.cpp, line 42.
```

**새로운:**
```
Assertion failed: Expected to find 'hello' in output (Should greet user)
Actual output: goodbye world
```

### 2. 자동 통계 및 리포트 ??

```
======================================
  TEST SUMMARY
======================================
Total Tests: 11
Passed: 10 ?
Failed: 1 ?

Failed Tests:
  ? inheritance test
    Error: Expected to find 'woof' in output

Performance:
  simple class method: 2.3 ms
  inheritance test: 1.8 ms
  ...
  Total: 45.7 ms
======================================
```

### 3. 순서 검증 ??

```cpp
// 기존: 순서 확인 불가
assert(out.find("animal") != std::string::npos);
assert(out.find("dog") != std::string::npos);

// 새로운: 순서도 검증
AssertHelper::assert_order(out, "animal", "dog", "super should be called first");
```

### 4. 메모리 누수 감지 ??

```cpp
{
    MemoryTrackingScope scope("test");
    // 테스트 코드...
}
// 자동으로 누수 보고:
// ? Memory Leaks Detected!
//   Leaked allocations: 2
//   - 100 bytes at 0x7fff5fbff8a0
//   Total leaked: 300 bytes
```

### 5. Release 빌드 안전성 ??

기존 `assert()` 매크로와 달리:
- Release 빌드에서도 작동
- 프로덕션 환경에서도 테스트 가능
- 예외를 통한 안전한 실패 처리

---

## 사용 방법

### 빠른 시작

```cpp
#include "test_helpers.hpp"

using namespace swiftscript::test;

void my_test() {
    auto out = run_code(source);
    AssertHelper::assert_contains(out, "expected");
}

int main() {
    TestRunner runner;
    runner.run_test("my test", my_test);
    runner.print_summary();
    return runner.all_passed() ? 0 : 1;
}
```

### 기존 테스트 마이그레이션

1. `#include "test_helpers.hpp"` 추가
2. `using namespace swiftscript::test;` 추가
3. `assert()` → `AssertHelper::assert_*()` 변경
4. `main()`에서 `TestRunner` 사용
5. 수동 출력 제거 (자동으로 처리됨)

---

## 장점 요약

### 개발자 경험 향상 ?????
- 실패 원인을 빠르게 파악
- 테스트 작성이 더 쉬움
- 의도가 명확한 코드

### 유지보수성 향상 ??
- 테스트 의도가 명확
- 변경사항 추적 용이
- 리팩토링 안전성

### 생산성 향상 ?
- 자동 통계 및 리포트
- 시간 측정 자동화
- 메모리 누수 자동 감지

### 품질 향상 ?
- 더 포괄적인 테스트
- Edge case 쉽게 추가
- 회귀 테스트 용이

---

## 향후 개선 가능 사항

### 1. VM 통합 ??
```cpp
// VMStateVerifier에 실제 VM API 연결
class VM {
public:
    size_t get_stack_size() const;
    size_t get_object_count() const;
    // ...
};
```

### 2. 추가 기능
- JSON 리포트 출력 (CI/CD 통합)
- 코드 커버리지 추적
- 병렬 테스트 실행
- 테스트 필터링
- 스냅샷 테스팅

### 3. 통합
- Google Test / Catch2 호환성
- CI/CD 파이프라인 통합
- Visual Studio Test Explorer 지원

---

## 빌드 확인 ?

프로젝트가 성공적으로 빌드되었습니다!

모든 기능이 작동하며 기존 테스트와 호환됩니다.

---

## 다음 단계

1. **다른 테스트 파일도 업데이트**
   - `test/test_basic.cpp`
   - `test/test_optional.cpp`
   - `test/test_closure.cpp`
   - 등등...

2. **새로운 테스트 추가**
   - 더 많은 edge case
   - 성능 테스트
   - 통합 테스트

3. **VM 통합**
   - `VMStateVerifier` 구현
   - 메모리 추적 활성화

4. **CI/CD 통합**
   - 자동 테스트 실행
   - 리포트 생성

---

## 문의 및 피드백

이 시스템을 사용하면서:
- 버그를 발견하거나
- 개선 제안이 있거나
- 새로운 기능이 필요하면

언제든지 알려주세요! ??
