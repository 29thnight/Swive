#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_vm.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace swiftscript;

std::string run_code(const std::string& source) {
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
}

void test_simple_class_method() {
    std::cout << "Test: simple class method ... ";
    std::string source = R"(
        class Greeter {
            func greet() {
                print("hi")
            }
        }
        var g = Greeter()
        g.greet()
    )";
    auto out = run_code(source);
    assert(out.find("hi") != std::string::npos);
    std::cout << "PASSED\n";
}

void test_initializer_called() {
    std::cout << "Test: initializer is invoked ... ";
    std::string source = R"(
        class Counter {
            func init() {
                print("init called")
            }
            func value() -> Int {
                return 123
            }
        }
        var c = Counter()
        print(c.value())
    )";
    auto out = run_code(source);
    assert(out.find("init called") != std::string::npos);
    assert(out.find("123") != std::string::npos);
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "======================================\n";
    std::cout << "  CLASS TEST SUITE\n";
    std::cout << "======================================\n\n";

    try {
        test_simple_class_method();
        test_initializer_called();

        std::cout << "\n======================================\n";
        std::cout << "  ALL CLASS TESTS PASSED!\n";
        std::cout << "======================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n======================================\n";
        std::cerr << "  CLASS TEST FAILED!\n";
        std::cerr << "  Error: " << e.what() << "\n";
        std::cerr << "======================================\n";
        return 1;
    }
}
