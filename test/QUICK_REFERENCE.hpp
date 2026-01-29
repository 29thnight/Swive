// =====================================================================
// SwiftScript Test Framework - Quick Reference Card
// DebugHelper 시스템 빠른 참조 카드
// =====================================================================

#pragma once

/*

// ====================================================================
// 1. SETUP - 초기 설정
// ====================================================================

#include "test_helpers.hpp"
using namespace swiftscript::test;

// ====================================================================
// 2. ASSERTIONS - 어설션 (AssertHelper)
// ====================================================================

// 기본 검증
AssertHelper::assert_true(condition, "message");
AssertHelper::assert_false(condition, "message");

// 문자열 검증
AssertHelper::assert_contains(text, "substring", "context");
AssertHelper::assert_not_contains(text, "substring", "context");
AssertHelper::assert_equals("expected", actual, "context");

// 순서 검증
AssertHelper::assert_order(text, "first", "second", "context");

// 에러 검증
AssertHelper::assert_error(output, "context");
AssertHelper::assert_no_error(output, "context");

// 값 비교
AssertHelper::assert_equals(expected, actual, "context");
AssertHelper::assert_not_equals(val1, val2, "context");
AssertHelper::assert_greater(value, min, "context");
AssertHelper::assert_less(value, max, "context");

// ====================================================================
// 3. OUTPUT MATCHING - 출력 검증 (OutputMatcher)
// ====================================================================

// 정확한 출력 매칭
OutputMatcher::assert_exact_output(output, {
    "line1",
    "line2",
    "line3"
});

// 모든 문자열 포함 확인
OutputMatcher::assert_contains_all(output, {
    "string1",
    "string2",
    "string3"
});

// 순서 확인
OutputMatcher::assert_output_order(output, {
    "first",
    "second",
    "third"
});

// 라인 분할
auto lines = OutputMatcher::split_lines(output);

// ====================================================================
// 4. TEST RUNNER - 테스트 실행 (TestRunner)
// ====================================================================

TestRunner runner;

// 자동 테스트 실행
runner.run_test("test name", test_function);

// 수동 테스트 관리
runner.begin_test("manual test");
try {
    // test code
    runner.end_test(true);
} catch (const std::exception& e) {
    runner.end_test(false, e.what());
}

// 결과 확인
runner.print_summary();
return runner.all_passed() ? 0 : 1;

// ====================================================================
// 5. PERFORMANCE - 성능 측정 (PerformanceProfiler)
// ====================================================================

// 자동 측정 (RAII)
{
    PerformanceProfiler profiler("operation name");
    profiler.start();
    // 측정할 코드...
    profiler.stop_and_print();  // 자동 출력
}

// 수동 측정
PerformanceProfiler profiler("test");
profiler.start();
// 코드...
double ms = profiler.stop_and_get_ms();

// 성능 검증
AssertHelper::assert_less(ms, 1000.0, "Should complete in under 1s");

// ====================================================================
// 6. MEMORY TRACKING - 메모리 추적 (MemoryTracker)
// ====================================================================

// 자동 추적 (RAII) - 권장!
{
    MemoryTrackingScope scope("test name");
    // 테스트 코드...
    // 스코프 종료 시 자동 누수 검사
}

// 수동 추적
auto& tracker = MemoryTracker::instance();
tracker.enable_tracking();
tracker.track_allocation(ptr, size, "location");
tracker.track_deallocation(ptr);
tracker.print_leak_report();
tracker.print_memory_stats();

// ====================================================================
// 7. COMPLETE TEST TEMPLATE - 완전한 테스트 템플릿
// ====================================================================

#include "test_helpers.hpp"
using namespace swiftscript::test;

void test_example() {
    std::string source = R"(
        // SwiftScript code here
    )";
    
    auto out = run_code(source);
    
    // Assertions
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "expected");
}

int main() {
    TestRunner runner;
    
    // Run tests
    runner.run_test("example test", test_example);
    runner.run_test("another test", test_another);
    
    // Print summary
    runner.print_summary();
    
    // Memory report (if tracking enabled)
    auto& mem = MemoryTracker::instance();
    if (mem.is_tracking()) {
        mem.print_memory_stats();
        mem.print_leak_report();
    }
    
    return runner.all_passed() ? 0 : 1;
}

// ====================================================================
// 8. COMMON PATTERNS - 자주 사용하는 패턴
// ====================================================================

// Pattern 1: 기본 출력 검증
void test_basic_output() {
    auto out = run_code("print(42)");
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "42");
}

// Pattern 2: 순서 검증
void test_execution_order() {
    auto out = run_code(source);
    AssertHelper::assert_order(out, "first", "second");
    // 또는
    OutputMatcher::assert_output_order(out, {"a", "b", "c"});
}

// Pattern 3: 에러 테스트
void test_error_case() {
    auto out = run_code(invalid_source);
    AssertHelper::assert_error(out, "Should produce error");
}

// Pattern 4: 정확한 출력
void test_exact_output() {
    auto out = run_code(source);
    OutputMatcher::assert_exact_output(out, {
        "expected line 1",
        "expected line 2"
    });
}

// Pattern 5: 성능 테스트
void test_performance() {
    PerformanceProfiler profiler("loop test");
    profiler.start();
    
    auto out = run_code(source_with_loop);
    
    double ms = profiler.stop_and_get_ms();
    AssertHelper::assert_less(ms, 100.0, "Should be fast");
}

// Pattern 6: 메모리 안전성
void test_no_leaks() {
    MemoryTrackingScope scope("leak test");
    auto out = run_code(source_with_objects);
    AssertHelper::assert_no_error(out);
    // 자동 누수 검사
}

// Pattern 7: 복잡한 검증
void test_complex() {
    auto out = run_code(source);
    
    // 여러 검증
    AssertHelper::assert_no_error(out);
    OutputMatcher::assert_contains_all(out, {"a", "b", "c"});
    OutputMatcher::assert_output_order(out, {"a", "b", "c"});
    
    // 라인별 확인
    auto lines = OutputMatcher::split_lines(out);
    AssertHelper::assert_equals(size_t(3), lines.size());
    AssertHelper::assert_equals("a", lines[0]);
}

// ====================================================================
// 9. MIGRATION CHECKLIST - 마이그레이션 체크리스트
// ====================================================================

// [ ] 1. #include "test_helpers.hpp" 추가
// [ ] 2. using namespace swiftscript::test; 추가
// [ ] 3. assert() → AssertHelper::assert_*() 변경
// [ ] 4. std::cout 수동 출력 제거
// [ ] 5. TestRunner 사용
// [ ] 6. main()에서 runner.print_summary() 호출
// [ ] 7. 빌드 및 테스트 확인

// ====================================================================
// 10. TROUBLESHOOTING - 문제 해결
// ====================================================================

// 문제: 컴파일 에러 - 'AssertHelper' not found
// 해결: #include "test_helpers.hpp" 추가

// 문제: 링크 에러 - multiple definition of main
// 해결: 각 테스트 파일에 하나의 main()만 있어야 함

// 문제: assert_order 실패
// 해결: 첫 번째 문자열이 두 번째보다 먼저 나타나는지 확인

// 문제: 메모리 추적이 작동하지 않음
// 해결: VM integration 필요 (현재는 placeholder)

// 문제: 성능 측정이 0ms로 나옴
// 해결: start() 호출 확인, 측정 코드가 너무 빠름

// ====================================================================
// 11. BEST PRACTICES - 모범 사례
// ====================================================================

// ? DO: 의미있는 context 메시지 사용
AssertHelper::assert_contains(out, "hello", "Should greet user");

// ? DON'T: context 없이 사용
AssertHelper::assert_contains(out, "hello", "");

// ? DO: 순서 검증 사용
AssertHelper::assert_order(out, "init", "method");

// ? DON'T: 순서 무시
assert(out.find("init") != npos);
assert(out.find("method") != npos);

// ? DO: TestRunner 사용
runner.run_test("test name", test_func);

// ? DON'T: 수동 try-catch
try { test_func(); } catch(...) { }

// ? DO: 정확한 출력 검증
OutputMatcher::assert_exact_output(out, expected_lines);

// ? DON'T: 부분 문자열만 확인
assert(out.find("1") != npos);  // "10", "21" 등도 매칭됨

// ====================================================================
// 12. QUICK EXAMPLES - 빠른 예제
// ====================================================================

// Example: 기본 테스트
runner.run_test("basic", []() {
    auto out = run_code("print(1)");
    AssertHelper::assert_contains(out, "1");
});

// Example: 에러 테스트
runner.run_test("error", []() {
    auto out = run_code("invalid code");
    AssertHelper::assert_error(out);
});

// Example: 순서 테스트
runner.run_test("order", []() {
    auto out = run_code("print(1); print(2)");
    AssertHelper::assert_order(out, "1", "2");
});

// Example: 성능 테스트
runner.run_test("perf", []() {
    PerformanceProfiler p("test");
    p.start();
    run_code(source);
    AssertHelper::assert_less(p.stop_and_get_ms(), 100.0);
});

// ====================================================================

*/
