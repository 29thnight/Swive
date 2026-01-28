#include "ss_vm.hpp"
#include "ss_value.hpp"
#include "ss_core.hpp"
#include <iostream>
#include <cassert>

using namespace swiftscript;

void run_optional_tests();

void test_value_types() {
    std::cout << "=== Testing Value Types ===\n";
    
    // Test null
    Value v_null = Value::null();
    assert(v_null.is_null());
    std::cout << "Null: " << v_null.to_string() << "\n";
    
    // Test bool
    Value v_true = Value::from_bool(true);
    Value v_false = Value::from_bool(false);
    assert(v_true.is_bool() && v_true.as_bool() == true);
    assert(v_false.is_bool() && v_false.as_bool() == false);
    std::cout << "Bool: " << v_true.to_string() << ", " << v_false.to_string() << "\n";
    
    // Test int
    Value v_int = Value::from_int(42);
    assert(v_int.is_int() && v_int.as_int() == 42);
    std::cout << "Int: " << v_int.to_string() << "\n";
    
    // Test float
    Value v_float = Value::from_float(3.14);
    assert(v_float.is_float());
    std::cout << "Float: " << v_float.to_string() << "\n";
    
    // Test equality
    Value v_int2 = Value::from_int(42);
    assert(v_int.equals(v_int2));
    std::cout << "Equality test passed\n";
    
    std::cout << "Value size: " << Value::size() << " bytes\n";
    std::cout << "\n";
}

void test_string_object() {
    std::cout << "=== Testing String Objects ===\n";
    
    VMConfig config;
    config.enable_debug = true;
    VM vm(config);
    
    // Create string
    auto* str1 = vm.allocate_object<StringObject>("Hello, World!");
    Value v_str1 = Value::from_object(str1);
    
    std::cout << "String: " << v_str1.to_string() << "\n";
    std::cout << "Memory size: " << str1->memory_size() << " bytes\n";
    
    // Test RC
    std::cout << "\nTesting Reference Counting:\n";
    std::cout << "Initial RC: " << str1->rc.strong_count << "\n";
    
    RC::retain(str1);
    std::cout << "After retain: " << str1->rc.strong_count << "\n";
    
    RC::retain(str1);
    std::cout << "After second retain: " << str1->rc.strong_count << "\n";
    
    RC::release(&vm, str1);
    std::cout << "After release: " << str1->rc.strong_count << "\n";
    
    // Test deferred cleanup
    RC::release(&vm, str1);
    std::cout << "After second release (RC=0): " << str1->rc.strong_count << "\n";
    std::cout << "Deferred releases pending: " << vm.get_deferred_releases().size() << "\n";
    
    vm.run_cleanup();
    std::cout << "After cleanup, deferred releases: " << vm.get_deferred_releases().size() << "\n";
    
    std::cout << "\n";
}

void test_list_object() {
    std::cout << "=== Testing List Objects ===\n";
    
    VMConfig config;
    config.enable_debug = true;
    VM vm(config);
    
    // Create list
    auto* list = vm.allocate_object<ListObject>();
    list->elements.push_back(Value::from_int(1));
    list->elements.push_back(Value::from_int(2));
    list->elements.push_back(Value::from_int(3));
    
    Value v_list = Value::from_object(list);
    std::cout << "List: " << v_list.to_string() << "\n";
    
    // Test RC
    RC::retain(list);
    RC::retain(list);
    std::cout << "RC after retain: " << list->rc.strong_count << "\n";
    
    RC::release(&vm, list);
    RC::release(&vm, list);
    std::cout << "RC after 2 releases: " << list->rc.strong_count << "\n";
    
    vm.run_cleanup();
    std::cout << "\n";
}

void test_weak_references() {
    std::cout << "=== Testing Weak References ===\n";
    
    VMConfig config;
    config.enable_debug = true;
    VM vm(config);
    
    // Create string with strong reference
    auto* str = vm.allocate_object<StringObject>("Test String");
    Value strong_ref = Value::from_object(str, RefType::Strong);
    
    // Create weak reference
    Value weak_ref = Value::from_object(str, RefType::Weak);
    
    std::cout << "Strong ref: " << strong_ref.to_string() << "\n";
    std::cout << "Weak ref: " << weak_ref.to_string() << "\n";
    
    // Register weak reference
    RC::retain(str);
    RC::weak_retain(str, &weak_ref);
    
    std::cout << "Strong RC: " << str->rc.strong_count << "\n";
    std::cout << "Weak RC: " << str->rc.weak_count << "\n";
    
    // Release strong reference
    RC::release(&vm, str);
    std::cout << "After release, Strong RC: " << str->rc.strong_count << "\n";
    
    // Process deferred - this should nil out weak reference
    vm.run_cleanup();
    
    std::cout << "After cleanup:\n";
    std::cout << "Weak ref is now: " << weak_ref.to_string() << "\n";
    assert(weak_ref.is_null() && "Weak reference should be nil after object deallocation");
    
    std::cout << "\n";
}

void test_vm_stack() {
    std::cout << "=== Testing VM Stack ===\n";
    
    VM vm;
    
    // Push values
    vm.push(Value::from_int(10));
    vm.push(Value::from_int(20));
    vm.push(Value::from_int(30));
    
    std::cout << "Pushed 10, 20, 30\n";
    std::cout << "Peek(0): " << vm.peek(0).to_string() << "\n";
    std::cout << "Peek(1): " << vm.peek(1).to_string() << "\n";
    std::cout << "Peek(2): " << vm.peek(2).to_string() << "\n";
    
    // Pop values
    std::cout << "Pop: " << vm.pop().to_string() << "\n";
    std::cout << "Pop: " << vm.pop().to_string() << "\n";
    std::cout << "Pop: " << vm.pop().to_string() << "\n";
    
    std::cout << "\n";
}

void test_nested_objects() {
    std::cout << "=== Testing Nested Objects ===\n";
    
    VMConfig config;
    config.enable_debug = true;
    VM vm(config);
    
    // Create list with string elements
    auto* list = vm.allocate_object<ListObject>();
    auto* str1 = vm.allocate_object<StringObject>("First");
    auto* str2 = vm.allocate_object<StringObject>("Second");
    
    RC::retain(str1);
    RC::retain(str2);
    
    list->elements.push_back(Value::from_object(str1));
    list->elements.push_back(Value::from_object(str2));
    
    std::cout << "List contents: " << list->to_string() << "\n";
    std::cout << "List RC: " << list->rc.strong_count << "\n";
    std::cout << "String1 RC: " << str1->rc.strong_count << "\n";
    std::cout << "String2 RC: " << str2->rc.strong_count << "\n";
    
    // Release strings (they're now only held by list)
    RC::release(&vm, str1);
    RC::release(&vm, str2);
    std::cout << "After releasing strings from external refs:\n";
    std::cout << "String1 RC: " << str1->rc.strong_count << "\n";
    std::cout << "String2 RC: " << str2->rc.strong_count << "\n";
    
    vm.run_cleanup();
    std::cout << "\n";
}

int main() {
    std::cout << "SwiftScript Basic Tests\n";
    std::cout << "=======================\n\n";
    
    try {
        test_value_types();
        test_string_object();
        test_list_object();
        test_weak_references();
        test_vm_stack();
        test_nested_objects();
        run_optional_tests();
        
        std::cout << "\n✓ All tests passed!\n\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << "\n";
        return 1;
    }
}
