// Test let constant validation
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
// Helper function to run code
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

// Test 1: let constant cannot be reassigned
void test_let_reassignment_error() {
    std::string source = R"(
        let x = 10
        x = 20
    )";

    try {
        run_code(source);
        throw std::runtime_error("Expected type check error for let reassignment");
    } catch (const TypeCheckError& e) {
        std::string msg = e.what();
        if (msg.find("let") == std::string::npos || msg.find("constant") == std::string::npos) {
            throw std::runtime_error("Expected error message about let constant, got: " + msg);
        }
        std::cout << "[PASS] test_let_reassignment_error\n";
    }
}

// Test 2: var can be reassigned
void test_var_reassignment_ok() {
    std::string source = R"(
        var x = 10
        x = 20
        print(x)
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "20", "test_var_reassignment_ok");
    std::cout << "[PASS] test_var_reassignment_ok\n";
}

// Test 3: Multiple let constants
void test_multiple_let_constants() {
    std::string source = R"(
        let a = 1
        let b = 2
        let c = 3
        print(a + b + c)
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "6", "test_multiple_let_constants");
    std::cout << "[PASS] test_multiple_let_constants\n";
}

// Test 4: let in different scopes
void test_let_scopes() {
    std::string source = R"(
        let x = 10
        if true {
            let x = 20
            print(x)
        }
        print(x)
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "20", "test_let_scopes inner");
    AssertHelper::assert_contains(result, "10", "test_let_scopes outer");
    std::cout << "[PASS] test_let_scopes\n";
}

// Test 5: let struct with mutating method error
void test_let_struct_mutating_error() {
    std::string source = R"(
        struct Counter {
            var count: Int = 0
            
            mutating func increment() {
                self.count = self.count + 1
            }
        }
        
        let c = Counter()
        c.increment()
    )";

    try {
        run_code(source);
        throw std::runtime_error("Expected type check error for mutating method on let constant");
    } catch (const TypeCheckError& e) {
        std::string msg = e.what();
        if (msg.find("mutating") == std::string::npos || msg.find("let") == std::string::npos) {
            throw std::runtime_error("Expected error about mutating method on let constant, got: " + msg);
        }
        std::cout << "[PASS] test_let_struct_mutating_error\n";
    }
}

// Test 7: var struct with mutating method ok
void test_var_struct_mutating_ok() {
    std::string source = R"(
        struct Counter {
            var count: Int = 0
            
            mutating func increment() {
                self.count = self.count + 1
            }
            
            func getCount() -> Int {
                return self.count
            }
        }
        
        var c = Counter()
        c.increment()
        c.increment()
        print(c.getCount())
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "2", "test_var_struct_mutating_ok");
    std::cout << "[PASS] test_var_struct_mutating_ok\n";
}

// Test 8: let struct with non-mutating method ok
void test_let_struct_non_mutating_ok() {
    std::string source = R"(
        struct Point {
            var x: Int = 0
            var y: Int = 0
            
            func distance() -> Int {
                return self.x + self.y
            }
        }
        
        let p = Point()
        print(p.distance())
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "0", "test_let_struct_non_mutating_ok");
    std::cout << "[PASS] test_let_struct_non_mutating_ok\n";
}

} // namespace test
} // namespace swiftscript
