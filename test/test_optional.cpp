#include "ss_vm.hpp"
#include "ss_value.hpp"
#include <cassert>
#include <iostream>

using namespace swiftscript;

namespace {
void test_optional_nil_coalesce() {
    VM vm;
    vm.interpret("var x: Int? = nil; var y = x ?? 42;");
    Value y = vm.get_global("y");
    assert(y.is_int() && y.as_int() == 42);
}

void test_optional_force_unwrap() {
    VM vm;
    vm.interpret("var x: Int? = 10; var y = x!;");
    Value y = vm.get_global("y");
    assert(y.is_int() && y.as_int() == 10);
}

void test_optional_force_unwrap_nil() {
    VM vm;
    bool threw = false;
    try {
        vm.interpret("var x: Int? = nil; var y = x!;");
    } catch (const std::exception&) {
        threw = true;
    }
    assert(threw && "Force unwrap of nil should throw");
}

void test_if_let() {
    VM vm;
    vm.interpret("var x: Int? = 10; var y = 0; if let v = x { y = v } else { y = 1 }");
    Value y = vm.get_global("y");
    assert(y.is_int() && y.as_int() == 10);
}

void test_guard_let() {
    VM vm;
    vm.interpret("var x: Int? = 10; var y = 0; guard let v = x else { y = 1; return; } y = v;");
    Value y = vm.get_global("y");
    assert(y.is_int() && y.as_int() == 10);
}

void test_guard_let_nil() {
    VM vm;
    vm.interpret("var x: Int? = nil; var y = 0; guard let v = x else { y = 5; return; } y = v;");
    Value y = vm.get_global("y");
    assert(y.is_int() && y.as_int() == 5);
}

void test_optional_chaining() {
    VM vm;
    auto* map = vm.allocate_object<MapObject>();
    map->entries["value"] = Value::from_int(7);
    vm.set_global("obj", Value::from_object(map));
    vm.interpret("var result = obj?.value;");
    Value result = vm.get_global("result");
    assert(result.is_int() && result.as_int() == 7);
}

void test_optional_chaining_nil() {
    VM vm;
    vm.set_global("obj", Value::null());
    vm.interpret("var result = obj?.value;");
    Value result = vm.get_global("result");
    assert(result.is_null());
}

void test_nil_coalesce_chain() {
    VM vm;
    vm.interpret("var a: Int? = nil; var b: Int? = nil; var c = 7; var result = a ?? b ?? c;");
    Value result = vm.get_global("result");
    assert(result.is_int() && result.as_int() == 7);
}

void test_optional_assignment() {
    VM vm;
    vm.interpret("var x: Int? = 5; x = nil;");
    Value x = vm.get_global("x");
    assert(x.is_null());
}
}

void run_optional_tests() {
    std::cout << "=== Optional Feature Tests ===\n";
    test_optional_nil_coalesce();
    test_optional_force_unwrap();
    test_optional_force_unwrap_nil();
    test_if_let();
    test_guard_let();
    test_guard_let_nil();
    test_optional_chaining();
    test_optional_chaining_nil();
    test_nil_coalesce_chain();
    test_optional_assignment();
    std::cout << "✓ Optional tests passed!\n\n";
}
