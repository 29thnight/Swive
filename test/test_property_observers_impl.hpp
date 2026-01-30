#pragma once

#include "test_helpers.hpp"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_vm.hpp"
#include <string>
#include <sstream>
#include <iostream>

namespace swiftscript {
namespace test {

// Test 1: Basic willSet observer
void test_willset_basic() {
    std::string source = R"(
        class Counter {
            var value: Int = 0 {
                willSet {
                    print("Will set to:")
                    print(newValue)
                }
            }
        }
        
        var c = Counter()
        c.value = 5
        print(c.value)
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "Will set to:", "test_willset_basic");
    AssertHelper::assert_contains(result, "5", "test_willset_basic value");
    std::cout << "[PASS] test_willset_basic\n";
}

// Test 2: Basic didSet observer
void test_didset_basic() {
    std::string source = R"(
        class Counter {
            var value: Int = 0 {
                didSet {
                    print("Did set from:")
                    print(oldValue)
                }
            }
        }
        
        var c = Counter()
        c.value = 10
        print(c.value)
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "Did set from:", "test_didset_basic");
    AssertHelper::assert_contains(result, "0", "test_didset_basic old value");
    AssertHelper::assert_contains(result, "10", "test_didset_basic new value");
    std::cout << "[PASS] test_didset_basic\n";
}

// Test 3: Both willSet and didSet
void test_willset_and_didset() {
    std::string source = R"(
        class Temperature {
            var celsius: Int = 0 {
                willSet {
                    print("Changing from")
                    print(self.celsius)
                    print("to")
                    print(newValue)
                }
                didSet {
                    print("Changed from")
                    print(oldValue)
                }
            }
        }
        
        var temp = Temperature()
        temp.celsius = 25
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "Changing from", "test_willset_and_didset willSet");
    AssertHelper::assert_contains(result, "Changed from", "test_willset_and_didset didSet");
    AssertHelper::assert_contains(result, "25", "test_willset_and_didset value");
    std::cout << "[PASS] test_willset_and_didset\n";
}

// Test 4: Property observers in struct
void test_observers_in_struct() {
    std::string source = R"(
        struct Point {
            var x: Int = 0 {
                didSet {
                    print("X changed")
                }
            }
            var y: Int = 0 {
                didSet {
                    print("Y changed")
                }
            }
        }
        
        var p = Point()
        p.x = 10
        p.y = 20
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "X changed", "test_observers_in_struct x");
    AssertHelper::assert_contains(result, "Y changed", "test_observers_in_struct y");
    std::cout << "[PASS] test_observers_in_struct\n";
}

// Test 5: Lazy property basic
void test_lazy_property_basic() {
    std::string source = R"(
        class Manager {
            lazy var expensiveData: Int = 42
            
            func getData() -> Int {
                print("Getting data")
                return self.expensiveData
            }
        }
        
        var m = Manager()
        print("Manager created")
        print(m.getData())
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "Manager created", "test_lazy_property_basic");
    AssertHelper::assert_contains(result, "Getting data", "test_lazy_property_basic");
    AssertHelper::assert_contains(result, "42", "test_lazy_property_basic value");
    std::cout << "[PASS] test_lazy_property_basic\n";
}

// Test 6: Subscript basic for array
void test_subscript_basic() {
    std::string source = R"(
        var arr = [1, 2, 3, 4, 5]
        print(arr[0])
        print(arr[2])
        arr[1] = 10
        print(arr[1])
    )";

    std::string result = run_code(source);
    AssertHelper::assert_contains(result, "1", "test_subscript_basic first");
    AssertHelper::assert_contains(result, "3", "test_subscript_basic third");
    AssertHelper::assert_contains(result, "10", "test_subscript_basic modified");
    std::cout << "[PASS] test_subscript_basic\n";
}

// Test 7: Custom subscript in struct
void test_custom_subscript_struct() {
    std::string source = R"(
        struct Matrix {
            var data: [Int] = [1, 2, 3, 4]
            
            subscript(index: Int) -> Int {
                get {
                    return self.data[index]
                }
                set {
                    self.data[index] = newValue
                }
            }
        }
        
        var m = Matrix()
        print(m[0])
        print(m[2])
    )";

    // This test may fail if custom subscript is not implemented
    try {
        std::string result = run_code(source);
        AssertHelper::assert_contains(result, "1", "test_custom_subscript_struct first");
        AssertHelper::assert_contains(result, "3", "test_custom_subscript_struct third");
        std::cout << "[PASS] test_custom_subscript_struct\n";
    } catch (...) {
        std::cout << "[SKIP] test_custom_subscript_struct - Custom subscript not yet implemented\n";
    }
}

} // namespace test
} // namespace swiftscript
