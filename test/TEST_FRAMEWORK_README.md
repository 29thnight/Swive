# SwiftScript Test Framework - Debug Helper System

이 문서는 SwiftScript 프로젝트를 위한 테스트 프레임워크의 DebugHelper 시스템에 대해 설명합니다.

## 개요 (Overview)

DebugHelper 시스템은 다음 기능들을 제공합니다:

1. **Memory Leak Detection** - 메모리 누수 감지
2. **Enhanced Assertions** - 개선된 어설션 (에러 메시지 포함)
3. **Test Result Tracking** - 테스트 결과 추적 및 통계
4. **Performance Profiling** - 성능 프로파일링
5. **Output Verification** - 출력 검증 도구
6. **VM State Verification** - VM 상태 검증 (확장 가능)

## 주요 컴포넌트 (Main Components)

### 1. AssertHelper - 강화된 어설션

기존 `assert()` 매크로의 문제점:
- Release 빌드에서 무시됨 (`NDEBUG`)
- 실패 시 의미있는 에러 메시지 없음
- 디버깅이 어려움

**개선된 솔루션:**

```cpp
// 기존 방식 (나쁨)
assert(out.find("hello") != std::string::npos);  // 실패 시 정보 없음

// 새로운 방식 (좋음)
AssertHelper::assert_contains(out, "hello", "Should print hello message");
// 실패 시: "Expected to find 'hello' in output (Should print hello message)
//           Actual output: ..."
```

**사용 가능한 메서드:**

```cpp
// 기본 검증
AssertHelper::assert_true(condition, "message");
AssertHelper::assert_false(condition, "message");

// 문자열 검증
AssertHelper::assert_contains(text, substring, "context");
AssertHelper::assert_not_contains(text, substring, "context");
AssertHelper::assert_equals(expected, actual, "context");

// 순서 검증
AssertHelper::assert_order(text, "first", "second", "context");
// "first"가 "second"보다 먼저 나타나는지 확인

// 에러 검증
AssertHelper::assert_error(output, "context");
AssertHelper::assert_no_error(output, "context");

// 값 비교
AssertHelper::assert_equals(expected_value, actual_value, "context");
AssertHelper::assert_not_equals(value1, value2, "context");
AssertHelper::assert_greater(value, min, "context");
AssertHelper::assert_less(value, max, "context");
```

### 2. TestRunner - 자동 테스트 관리

테스트 실행, 타이밍, 결과를 자동으로 추적합니다.

```cpp
TestRunner runner;

// 테스트 함수 등록 및 실행
runner.run_test("test name", test_function);

// 개별 테스트 관리
runner.begin_test("manual test");
try {
    // test code
    runner.end_test(true);  // passed
} catch (const std::exception& e) {
    runner.end_test(false, e.what());  // failed
}

// 요약 출력
runner.print_summary();

// 결과 확인
bool success = runner.all_passed();
```

**출력 예시:**
```
Test: simple class method ... ? PASSED (2.3 ms)
Test: inheritance test ... ? FAILED (1.8 ms)
  Error: Expected to find 'woof' in output

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

### 3. MemoryTracker - 메모리 누수 감지

메모리 할당/해제를 추적하여 누수를 감지합니다.

```cpp
// 자동 추적 (RAII)
{
    MemoryTrackingScope scope("test name");
    // 테스트 코드...
    // 스코프 종료 시 자동으로 누수 보고
}

// 수동 추적
auto& tracker = MemoryTracker::instance();
tracker.enable_tracking();

// 할당 추적
void* ptr = malloc(100);
tracker.track_allocation(ptr, 100, "test allocation");

// 해제 추적
tracker.track_deallocation(ptr);
free(ptr);

// 보고서
tracker.print_leak_report();
tracker.print_memory_stats();
```

**출력 예시:**
```
Memory Statistics:
  Total allocated: 1024 bytes
  Total freed: 1024 bytes
  Peak usage: 512 bytes
  Current usage: 0 bytes

? No memory leaks detected
```

또는 누수 발견 시:
```
? Memory Leaks Detected!
  Leaked allocations: 2
  - 100 bytes at 0x7fff5fbff8a0 (test allocation)
  - 200 bytes at 0x7fff5fbff9b0
  Total leaked: 300 bytes
```

### 4. OutputMatcher - 출력 검증

정확한 출력 검증을 위한 도구입니다.

```cpp
std::string output = run_code(source);

// 정확한 라인별 매칭
OutputMatcher::assert_exact_output(output, {
    "Line 1",
    "Line 2",
    "Line 3"
});

// 모든 문자열 포함 확인
OutputMatcher::assert_contains_all(output, {
    "hello",
    "world",
    "test"
});

// 순서 확인
OutputMatcher::assert_output_order(output, {
    "first",
    "second",
    "third"
});

// 라인 분할
auto lines = OutputMatcher::split_lines(output);
AssertHelper::assert_equals(3, lines.size());
```

### 5. PerformanceProfiler - 성능 측정

코드 실행 시간을 측정합니다.

```cpp
// 자동 측정 (RAII)
{
    PerformanceProfiler profiler("test name");
    profiler.start();
    // 측정할 코드...
    profiler.stop_and_print();  // 자동 출력
}

// 수동 측정
PerformanceProfiler profiler("manual test");
profiler.start();
// 코드...
double ms = profiler.stop_and_get_ms();
std::cout << "Took " << ms << " ms\n";
```

### 6. TestStatistics - 테스트 통계

테스트 결과를 집계하고 분석합니다.

```cpp
TestStatistics stats;

// 결과 추가
TestResult result{
    "test name",
    true,  // passed
    "",    // error message
    2.5    // duration in ms
};
stats.add_result(result);

// 요약 출력
stats.print_summary();

// 결과 확인
bool all_ok = stats.all_passed();
int failed = stats.get_failed_count();
```

## 사용 예시 (Usage Examples)

### 예시 1: 기본 테스트

```cpp
#include "test_helpers.hpp"

using namespace swiftscript::test;

void test_basic() {
    std::string source = R"(
        print("Hello")
        print("World")
    )";
    
    auto out = run_code(source);
    
    // 기본 검증
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "Hello");
    AssertHelper::assert_contains(out, "World");
    
    // 순서 검증
    AssertHelper::assert_order(out, "Hello", "World");
}

int main() {
    TestRunner runner;
    runner.run_test("basic test", test_basic);
    runner.print_summary();
    return runner.all_passed() ? 0 : 1;
}
```

### 예시 2: 에러 테스트

```cpp
void test_error_case() {
    std::string source = R"(
        class Dog: Animal {
            func speak() {  // Missing override
                print("bark")
            }
        }
    )";
    
    auto out = run_code(source);
    
    // 에러가 발생해야 함
    AssertHelper::assert_error(out, "Missing override should error");
    AssertHelper::assert_contains(out, "override");
}
```

### 예시 3: 성능 테스트

```cpp
void test_performance() {
    std::string source = R"(
        var sum = 0
        for i in 0..<10000 {
            sum = sum + i
        }
    )";
    
    PerformanceProfiler profiler("Loop performance");
    profiler.start();
    
    auto out = run_code(source);
    
    double ms = profiler.stop_and_get_ms();
    
    // 성능 검증
    AssertHelper::assert_less(ms, 1000.0, "Should complete in under 1 second");
}
```

### 예시 4: 메모리 추적

```cpp
void test_with_memory_tracking() {
    MemoryTrackingScope scope("Class instantiation");
    
    std::string source = R"(
        class MyClass {
            var data: Int = 42
        }
        var obj = MyClass()
    )";
    
    auto out = run_code(source);
    AssertHelper::assert_no_error(out);
    
    // 스코프 종료 시 자동으로 메모리 누수 확인
}
```

### 예시 5: 복잡한 시나리오

```cpp
void test_complex_inheritance() {
    std::string source = R"(
        class Animal {
            func speak() { print("animal") }
        }
        class Dog: Animal {
            override func speak() {
                super.speak()
                print("dog")
            }
        }
        class Puppy: Dog {
            override func speak() {
                super.speak()
                print("puppy")
            }
        }
        var p = Puppy()
        p.speak()
    )";
    
    auto out = run_code(source);
    
    // 여러 검증
    AssertHelper::assert_no_error(out);
    
    // 정확한 순서 확인
    OutputMatcher::assert_output_order(out, {
        "animal",
        "dog",
        "puppy"
    });
    
    // 또는 정확한 출력 확인
    OutputMatcher::assert_exact_output(out, {
        "animal",
        "dog",
        "puppy"
    });
}
```

## 기존 테스트 마이그레이션 (Migration Guide)

### Before (기존 방식)

```cpp
void test_something() {
    std::cout << "Test: something ... ";
    auto out = run_code(source);
    assert(out.find("expected") != std::string::npos);
    assert(out.find("ERROR") == std::string::npos);
    std::cout << "PASSED\n";
}

int main() {
    test_something();
    test_other();
    // ...
    return 0;
}
```

### After (새로운 방식)

```cpp
void test_something() {
    auto out = run_code(source);
    AssertHelper::assert_contains(out, "expected", "Should output expected value");
    AssertHelper::assert_no_error(out);
}

int main() {
    TestRunner runner;
    runner.run_test("something", test_something);
    runner.run_test("other", test_other);
    // ...
    runner.print_summary();
    return runner.all_passed() ? 0 : 1;
}
```

## 장점 (Benefits)

### 1. 더 나은 에러 메시지
```
// 기존
Assertion failed: (expression), function test_func, file test.cpp, line 42.

// 새로운
Assertion failed: Expected to find 'hello' in output (Should greet user)
Actual output: goodbye world
```

### 2. 자동 통계
- 각 테스트 실행 시간 측정
- 실패한 테스트 목록
- 전체 성공률

### 3. Release 빌드 안전성
- `assert()` 매크로와 달리 Release에서도 동작
- 프로덕션 환경에서도 테스트 가능

### 4. 유지보수성
- 테스트 의도가 명확함
- 실패 원인을 빠르게 파악
- 테스트 추가가 쉬움

### 5. 확장성
- 새로운 assertion 메서드 추가 가능
- VM integration 가능
- 커스텀 검증 로직 추가 가능

## VM Integration 확장 (Future Work)

`VMStateVerifier`를 VM과 통합하여 더 많은 검증을 수행할 수 있습니다:

```cpp
// VM API에 다음 메서드 추가 필요:
class VM {
public:
    size_t get_stack_size() const;
    size_t get_object_count() const;
    bool has_error() const;
    std::string get_error_message() const;
};

// 그러면 다음과 같이 사용 가능:
void test_with_vm_verification() {
    VM vm;
    auto before = VMStateVerifier::capture_state(vm);
    
    // 테스트 실행...
    
    auto after = VMStateVerifier::capture_state(vm);
    VMStateVerifier::assert_clean_state(after);
    VMStateVerifier::assert_no_leaks(before, after);
}
```

## 참고 사항 (Notes)

1. `test_helpers.hpp` 파일을 include하면 모든 기능을 사용할 수 있습니다
2. `swiftscript::test` 네임스페이스를 사용합니다
3. 모든 도구는 예외를 throw하여 실패를 알립니다
4. `TestRunner`가 자동으로 예외를 처리합니다

## 추가 개선 사항 제안

1. **JSON 리포트 출력** - CI/CD 통합용
2. **코드 커버리지 추적** - 어떤 코드가 테스트되었는지
3. **병렬 테스트 실행** - 속도 향상
4. **테스트 필터링** - 특정 테스트만 실행
5. **스냅샷 테스팅** - 출력을 파일로 저장하여 비교

이 시스템을 사용하면 더 robust하고 maintainable한 테스트를 작성할 수 있습니다!
