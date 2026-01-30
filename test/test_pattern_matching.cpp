// Test associated values pattern matching
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_type_checker.hpp"
#include "ss_vm.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace swiftscript;

namespace {
std::string run_code(const std::string& source) {
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
        throw;
    }
}

class AssertHelper {
public:
    static void assert_contains(const std::string& output, const std::string& expected, const std::string& test_name) {
        if (output.find(expected) == std::string::npos) {
            throw std::runtime_error("Expected to find '" + expected + "' in output (" + test_name + ")\nActual output: " + output);
        }
    }
};
} // anonymous namespace

namespace swiftscript {
namespace test {

// Test 1: Basic associated value extraction with Int
void test_associated_value_int_extraction() {
    std::string source = R"(
        enum Result {
            case success(value: Int)
            case failure(error: Int)
        }
        
        var r = Result.success(value: 42)
        switch r {
        case .success(let value):
            print(value)
        case .failure(let error):
            print(error)
        }
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "42", "test_associated_value_int_extraction");
    std::cout << "[PASS] test_associated_value_int_extraction\n";
}

// Test 2: Associated value extraction with String
void test_associated_value_string_extraction() {
    std::string source = R"(
        enum Message {
            case text(content: String)
            case number(value: Int)
        }
        
        var msg = Message.text(content: "Hello")
        switch msg {
        case .text(let content):
            print(content)
        case .number(let value):
            print(value)
        }
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "Hello", "test_associated_value_string_extraction");
    std::cout << "[PASS] test_associated_value_string_extraction\n";
}

// Test 3: Multiple associated values
void test_multiple_associated_values() {
    std::string source = R"(
        enum Point {
            case cartesian(x: Int, y: Int)
            case polar(r: Int, theta: Int)
        }
        
        var p = Point.cartesian(x: 10, y: 20)
        switch p {
        case .cartesian(let x, let y):
            print(x)
            print(y)
        case .polar(let r, let theta):
            print(r)
            print(theta)
        }
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "10", "test_multiple_associated_values x");
    AssertHelper::assert_contains(result, "20", "test_multiple_associated_values y");
    std::cout << "[PASS] test_multiple_associated_values\n";
}

// Test 4: Associated values with default case
void test_associated_values_with_default() {
    std::string source = R"(
        enum Result {
            case success(value: Int)
            case failure(error: Int)
            case pending
        }
        
        var r = Result.pending
        switch r {
        case .success(let value):
            print(value)
        case .failure(let error):
            print(error)
        default:
            print("pending")
        }
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "pending", "test_associated_values_with_default");
    std::cout << "[PASS] test_associated_values_with_default\n";
}

} // namespace test
} // namespace swiftscript
