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

void test_simple_class_method() {
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
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "hi", "greet() should print 'hi'");
}

void test_initializer_called() {
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
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_order(out, "init called", "123", "init should be called before value()");
}

void test_stored_property_defaults() {
    std::string source = R"(
        class Box {
            var value: Int = 42
            let label = "box"
            func describe() {
                print(label)
            }
        }
        var box = Box()
        print(box.value)
        box.describe()
    )";
    auto out = run_code(source);
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "42", "Property value should be 42");
    AssertHelper::assert_contains(out, "box", "Property label should be 'box'");
}

void test_inherited_method_call() {
    std::string source = R"(
        class Animal {
            func speak() {
                print("woof")
            }
        }
        class Dog: Animal {
        }
        var d = Dog()
        d.speak()
    )";
    auto out = run_code(source);
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "woof", "Inherited method should work");
}

void test_super_method_call() {
    std::string source = R"(
        class Animal {
            func speak() {
                print("animal")
            }
        }
        class Dog: Animal {
            override func speak() {
                super.speak()
                print("dog")
            }
        }
        var d = Dog()
        d.speak()
    )";
    auto out = run_code(source);
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_order(out, "animal", "dog", "super.speak() should be called first");
}

void test_inherited_property_defaults() {
    std::string source = R"(
        class Base {
            var a: Int = 1
        }
        class Derived: Base {
            var b: Int = 2
        }
        var obj = Derived()
        print(obj.a)
        print(obj.b)
    )";
    auto out = run_code(source);
    AssertHelper::assert_no_error(out);
    OutputMatcher::assert_contains_all(out, {"1", "2"});
    AssertHelper::assert_order(out, "1", "2", "Properties should print in order a, b");
}

void test_override_required() {
    std::string source = R"(
        class Animal {
            func speak() {
                print("animal")
            }
        }
        class Dog: Animal {
           func speak() {
                print("dog")
            }
        }
        var d = Dog()
        d.speak()
    )";
    auto out = run_code(source);
    // Should error: override not used
    AssertHelper::assert_error(out, "Missing override keyword should produce error");
}

void test_override_without_base_method() {
    std::string source = R"(
        class Animal {
            func speak() {
                print("animal")
            }
        }
        class Dog: Animal {
            override func bark() {
                print("woof")
            }
        }
        var d = Dog()
        d.bark()
    )";
    auto out = run_code(source);
    // Should error: override used but no base method
    AssertHelper::assert_error(out, "Override without base method should produce error");
}

void test_override_init_allowed() {
    std::string source = R"(
        class Base {
            func init() {
                print("base")
            }
        }
        class Derived: Base {
            func init() {
                print("derived")
            }
        }
        var d = Derived()
    )";
    auto out = run_code(source);
    // init can be overridden without override keyword
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "derived", "Derived init should be called");
}

void test_deinit_called() {
    std::string source = R"(
        class Resource {
            deinit {
                print("cleanup")
            }
        }
        func test() {
            var r = Resource()
            print("created")
        }
        test()
        print("done")
    )";
    auto out = run_code(source);
    AssertHelper::assert_no_error(out);
    
    // Note: deinit may be called after function returns (deferred deallocation)
    // Just verify all three messages are present
    AssertHelper::assert_contains(out, "created", "Should create resource");
    AssertHelper::assert_contains(out, "cleanup", "Should call deinit");
    AssertHelper::assert_contains(out, "done", "Should complete");
    
    // If VM uses immediate deallocation (ARC-style), order would be: created, cleanup, done
    // If VM uses deferred deallocation (GC-style), order would be: created, done, cleanup
    // Both are valid depending on memory management strategy
}

void test_deinit_with_properties() {
    std::string source = R"(
        class Counter {
            var value: Int = 42
            deinit {
                print(value)
            }
        }
        func test() {
            var c = Counter()
        }
        test()
    )";
    auto out = run_code(source);
    AssertHelper::assert_no_error(out);
    AssertHelper::assert_contains(out, "42", "deinit should access property value");
}

int main() {
    std::cout << "======================================\n";
    std::cout << "  CLASS TEST SUITE\n";
    std::cout << "======================================\n\n";

    TestRunner runner;

    // Run all tests with automatic tracking
    runner.run_test("simple class method", test_simple_class_method);
    runner.run_test("initializer is invoked", test_initializer_called);
    runner.run_test("stored property defaults", test_stored_property_defaults);
    runner.run_test("inherited method call", test_inherited_method_call);
    runner.run_test("super method call", test_super_method_call);
    runner.run_test("inherited property defaults", test_inherited_property_defaults);
    runner.run_test("override keyword required", test_override_required);
    runner.run_test("override without base method (should error)", test_override_without_base_method);
    runner.run_test("override init without keyword allowed", test_override_init_allowed);
    runner.run_test("deinit is called on deallocation", test_deinit_called);
    runner.run_test("deinit can access properties", test_deinit_with_properties);

    // Print summary
    runner.print_summary();

    // Check memory (if tracking was enabled)
    auto& mem_tracker = MemoryTracker::instance();
    if (mem_tracker.is_tracking()) {
        mem_tracker.print_memory_stats();
        mem_tracker.print_leak_report();
    }

    if (runner.all_passed()) {
        std::cout << "\n✓ ALL CLASS TESTS PASSED!\n";
        return 0;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED\n";
        return 1;
    }
}
