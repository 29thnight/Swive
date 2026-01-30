// Test access control validation
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

// Test 1: Private property cannot be accessed from outside
void test_private_property_error() {
    std::string source = R"(
        class Person {
            private var ssn: String = "123-45-6789"
            var name: String = "John"
        }
        
        var p = Person()
        print(p.ssn)
    )";

    try {
        run_code(source);
        throw std::runtime_error("Expected type check error for private property access");
    } catch (const TypeCheckError& e) {
        std::string msg = e.what();
        if (msg.find("private") == std::string::npos || msg.find("inaccessible") == std::string::npos) {
            throw std::runtime_error("Expected error about private access, got: " + msg);
        }
        std::cout << "[PASS] test_private_property_error\n";
    }
}

// Test 2: Private method cannot be accessed from outside
void test_private_method_error() {
    std::string source = R"(
        class Calculator {
            private func secretCalculation() -> Int {
                return 42
            }
            
            func publicMethod() -> Int {
                return secretCalculation()
            }
        }
        
        var calc = Calculator()
        print(calc.secretCalculation())
    )";

    try {
        run_code(source);
        throw std::runtime_error("Expected type check error for private method access");
    } catch (const TypeCheckError& e) {
        std::string msg = e.what();
        if (msg.find("private") == std::string::npos || msg.find("inaccessible") == std::string::npos) {
            throw std::runtime_error("Expected error about private access, got: " + msg);
        }
        std::cout << "[PASS] test_private_method_error\n";
    }
}

// Test 3: Private members accessible within same class
void test_private_access_within_class() {
    std::string source = R"(
        class Counter {
            private var value: Int = 0

            private func increment() {
                self.value = self.value + 1
            }

            func incrementTwice() {
                self.increment()
                self.increment()
            }

            func getValue() -> Int {
                return self.value
            }
        }

        var c = Counter()
        c.incrementTwice()
        print(c.getValue())
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "2", "test_private_access_within_class");
    std::cout << "[PASS] test_private_access_within_class\n";
}

// Test 4: Public members accessible from anywhere
void test_public_access() {
    std::string source = R"(
        class Person {
            public var name: String = "John"
            public func greet() {
                print("Hello")
            }
        }
        
        var p = Person()
        print(p.name)
        p.greet()
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "John", "test_public_access name");
    AssertHelper::assert_contains(result, "Hello", "test_public_access greet");
    std::cout << "[PASS] test_public_access\n";
}

// Test 5: Internal (default) access
void test_internal_access() {
    std::string source = R"(
        struct Point {
            var x: Int = 0
            var y: Int = 0
        }
        
        var p = Point()
        print(p.x)
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "0", "test_internal_access");
    std::cout << "[PASS] test_internal_access\n";
}

// Test 6: Private struct members
void test_private_struct_members() {
    std::string source = R"(
        struct BankAccount {
            private var balance: Int = 1000
            
            func getBalance() -> Int {
                return self.balance
            }
        }
        
        var account = BankAccount()
        print(account.getBalance())
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "1000", "test_private_struct_members");
    std::cout << "[PASS] test_private_struct_members\n";
}

// Test 7: Private struct member access error
void test_private_struct_error() {
    std::string source = R"(
        struct BankAccount {
            private var balance: Int = 1000
        }
        
        var account = BankAccount()
        print(account.balance)
    )";

    try {
        run_code(source);
        throw std::runtime_error("Expected type check error for private struct member access");
    } catch (const TypeCheckError& e) {
        std::string msg = e.what();
        if (msg.find("private") == std::string::npos || msg.find("inaccessible") == std::string::npos) {
            throw std::runtime_error("Expected error about private access, got: " + msg);
        }
        std::cout << "[PASS] test_private_struct_error\n";
    }
}

// Test 8: Extension can access private members within same type context
void test_extension_private_access() {
    std::string source = R"(
        class Person {
            private var id: Int = 123
            
            func getId() -> Int {
                return self.id
            }
        }
        
        extension Person {
            func showId() {
                print(self.id)
            }
        }
        
        var p = Person()
        p.showId()
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "123", "test_extension_private_access");
    std::cout << "[PASS] test_extension_private_access\n";
}

// Test 9: Extension with private method
void test_extension_private_method() {
    std::string source = R"(
        struct Calculator {
            var value: Int = 0
        }
        
        extension Calculator {
            private func secretMultiply() -> Int {
                return self.value * 2
            }
            
            func publicDouble() -> Int {
                return self.secretMultiply()
            }
        }
        
        var calc = Calculator()
        print(calc.publicDouble())
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "0", "test_extension_private_method");
    std::cout << "[PASS] test_extension_private_method\n";
}

// Test 10: Extension private method cannot be accessed from outside
void test_extension_private_error() {
    std::string source = R"(
        struct Calculator {
            var value: Int = 10
        }
        
        extension Calculator {
            private func secretMultiply() -> Int {
                return self.value * 2
            }
        }
        
        var calc = Calculator()
        print(calc.secretMultiply())
    )";

    try {
        run_code(source);
        throw std::runtime_error("Expected type check error for extension private method access");
    } catch (const TypeCheckError& e) {
        std::string msg = e.what();
        if (msg.find("private") == std::string::npos || msg.find("inaccessible") == std::string::npos) {
            throw std::runtime_error("Expected error about private access, got: " + msg);
        }
        std::cout << "[PASS] test_extension_private_error\n";
    }
}

} // namespace test
} // namespace swiftscript
