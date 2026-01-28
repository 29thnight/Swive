#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_vm.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace swiftscript;

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

void test_closure_basic() {
    std::cout << "Test: Basic closure ... ";
    
    std::string source = R"(
        var add = { (a: Int, b: Int) -> Int in
            return a + b
        }
        print(add(3, 5))
    )";
    
    std::string result = execute_code(source);
    assert(result.find("8") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_closure_no_params() {
    std::cout << "Test: Closure with no parameters ... ";
    
    std::string source = R"(
        var greet = { () -> Void in
            print("Hello!")
        }
        greet()
    )";
    
    std::string result = execute_code(source);
    assert(result.find("Hello!") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_closure_single_param() {
    std::cout << "Test: Closure with single parameter ... ";
    
    std::string source = R"(
        var double = { (x: Int) -> Int in
            return x * 2
        }
        print(double(7))
    )";
    
    std::string result = execute_code(source);
    assert(result.find("14") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_closure_as_argument() {
    std::cout << "Test: Closure passed as function argument ... ";
    
    std::string source = R"(
        func apply(value: Int, transform: (Int) -> Int) -> Int {
            return transform(value)
        }
        
        var triple = { (x: Int) -> Int in
            return x * 3
        }
        
        print(apply(5, triple))
    )";
    
    std::string result = execute_code(source);
    assert(result.find("ERROR") == std::string::npos);
    assert(result.find("15") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_closure_multiple_statements() {
    std::cout << "Test: Closure with multiple statements ... ";
    
    std::string source = R"(
        var compute = { (a: Int, b: Int) -> Int in
            var sum = a + b
            var doubled = sum * 2
            return doubled
        }
        print(compute(3, 4))
    )";
    
    std::string result = execute_code(source);
    // (3 + 4) * 2 = 14
    assert(result.find("14") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_function_returning_closure() {
    std::cout << "Test: Function returning a closure ... ";
    
    std::string source = R"(
        func makeCounter() -> () -> Int {
            var count = 0
            return { () -> Int in
                count += 1
                return count
            }
        }
        var counter = makeCounter()
        print(counter())
        print(counter())
        print(counter())
    )";
    
    std::string result = execute_code(source);
    size_t pos1 = result.find("1");
    size_t pos2 = result.find("2", pos1 == std::string::npos ? 0 : pos1 + 1);
    size_t pos3 = result.find("3", pos2 == std::string::npos ? 0 : pos2 + 1);
    assert(result.find("ERROR") == std::string::npos);
    assert(pos1 != std::string::npos);
    assert(pos2 != std::string::npos);
    assert(pos3 != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_closure_variable_assignment() {
    std::cout << "Test: Closure assigned to variable ... ";
    
    std::string source = R"(
        var multiply = { (a: Int, b: Int) -> Int in
            return a * b
        }
        var result = multiply(6, 7)
        print(result)
    )";
    
    std::string result = execute_code(source);
    assert(result.find("42") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_closure_captures_outer_variable() {
    std::cout << "Test: Closure captures outer variable ... ";
    
    std::string source = R"(
        var base = 10
        var addBase = { (x: Int) -> Int in
            return base + x
        }
        print(addBase(5))
    )";
    
    std::string result = execute_code(source);
    assert(result.find("15") != std::string::npos);
    
    std::cout << "PASSED\n";
}

void test_nested_closure_captures_after_scope_exit() {
    std::cout << "Test: Nested closure keeps captured state after scope exit ... ";
    
    std::string source = R"(
        var counter
        {
            var count = 0
            counter = { () -> Int in
                count += 1
                return count
            }
        }
        print(counter())
        print(counter())
        print(counter())
    )";
    
    std::string result = execute_code(source);
    // Expect 1, 2, 3 in order
    size_t pos1 = result.find("1");
    size_t pos2 = result.find("2", pos1 == std::string::npos ? 0 : pos1 + 1);
    size_t pos3 = result.find("3", pos2 == std::string::npos ? 0 : pos2 + 1);
    assert(pos1 != std::string::npos);
    assert(pos2 != std::string::npos);
    assert(pos3 != std::string::npos);
    
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "======================================\n";
    std::cout << "  CLOSURE TEST SUITE\n";
    std::cout << "======================================\n\n";

    try {
        test_closure_basic();
        test_closure_no_params();
        test_closure_single_param();
        test_closure_multiple_statements();
        test_closure_variable_assignment();
        test_closure_captures_outer_variable();
        test_nested_closure_captures_after_scope_exit();
        test_closure_as_argument();
        test_function_returning_closure();
        
        std::cout << "\n======================================\n";
        std::cout << "  ALL CLOSURE TESTS PASSED!\n";
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
