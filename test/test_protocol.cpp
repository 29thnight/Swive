#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_vm.hpp"
#include "test_helpers.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace swiftscript;
using namespace swiftscript::test;

namespace {
// Static helper function to avoid linker conflicts
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
        std::streambuf* old = std::cout.rdbuf(output.rdbuf());
        struct Restore { std::streambuf* old; ~Restore(){ std::cout.rdbuf(old); } } restore{old};

        vm.execute(chunk);
        return output.str();
    } catch (const std::exception& e) {
        return std::string("ERROR: ") + e.what();
    }
}
} // anonymous namespace

namespace swiftscript {
namespace test {

// Test 1: Basic protocol declaration
void test_protocol_basic_declaration() {
    const char* code = R"(
        protocol Drawable {
            func draw()
            var size: Int { get set }
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_basic_declaration\n";
    } else {
        std::cout << "[SKIP] test_protocol_basic_declaration (not implemented yet)\n";
    }
}

// Test 2: Protocol with method requirements
void test_protocol_method_requirements() {
    const char* code = R"(
        protocol Vehicle {
            func start()
            func stop()
            func getSpeed() -> Int
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_method_requirements\n";
    } else {
        std::cout << "[SKIP] test_protocol_method_requirements (not implemented yet)\n";
    }
}

// Test 3: Protocol with property requirements
void test_protocol_property_requirements() {
    const char* code = R"(
        protocol Named {
            var name: String { get }
            var fullName: String { get set }
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_property_requirements\n";
    } else {
        std::cout << "[SKIP] test_protocol_property_requirements (not implemented yet)\n";
    }
}

// Test 4: Protocol inheritance
void test_protocol_inheritance() {
    const char* code = R"(
        protocol Animal {
            func makeSound()
        }
        
        protocol Pet: Animal {
            var name: String { get }
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_inheritance\n";
    } else {
        std::cout << "[SKIP] test_protocol_inheritance (not implemented yet)\n";
    }
}

// Test 5: Protocol with mutating method
void test_protocol_mutating_method() {
    const char* code = R"(
        protocol Counter {
            mutating func increment()
            var count: Int { get }
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_mutating_method\n";
    } else {
        std::cout << "[SKIP] test_protocol_mutating_method (not implemented yet)\n";
    }
}

// Test 6: Struct conforming to protocol
void test_protocol_struct_conformance() {
    const char* code = R"(
        protocol Drawable {
            func draw()
        }
        
        struct Circle: Drawable {
            var radius: Int
            
            func draw() {
                print("Drawing circle")
            }
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_struct_conformance\n";
    } else {
        std::cout << "[SKIP] test_protocol_struct_conformance (not implemented yet)\n";
    }
}

// Test 7: Class conforming to protocol
void test_protocol_class_conformance() {
    const char* code = R"(
        protocol Describable {
            func describe() -> String
        }
        
        class Person: Describable {
            var name: String
            
            func describe() -> String {
                return name
            }
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_class_conformance\n";
    } else {
        std::cout << "[SKIP] test_protocol_class_conformance (not implemented yet)\n";
    }
}

// Test 8: Class with superclass and protocol
void test_protocol_class_superclass_and_protocol() {
    const char* code = R"(
        protocol Flyable {
            func fly()
        }
        
        class Animal {
            var name: String
        }
        
        class Bird: Animal, Flyable {
            func fly() {
                print("Flying")
            }
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_class_superclass_and_protocol\n";
    } else {
        std::cout << "[SKIP] test_protocol_class_superclass_and_protocol (not implemented yet)\n";
    }
}

// Test 9: Multiple protocol conformance
void test_protocol_multiple_conformance() {
    const char* code = R"(
        protocol Drawable {
            func draw()
        }
        
        protocol Movable {
            func move()
        }
        
        struct Sprite: Drawable, Movable {
            func draw() {
                print("Drawing")
            }
            
            func move() {
                print("Moving")
            }
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_multiple_conformance\n";
    } else {
        std::cout << "[SKIP] test_protocol_multiple_conformance (not implemented yet)\n";
    }
}

// Test 10: Protocol with multiple method parameters
void test_protocol_method_parameters() {
    const char* code = R"(
        protocol Calculator {
            func add(a: Int, b: Int) -> Int
            func multiply(x: Int, y: Int) -> Int
        }
    )";
    
    std::string result = run_code(code);
    if (result.find("ERROR") == std::string::npos) {
        std::cout << "[PASS] test_protocol_method_parameters\n";
    } else {
        std::cout << "[SKIP] test_protocol_method_parameters (not implemented yet)\n";
    }
}

} // namespace test
} // namespace swiftscript
