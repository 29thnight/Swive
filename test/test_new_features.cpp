#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_vm.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace swiftscript;

// Helper function to execute code and capture output
std::string execute_code(const std::string& source) {
    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize_all();

        Parser parser(std::move(tokens));
        auto program = parser.parse();

        Compiler compiler;
        Chunk chunk = compiler.compile(program);

        VMConfig config;
        config.enable_debug = false;
        VM vm(config);

        // Capture cout output
        std::ostringstream output;
        std::streambuf* old_cout = std::cout.rdbuf(output.rdbuf());

        struct CoutRestorer {
            std::streambuf* old_buf;
            ~CoutRestorer() { std::cout.rdbuf(old_buf); }
        } restorer{old_cout};

        vm.execute(chunk);

        return output.str();
    } catch (const std::exception& e) {
        return std::string("ERROR: ") + e.what();
    }
}

// ============================================================
// 1. test Compound Assignment Operators
// ============================================================

void test_compound_assignment_plus_equal() {
    std::cout << "Test: Compound assignment += ... ";
    
    std::string source = R"(
        var x = 10
        x += 5
        print(x)
    )";
    
    std::string result = execute_code(source);
    assert(result.find("15") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_compound_assignment_minus_equal() {
    std::cout << "Test: Compound assignment -= ... ";
    
    std::string source = R"(
        var x = 20
        x -= 8
        print(x)
    )";
    
    std::string result = execute_code(source);
    assert(result.find("12") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_compound_assignment_multiply_equal() {
    std::cout << "Test: Compound assignment *= ... ";
    
    std::string source = R"(
        var x = 5
        x *= 3
        print(x)
    )";
    
    std::string result = execute_code(source);
    assert(result.find("15") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_compound_assignment_divide_equal() {
    std::cout << "Test: Compound assignment /= ... ";
    
    std::string source = R"(
        var x = 20
        x /= 4
        print(x)
    )";
    
    std::string result = execute_code(source);
    assert(result.find("5") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_compound_assignment_chained() {
    std::cout << "Test: Compound assignment chained ... ";
    
    std::string source = R"(
        var x = 10
        x += 5
        x *= 2
        x -= 10
        print(x)
    )";
    
    std::string result = execute_code(source);
    assert(result.find("20") != std::string::npos);
    
    std::cout << "PASSED\n";
}

// ============================================================
// 2. Break
// ============================================================

void test_break_in_while() {
    std::cout << "Test: Break in while loop ... ";
    
    std::string source = R"(
        var i = 0
        while i < 10 {
            if i == 5 {
                break
            }
            print(i)
            i += 1
        }
        print("done")
    )";
    
    std::string result = execute_code(source);
    assert(result.find("0") != std::string::npos);
    assert(result.find("4") != std::string::npos);
    assert(result.find("5") == std::string::npos); // 5�� ��µ��� �ʾƾ� ��
    assert(result.find("done") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_break_nested_loop() {
    std::cout << "Test: Break in nested loop ... ";
    
    std::string source = R"(
        var i = 0
        while i < 3 {
            var j = 0
            while j < 3 {
                if j == 1 {
                    break
                }
                print(j)
                j += 1
            }
            i += 1
        }
    )";
    
    std::string result = execute_code(source);
    int count = 0;
    size_t pos = 0;
    while ((pos = result.find("0", pos)) != std::string::npos) {
        count++;
        pos++;
    }
    assert(count == 3);
    
    std::cout << "PASSED\n";
}

void test_break_with_local_variables() {
    std::cout << "Test: Break with local variables cleanup ... ";
    
    std::string source = R"(
        var total = 0
        var i = 0
        while i < 10 {
            var x = i * 2
            if x > 10 {
                break
            }
            total += x
            i += 1
        }
        print(total)
    )";
    
    std::string result = execute_code(source);
    // 0+2+4+6+8+10 = 30
    assert(result.find("30") != std::string::npos);
    
    std::cout << "PASSED\n";
}

// ============================================================
// 3. Continue 
// ============================================================

void test_continue_in_while() {
    std::cout << "Test: Continue in while loop ... ";
    
    std::string source = R"(
        var i = 0
        var sum = 0
        while i < 10 {
            i += 1
            if i % 2 == 0 {
                continue
            }
            sum += i
        }
        print(sum)
    )";
    
    std::string result = execute_code(source);
    // Ȧ���� ��: 1+3+5+7+9 = 25
    assert(result.find("25") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_continue_nested_loop() {
    std::cout << "Test: Continue in nested loop ... ";
    
    std::string source = R"(
        var i = 0
        var count = 0
        while i < 3 {
            var j = 0
            while j < 3 {
                j += 1
                if j == 2 {
                    continue
                }
                count += 1
            }
            i += 1
        }
        print(count)
    )";
    
    std::string result = execute_code(source);
    // 3 * 2 = 6
    assert(result.find("6") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_continue_with_local_variables() {
    std::cout << "Test: Continue with local variables cleanup ... ";
    
    std::string source = R"(
        var i = 0
        var total = 0
        while i < 5 {
            i += 1
            var x = i * 10
            if i % 2 == 0 {
                continue
            }
            total += x
        }
        print(total)
    )";
    
    std::string result = execute_code(source);
    // 10 + 30 + 50 = 90
    assert(result.find("90") != std::string::npos);
    
    std::cout << "PASSED\n";
}

// ============================================================
// 4. For-In 
// ============================================================

void test_for_in_range_inclusive() {
    std::cout << "Test: For-in with inclusive range ... ";
    
    std::string source = R"(
        var sum = 0
        for i in 1...5 {
            sum += i
        }
        print(sum)
    )";
    
    std::string result = execute_code(source);
    // 1+2+3+4+5 = 15
    assert(result.find("15") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_for_in_range_exclusive() {
    std::cout << "Test: For-in with exclusive range ... ";
    
    std::string source = R"(
        var sum = 0
        for i in 1..<5 {
            sum += i
        }
        print(sum)
    )";
    
    std::string result = execute_code(source);
    // 1+2+3+4 = 10
    assert(result.find("10") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_for_in_with_break() {
    std::cout << "Test: For-in with break ... ";
    
    std::string source = R"(
        var sum = 0
        for i in 1...10 {
            if i > 5 {
                break
            }
            sum += i
        }
        print(sum)
    )";
    
    std::string result = execute_code(source);
    // 1+2+3+4+5 = 15
    assert(result.find("15") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_for_in_with_continue() {
    std::cout << "Test: For-in with continue ... ";
    
    std::string source = R"(
        var sum = 0
        for i in 1...10 {
            if i % 2 == 0 {
                continue
            }
            sum += i
        }
        print(sum)
    )";
    
    std::string result = execute_code(source);
    // 1+3+5+7+9 = 25
    assert(result.find("25") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_for_in_nested() {
    std::cout << "Test: Nested for-in loops ... ";
    
    std::string source = R"(
        var sum = 0
        for i in 1...3 {
            for j in 1...3 {
                sum += i * j
            }
        }
        print(sum)
    )";
    
    std::string result = execute_code(source);
    // (1*1+1*2+1*3) + (2*1+2*2+2*3) + (3*1+3*2+3*3) = 6+12+18 = 36
    assert(result.find("36") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_for_in_with_local_scope() {
    std::cout << "Test: For-in with local scope ... ";
    
    std::string source = R"(
        var total = 0
        for i in 1...5 {
            var doubled = i * 2
            total += doubled
        }
        print(total)
    )";
    
    std::string result = execute_code(source);
    // 2+4+6+8+10 = 30
    assert(result.find("30") != std::string::npos);
    
    std::cout << "PASSED\n";
}

// ============================================================
// 5. Complex Scenarios
// ============================================================

void test_complex_scenario_1() {
    std::cout << "Test: Complex scenario - compound assignment in loop ... ";
    
    std::string source = R"(
        var result = 1
        for i in 1...5 {
            result *= i
        }
        print(result)
    )";
    
    std::string result = execute_code(source);
    // 1*2*3*4*5 = 120 (factorial)
    assert(result.find("120") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_complex_scenario_2() {
    std::cout << "Test: Complex scenario - break and continue together ... ";
    
    std::string source = R"(
        var sum = 0
        for i in 1...20 {
            if i > 15 {
                break
            }
            if i % 3 == 0 {
                continue
            }
            sum += i
        }
        print(sum)
    )";
    
    std::string result = execute_code(source);
	// 1~15 but skip multiples of 3
    // 1+2+4+5+7+8+10+11+13+14 = 75
    assert(result.find("75") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_complex_scenario_3() {
    std::cout << "Test: Complex scenario - nested loops with break/continue ... ";
    
    std::string source = R"(
        var count = 0
        for i in 1...5 {
            for j in 1...5 {
                if i == j {
                    continue
                }
                if i + j > 7 {
                    break
                }
                count += 1
            }
        }
        print(count)
    )";
    
    std::string result = execute_code(source);
	// Valid pairs (i,j): (1,2),(1,3),(1,4),(1,5),(2,1),(2,3),(2,4),(2,5),
	// (3,1),(3,2),(3,4),(3,5),(4,1),(4,2),(4,3),(5,1),(5,2)
    assert(result.find("ERROR") == std::string::npos);
    
    std::cout << "PASSED\n";
}

// ============================================================
// 6. 배열 리터럴 테스트
// ============================================================

void test_array_literal_empty() {
    std::cout << "Test: Empty array literal ... ";

    std::string source = R"(
        var arr = []
        print("created")
    )";

    std::string result = execute_code(source);
    assert(result.find("created") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_array_literal_integers() {
    std::cout << "Test: Array literal with integers ... ";

    std::string source = R"(
        var arr = [1, 2, 3, 4, 5]
        print(arr[0])
        print(arr[2])
        print(arr[4])
    )";

    std::string result = execute_code(source);
    assert(result.find("1") != std::string::npos);
    assert(result.find("3") != std::string::npos);
    assert(result.find("5") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_array_literal_mixed() {
    std::cout << "Test: Array literal with mixed types ... ";

    std::string source = R"(
        var arr = [1, "hello", true, 3.14]
        print(arr[0])
        print(arr[1])
        print(arr[2])
    )";

    std::string result = execute_code(source);
    assert(result.find("1") != std::string::npos);
    assert(result.find("hello") != std::string::npos);
    assert(result.find("true") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_array_subscript_get() {
    std::cout << "Test: Array subscript get ... ";

    std::string source = R"(
        var numbers = [10, 20, 30, 40, 50]
        var first = numbers[0]
        var third = numbers[2]
        var last = numbers[4]
        print(first)
        print(third)
        print(last)
    )";

    std::string result = execute_code(source);
    assert(result.find("10") != std::string::npos);
    assert(result.find("30") != std::string::npos);
    assert(result.find("50") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_array_subscript_expression_index() {
    std::cout << "Test: Array subscript with expression index ... ";

    std::string source = R"(
        var arr = [100, 200, 300]
        var i = 1
        print(arr[i])
        print(arr[i + 1])
    )";

    std::string result = execute_code(source);
    assert(result.find("200") != std::string::npos);
    assert(result.find("300") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_array_in_loop() {
    std::cout << "Test: Array access in loop ... ";

    std::string source = R"(
        var arr = [1, 2, 3, 4, 5]
        var sum = 0
        for i in 0..<5 {
            sum += arr[i]
        }
        print(sum)
    )";

    std::string result = execute_code(source);
    // 1+2+3+4+5 = 15
    assert(result.find("15") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_array_nested() {
    std::cout << "Test: Nested array access ... ";

    std::string source = R"(
        var matrix = [[1, 2], [3, 4], [5, 6]]
        print(matrix[0][0])
        print(matrix[1][1])
        print(matrix[2][0])
    )";

    std::string result = execute_code(source);
    assert(result.find("1") != std::string::npos);
    assert(result.find("4") != std::string::npos);
    assert(result.find("5") != std::string::npos);

    std::cout << "PASSED\n";
}

// ============================================================
// 7. 딕셔너리 리터럴 테스트
// ============================================================

void test_dict_literal_empty() {
    std::cout << "Test: Empty dictionary literal ... ";

    std::string source = R"(
        var dict = [:]
        print("created")
    )";

    std::string result = execute_code(source);
    assert(result.find("created") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_dict_literal_string_keys() {
    std::cout << "Test: Dictionary with string keys ... ";

    std::string source = R"(
        var person = ["name": "Alice", "city": "Seoul"]
        print(person["name"])
        print(person["city"])
    )";

    std::string result = execute_code(source);
    assert(result.find("Alice") != std::string::npos);
    assert(result.find("Seoul") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_dict_literal_mixed_values() {
    std::cout << "Test: Dictionary with mixed value types ... ";

    std::string source = R"(
        var data = ["count": 42, "active": true, "name": "test"]
        print(data["count"])
        print(data["active"])
        print(data["name"])
    )";

    std::string result = execute_code(source);
    assert(result.find("42") != std::string::npos);
    assert(result.find("true") != std::string::npos);
    assert(result.find("test") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_dict_subscript_get() {
    std::cout << "Test: Dictionary subscript get ... ";

    std::string source = R"(
        var scores = ["math": 95, "english": 88, "science": 92]
        var math = scores["math"]
        var eng = scores["english"]
        print(math)
        print(eng)
    )";

    std::string result = execute_code(source);
    assert(result.find("95") != std::string::npos);
    assert(result.find("88") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_dict_missing_key() {
    std::cout << "Test: Dictionary missing key returns nil ... ";

    std::string source = R"(
        var dict = ["a": 1, "b": 2]
        var value = dict["c"]
        if value == nil {
            print("nil")
        } else {
            print("found")
        }
    )";

    std::string result = execute_code(source);
    assert(result.find("nil") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_dict_with_nil_coalesce() {
    std::cout << "Test: Dictionary with nil coalesce ... ";

    std::string source = R"(
        var config = ["timeout": 30]
        var timeout = config["timeout"] ?? 60
        var retries = config["retries"] ?? 3
        print(timeout)
        print(retries)
    )";

    std::string result = execute_code(source);
    assert(result.find("30") != std::string::npos);
    assert(result.find("3") != std::string::npos);

    std::cout << "PASSED\n";
}

// ============================================================
// 8. 배열/딕셔너리 복합 테스트
// ============================================================

void test_array_of_dicts() {
    std::cout << "Test: Array of dictionaries ... ";

    std::string source = R"(
        var users = [
            ["name": "Alice", "age": 25],
            ["name": "Bob", "age": 30]
        ]
        print(users[0]["name"])
        print(users[1]["age"])
    )";

    std::string result = execute_code(source);
    assert(result.find("Alice") != std::string::npos);
    assert(result.find("30") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_dict_with_array_values() {
    std::cout << "Test: Dictionary with array values ... ";

    std::string source = R"(
        var data = ["numbers": [1, 2, 3], "names": ["a", "b"]]
        print(data["numbers"][0])
        print(data["numbers"][2])
        print(data["names"][1])
    )";

    std::string result = execute_code(source);
    assert(result.find("1") != std::string::npos);
    assert(result.find("3") != std::string::npos);
    assert(result.find("b") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_collection_in_function() {
    std::cout << "Test: Collections in function ... ";

    std::string source = R"(
        func sumArray() -> Int {
            var arr = [10, 20, 30]
            var total = 0
            for i in 0..<3 {
                total += arr[i]
            }
            return total
        }
        print(sumArray())
    )";

    std::string result = execute_code(source);
    assert(result.find("60") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_trailing_comma() {
    std::cout << "Test: Trailing comma in literals ... ";

    std::string source = R"(
        var arr = [1, 2, 3,]
        var dict = ["a": 1, "b": 2,]
        print(arr[2])
        print(dict["b"])
    )";

    std::string result = execute_code(source);
    assert(result.find("3") != std::string::npos);
    assert(result.find("2") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_print_array() {
    std::cout << "Test: Print array directly ... ";

    std::string source = R"(
        var arr = [1, 2, 3]
        print(arr)
    )";

    std::string result = execute_code(source);
    // Should output something like [1, 2, 3]
    assert(result.find("[") != std::string::npos);
    assert(result.find("1") != std::string::npos);
    assert(result.find("2") != std::string::npos);
    assert(result.find("3") != std::string::npos);
    assert(result.find("]") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_print_dict() {
    std::cout << "Test: Print dictionary directly ... ";

    std::string source = R"(
        var dict = ["name": "test", "value": 42]
        print(dict)
    )";

    std::string result = execute_code(source);
    // Should output something like ["name": "test", "value": 42]
    assert(result.find("[") != std::string::npos);
    assert(result.find("name") != std::string::npos);
    assert(result.find("test") != std::string::npos);
    assert(result.find("value") != std::string::npos);
    assert(result.find("42") != std::string::npos);
    assert(result.find("]") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_print_nested_collection() {
    std::cout << "Test: Print nested collection ... ";

    std::string source = R"(
        var nested = [[1, 2], [3, 4]]
        print(nested)
    )";

    std::string result = execute_code(source);
    assert(result.find("ERROR") == std::string::npos);
    assert(result.find("[") != std::string::npos);

    std::cout << "PASSED\n";
}

// ============================================================
// 9. 에러 케이스 테스트
// ============================================================

void test_break_outside_loop() {
    std::cout << "Test: Break outside loop (should error) ... ";
    
    std::string source = R"(
        var x = 10
        break
    )";
    
    std::string result = execute_code(source);
    assert(result.find("ERROR") != std::string::npos);
    assert(result.find("outside") != std::string::npos || 
           result.find("loop") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_continue_outside_loop() {
    std::cout << "Test: Continue outside loop (should error) ... ";
    
    std::string source = R"(
        var x = 10
        continue
    )";
    
    std::string result = execute_code(source);
    assert(result.find("ERROR") != std::string::npos);
    assert(result.find("outside") != std::string::npos || 
           result.find("loop") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_invalid_compound_assignment() {
    std::cout << "Test: Invalid compound assignment (should error) ... ";
    
    std::string source = R"(
        var x = "hello"
        x += 5
    )";
    
    std::string result = execute_code(source);
    // Ÿ�� ������ �߻��ؾ� �� (��Ÿ�ӿ���)
    assert(result.find("ERROR") != std::string::npos || result.length() > 0);
    
    std::cout << "PASSED\n";
}

// ============================================================
// Main test runner
// ============================================================

int main() {
    std::cout << "======================================\n";
    std::cout << "  NEW FEATURES TEST SUITE\n";
    std::cout << "======================================\n\n";

    try {
        std::cout << "--- Compound Assignment Operators ---\n";
        test_compound_assignment_plus_equal();
        test_compound_assignment_minus_equal();
        test_compound_assignment_multiply_equal();
        test_compound_assignment_divide_equal();
        test_compound_assignment_chained();
        std::cout << "\n";

        std::cout << "--- Break Statement ---\n";
        test_break_in_while();
        test_break_nested_loop();
        test_break_with_local_variables();
        std::cout << "\n";

        std::cout << "--- Continue Statement ---\n";
        test_continue_in_while();
        test_continue_nested_loop();
        test_continue_with_local_variables();
        std::cout << "\n";

        std::cout << "--- For-In Loop ---\n";
        test_for_in_range_inclusive();
        test_for_in_range_exclusive();
        test_for_in_with_break();
        test_for_in_with_continue();
        test_for_in_nested();
        test_for_in_with_local_scope();
        std::cout << "\n";

        std::cout << "--- Complex Scenarios ---\n";
        test_complex_scenario_1();
        test_complex_scenario_2();
        test_complex_scenario_3();
        std::cout << "\n";

        std::cout << "--- Array Literals ---\n";
        test_array_literal_empty();
        test_array_literal_integers();
        test_array_literal_mixed();
        test_array_subscript_get();
        test_array_subscript_expression_index();
        test_array_in_loop();
        test_array_nested();
        std::cout << "\n";

        std::cout << "--- Dictionary Literals ---\n";
        test_dict_literal_empty();
        test_dict_literal_string_keys();
        test_dict_literal_mixed_values();
        test_dict_subscript_get();
        test_dict_missing_key();
        test_dict_with_nil_coalesce();
        std::cout << "\n";

        std::cout << "--- Collection Complex ---\n";
        test_array_of_dicts();
        test_dict_with_array_values();
        test_collection_in_function();
        test_trailing_comma();
        test_print_array();
        test_print_dict();
        test_print_nested_collection();
        std::cout << "\n";

        std::cout << "--- Error Cases ---\n";
        test_break_outside_loop();
        test_continue_outside_loop();
        test_invalid_compound_assignment();
        std::cout << "\n";

        std::cout << "======================================\n";
        std::cout << "  ALL TESTS PASSED!\n";
        std::cout << "======================================\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n======================================\n";
        std::cerr << "  TEST FAILED!\n";
        std::cerr << "  Error: " << e.what() << "\n";
        std::cerr << "======================================\n";
        return 1;
    }
}