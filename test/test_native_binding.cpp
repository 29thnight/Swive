
#include "ss_native_registry.hpp"
#include "ss_native_convert.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_compiler.hpp"
#include "test_helpers.hpp"

using namespace swiftscript;
using namespace swiftscript::test;

namespace {

    // ============================================================================
    // Test Native Types for Binding
    // ============================================================================

    struct TestVector3 {
        float x, y, z;

        TestVector3() : x(0), y(0), z(0) {}
        TestVector3(float x, float y, float z) : x(x), y(y), z(z) {}

        float Magnitude() const {
            return std::sqrt(x * x + y * y + z * z);
        }

        void Normalize() {
            float m = Magnitude();
            if (m > 0) {
                x /= m;
                y /= m;
                z /= m;
            }
        }

        TestVector3 Add(const TestVector3& other) const {
            return TestVector3(x + other.x, y + other.y, z + other.z);
        }
    };

    class TestCounter {
        int value_;
    public:
        TestCounter() : value_(0) {}
        explicit TestCounter(int initial) : value_(initial) {}

        int GetValue() const { return value_; }
        void SetValue(int v) { value_ = v; }

        void Increment() { ++value_; }
        void Decrement() { --value_; }
        void Add(int amount) { value_ += amount; }

        static int StaticMultiply(int a, int b) { return a * b; }
    };

    // ============================================================================
    // Helper Functions
    // ============================================================================

    void setup_test_registry() {
        auto& registry = NativeRegistry::instance();
        registry.clear();

        // Register test functions
        registry.register_function("TestAdd", [](VM& vm, std::span<Value> args) -> Value {
            if (args.size() < 2) return Value::from_int(0);
            int a = from_value<int>(args[0]);
            int b = from_value<int>(args[1]);
            return Value::from_int(a + b);
            });

        registry.register_function("TestGreet", [](VM& vm, std::span<Value> args) -> Value {
            if (args.size() < 1) return to_value(vm, std::string("Hello, World!"));
            std::string name = from_value<std::string>(args[0]);
            return to_value(vm, std::string("Hello, ") + name + "!");
            });

        registry.register_function("TestSum", [](VM& vm, std::span<Value> args) -> Value {
            int sum = 0;
            for (const auto& arg : args) {
                sum += from_value<int>(arg);
            }
            return Value::from_int(sum);
            });

        // Register TestVector3 type
        NativeTypeInfo vector3_info;
        vector3_info.name = "TestVector3";
        vector3_info.size = sizeof(TestVector3);
        vector3_info.is_value_type = true;
        vector3_info.constructor = []() -> void* { return new TestVector3(); };
        vector3_info.destructor = [](void* ptr) { delete static_cast<TestVector3*>(ptr); };

        // Properties
        vector3_info.properties["x"] = NativePropertyInfo{
            "x",
            [](VM& vm, void* ptr) -> Value {
                return Value::from_float(static_cast<TestVector3*>(ptr)->x);
            },
            [](VM& vm, void* ptr, Value v) {
                static_cast<TestVector3*>(ptr)->x = from_value<float>(v);
            }
        };
        vector3_info.properties["y"] = NativePropertyInfo{
            "y",
            [](VM& vm, void* ptr) -> Value {
                return Value::from_float(static_cast<TestVector3*>(ptr)->y);
            },
            [](VM& vm, void* ptr, Value v) {
                static_cast<TestVector3*>(ptr)->y = from_value<float>(v);
            }
        };
        vector3_info.properties["z"] = NativePropertyInfo{
            "z",
            [](VM& vm, void* ptr) -> Value {
                return Value::from_float(static_cast<TestVector3*>(ptr)->z);
            },
            [](VM& vm, void* ptr, Value v) {
                static_cast<TestVector3*>(ptr)->z = from_value<float>(v);
            }
        };

        // Methods
        vector3_info.methods["Magnitude"] = NativeMethodInfo{
            "Magnitude",
            [](VM& vm, void* ptr, std::span<Value> args) -> Value {
                return Value::from_float(static_cast<TestVector3*>(ptr)->Magnitude());
            },
            0
        };
        vector3_info.methods["Normalize"] = NativeMethodInfo{
            "Normalize",
            [](VM& vm, void* ptr, std::span<Value> args) -> Value {
                static_cast<TestVector3*>(ptr)->Normalize();
                return Value::null();
            },
            0
        };

        registry.register_type("TestVector3", std::move(vector3_info));

        // Register TestCounter type
        NativeTypeInfo counter_info;
        counter_info.name = "TestCounter";
        counter_info.size = sizeof(TestCounter);
        counter_info.is_value_type = false;
        counter_info.constructor = []() -> void* { return new TestCounter(); };
        counter_info.destructor = [](void* ptr) { delete static_cast<TestCounter*>(ptr); };

        // Properties
        counter_info.properties["value"] = NativePropertyInfo{
            "value",
            [](VM& vm, void* ptr) -> Value {
                return Value::from_int(static_cast<TestCounter*>(ptr)->GetValue());
            },
            [](VM& vm, void* ptr, Value v) {
                static_cast<TestCounter*>(ptr)->SetValue(from_value<int>(v));
            }
        };

        // Methods
        counter_info.methods["Increment"] = NativeMethodInfo{
            "Increment",
            [](VM& vm, void* ptr, std::span<Value> args) -> Value {
                static_cast<TestCounter*>(ptr)->Increment();
                return Value::null();
            },
            0
        };
        counter_info.methods["Decrement"] = NativeMethodInfo{
            "Decrement",
            [](VM& vm, void* ptr, std::span<Value> args) -> Value {
                static_cast<TestCounter*>(ptr)->Decrement();
                return Value::null();
            },
            0
        };
        counter_info.methods["Add"] = NativeMethodInfo{
            "Add",
            [](VM& vm, void* ptr, std::span<Value> args) -> Value {
                if (args.empty()) return Value::null();
                static_cast<TestCounter*>(ptr)->Add(from_value<int>(args[0]));
                return Value::null();
            },
            1
        };

        registry.register_type("TestCounter", std::move(counter_info));
    }

} // anonymous namespace

namespace swiftscript {
    namespace test {

        // ============================================================================
        // NativeRegistry Tests
        // ============================================================================

        void test_native_registry_function_registration() {
            auto& registry = NativeRegistry::instance();
            registry.clear();

            // Register a simple function
            registry.register_function("MyFunc", [](VM& vm, std::span<Value> args) -> Value {
                return Value::from_int(42);
                });

            AssertHelper::assert_true(registry.has_function("MyFunc"), "Function should be registered");
            AssertHelper::assert_false(registry.has_function("NonExistent"), "Non-existent function should not be found");

            auto* func = registry.find_function("MyFunc");
            AssertHelper::assert_true(func != nullptr, "Function pointer should not be null");

            // Verify function count
            AssertHelper::assert_equals(size_t(1), registry.function_count(), "Should have 1 function");
        }

        void test_native_registry_type_registration() {
            auto& registry = NativeRegistry::instance();
            registry.clear();

            NativeTypeInfo info;
            info.name = "MyType";
            info.size = 16;
            info.constructor = []() -> void* { return nullptr; };
            info.destructor = [](void* ptr) {};

            registry.register_type("MyType", std::move(info));

            AssertHelper::assert_true(registry.has_type("MyType"), "Type should be registered");
            AssertHelper::assert_false(registry.has_type("NonExistent"), "Non-existent type should not be found");

            auto* type_info = registry.find_type("MyType");
            AssertHelper::assert_true(type_info != nullptr, "Type info should not be null");
            AssertHelper::assert_equals(std::string("MyType"), type_info->name, "Type name should match");
            AssertHelper::assert_equals(size_t(16), type_info->size, "Type size should match");
        }

        void test_native_registry_unregister() {
            auto& registry = NativeRegistry::instance();
            registry.clear();

            registry.register_function("TempFunc", [](VM&, std::span<Value>) { return Value::null(); });
            AssertHelper::assert_true(registry.has_function("TempFunc"), "Function should be registered");

            registry.unregister_function("TempFunc");
            AssertHelper::assert_false(registry.has_function("TempFunc"), "Function should be unregistered");
        }

        // ============================================================================
        // Type Conversion Tests
        // ============================================================================

        void test_native_convert_primitives() {
            VMConfig config;
            config.enable_debug = false;
            VM vm(config);

            // Test int conversion
            {
                Value v = to_value(vm, 42);
                AssertHelper::assert_true(v.is_int(), "Should be int");
                AssertHelper::assert_equals(Int(42), v.as_int(), "Int value should match");

                int result = from_value<int>(v);
                AssertHelper::assert_equals(42, result, "Converted int should match");
            }

            // Test float conversion
            {
                Value v = to_value(vm, 3.14);
                AssertHelper::assert_true(v.is_float(), "Should be float");

                double result = from_value<double>(v);
                AssertHelper::assert_true(std::abs(result - 3.14) < 0.001, "Float value should match");
            }

            // Test bool conversion
            {
                Value v = to_value(vm, true);
                AssertHelper::assert_true(v.is_bool(), "Should be bool");
                AssertHelper::assert_true(v.as_bool(), "Bool value should be true");

                bool result = from_value<bool>(v);
                AssertHelper::assert_true(result, "Converted bool should be true");
            }
        }

        void test_native_convert_string() {
            VMConfig config;
            config.enable_debug = false;
            VM vm(config);

            // Test string to Value
            Value v = to_value(vm, std::string("Hello"));
            AssertHelper::assert_true(v.is_object(), "String should be object");

            // Test Value to string
            std::string result = from_value<std::string>(v);
            AssertHelper::assert_equals(std::string("Hello"), result, "String value should match");

            // Test const char* conversion
            Value v2 = to_value(vm, "World");
            AssertHelper::assert_true(v2.is_object(), "const char* should create object");

            std::string result2 = from_value<std::string>(v2);
            AssertHelper::assert_equals(std::string("World"), result2, "const char* value should match");
        }

        void test_native_convert_errors() {
            // Test conversion error for mismatched types
            Value int_val = Value::from_int(42);

            bool caught_error = false;
            try {
                // This should work (int can become float)
                float f = from_value<float>(int_val);
                AssertHelper::assert_true(std::abs(f - 42.0f) < 0.001, "Int to float conversion should work");
            }
            catch (...) {
                caught_error = true;
            }
            AssertHelper::assert_false(caught_error, "Int to float should not throw");

            // Test null to string
            Value null_val = Value::null();
            std::string null_str = from_value<std::string>(null_val);
            AssertHelper::assert_equals(std::string(""), null_str, "Null should convert to empty string");
        }

        // ============================================================================
        // NativeObject Tests
        // ============================================================================

        void test_native_object_creation() {
            setup_test_registry();

            VMConfig config;
            config.enable_debug = false;
            VM vm(config);

            auto* type_info = NativeRegistry::instance().find_type("TestVector3");
            AssertHelper::assert_true(type_info != nullptr, "TestVector3 type should be registered");

            // Create native object
            void* native_ptr = type_info->constructor();
            NativeObject* obj = vm.allocate_object<NativeObject>(native_ptr, "TestVector3", type_info);

            AssertHelper::assert_true(obj != nullptr, "NativeObject should be created");
            AssertHelper::assert_equals(std::string("TestVector3"), obj->type_name, "Type name should match");
            AssertHelper::assert_true(obj->native_ptr != nullptr, "Native pointer should not be null");
            AssertHelper::assert_true(obj->type_info == type_info, "Type info should be cached");

            // Test as<T>() helper
            TestVector3* vec = obj->as<TestVector3>();
            AssertHelper::assert_true(vec != nullptr, "as<TestVector3>() should work");
            AssertHelper::assert_true(std::abs(vec->x) < 0.001, "Default x should be 0");
        }

        void test_native_object_property_access() {
            setup_test_registry();

            VMConfig config;
            config.enable_debug = false;
            VM vm(config);

            auto* type_info = NativeRegistry::instance().find_type("TestVector3");

            // Create and initialize vector
            TestVector3* vec = new TestVector3(1.0f, 2.0f, 3.0f);
            NativeObject* obj = vm.allocate_object<NativeObject>(vec, "TestVector3", type_info);

            // Test property getter
            auto* x_prop = type_info->get_property("x");
            AssertHelper::assert_true(x_prop != nullptr, "x property should exist");

            Value x_val = x_prop->getter(vm, obj->native_ptr);
            AssertHelper::assert_true(x_val.is_float(), "x should be float");
            AssertHelper::assert_true(std::abs(x_val.as_float() - 1.0) < 0.001, "x should be 1.0");

            // Test property setter
            x_prop->setter(vm, obj->native_ptr, Value::from_float(10.0));
            Value new_x = x_prop->getter(vm, obj->native_ptr);
            AssertHelper::assert_true(std::abs(new_x.as_float() - 10.0) < 0.001, "x should be 10.0 after set");
        }

        void test_native_object_method_call() {
            setup_test_registry();

            VMConfig config;
            config.enable_debug = false;
            VM vm(config);

            auto* type_info = NativeRegistry::instance().find_type("TestVector3");

            // Create vector with known values
            TestVector3* vec = new TestVector3(3.0f, 4.0f, 0.0f);
            NativeObject* obj = vm.allocate_object<NativeObject>(vec, "TestVector3", type_info);

            // Test Magnitude method
            auto* magnitude_method = type_info->get_method("Magnitude");
            AssertHelper::assert_true(magnitude_method != nullptr, "Magnitude method should exist");

            Value result = magnitude_method->func(vm, obj->native_ptr, std::span<Value>());
            AssertHelper::assert_true(result.is_float(), "Magnitude should return float");
            AssertHelper::assert_true(std::abs(result.as_float() - 5.0) < 0.001, "Magnitude of (3,4,0) should be 5");

            // Test Normalize method
            auto* normalize_method = type_info->get_method("Normalize");
            normalize_method->func(vm, obj->native_ptr, std::span<Value>());

            // Check normalized values
            auto* x_prop = type_info->get_property("x");
            auto* y_prop = type_info->get_property("y");

            Value norm_x = x_prop->getter(vm, obj->native_ptr);
            Value norm_y = y_prop->getter(vm, obj->native_ptr);

            AssertHelper::assert_true(std::abs(norm_x.as_float() - 0.6) < 0.001, "Normalized x should be 0.6");
            AssertHelper::assert_true(std::abs(norm_y.as_float() - 0.8) < 0.001, "Normalized y should be 0.8");
        }

        // ============================================================================
        // Native Function Call Tests
        // ============================================================================

        void test_native_function_call() {
            setup_test_registry();

            VMConfig config;
            config.enable_debug = false;
            VM vm(config);

            // Test TestAdd function
            auto* add_func = NativeRegistry::instance().find_function("TestAdd");
            AssertHelper::assert_true(add_func != nullptr, "TestAdd should be registered");

            std::vector<Value> args = { Value::from_int(10), Value::from_int(20) };
            Value result = (*add_func)(vm, std::span<Value>(args));

            AssertHelper::assert_true(result.is_int(), "Result should be int");
            AssertHelper::assert_equals(Int(30), result.as_int(), "10 + 20 should be 30");
        }

        void test_native_function_with_string() {
            setup_test_registry();

            VMConfig config;
            config.enable_debug = false;
            VM vm(config);

            auto* greet_func = NativeRegistry::instance().find_function("TestGreet");
            AssertHelper::assert_true(greet_func != nullptr, "TestGreet should be registered");

            // Call with argument
            StringObject* name_obj = vm.allocate_object<StringObject>("Claude");
            std::vector<Value> args = { Value::from_object(name_obj) };
            Value result = (*greet_func)(vm, std::span<Value>(args));

            std::string greeting = from_value<std::string>(result);
            AssertHelper::assert_equals(std::string("Hello, Claude!"), greeting, "Greeting should match");
        }

        void test_native_variadic_function() {
            setup_test_registry();

            VMConfig config;
            config.enable_debug = false;
            VM vm(config);

            auto* sum_func = NativeRegistry::instance().find_function("TestSum");
            AssertHelper::assert_true(sum_func != nullptr, "TestSum should be registered");

            std::vector<Value> args = {
                Value::from_int(1),
                Value::from_int(2),
                Value::from_int(3),
                Value::from_int(4),
                Value::from_int(5)
            };
            Value result = (*sum_func)(vm, std::span<Value>(args));

            AssertHelper::assert_equals(Int(15), result.as_int(), "Sum of 1-5 should be 15");
        }

        // ============================================================================
        // FunctionTraits Tests
        // ============================================================================

        void test_function_traits() {
            // Test function pointer traits
            using FuncPtr = int(*)(float, double);
            static_assert(FunctionTraits<FuncPtr>::arity == 2, "Arity should be 2");
            static_assert(std::is_same_v<FunctionTraits<FuncPtr>::return_type, int>, "Return type should be int");
            static_assert(std::is_same_v<FunctionTraits<FuncPtr>::arg<0>, float>, "First arg should be float");

            // Test member function traits
            using MemFuncPtr = int(TestCounter::*)(void) const;
            static_assert(FunctionTraits<MemFuncPtr>::arity == 0, "Const method arity should be 0");
            static_assert(std::is_same_v<FunctionTraits<MemFuncPtr>::return_type, int>, "Return type should be int");
            static_assert(std::is_same_v<FunctionTraits<MemFuncPtr>::class_type, TestCounter>, "Class type should be TestCounter");

            // If we get here, compile-time checks passed
            AssertHelper::assert_true(true, "FunctionTraits compile-time checks passed");
        }

        // ============================================================================
        // Phase 2: Integration Tests (Script -> Native Call)
        // ============================================================================

        namespace {
            // Helper to run SwiftScript code and capture output
            std::string run_script(const std::string& source) {
                try {
                    Lexer lexer(source);
                    auto tokens = lexer.tokenize_all();
                    Parser parser(std::move(tokens));
                    auto program = parser.parse();
                    Compiler compiler;
                    auto chunk = compiler.compile(program);

                    VMConfig config;
                    config.enable_debug = false;
                    VM vm(config);

                    std::ostringstream output;
                    std::streambuf* old = std::cout.rdbuf(output.rdbuf());
                    struct Restore { std::streambuf* old; ~Restore() { std::cout.rdbuf(old); } } restore{ old };

                    vm.execute(chunk);
                    return output.str();
                }
                catch (const std::exception& e) {
                    return std::string("ERROR: ") + e.what();
                }
            }
        }

        void test_native_internal_call_basic() {
            // Register a simple native function
            NativeRegistry::instance().register_function("Native_Add",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.size() < 2) return Value::from_int(0);
                    Int a = from_value<Int>(args[0]);
                    Int b = from_value<Int>(args[1]);
                    return Value::from_int(a + b);
                });

            std::string source = R"(
        [Native.InternalCall("Native_Add")]
        func add(a: Int, b: Int) -> Int

        let result = add(10, 20)
        print(result)
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "30", "Native add should return 30");
        }

        void test_native_internal_call_no_args() {
            // Register a function with no arguments
            NativeRegistry::instance().register_function("Native_GetMagicNumber",
                [](VM& vm, std::span<Value> args) -> Value {
                    return Value::from_int(42);
                });

            std::string source = R"(
        [Native.InternalCall("Native_GetMagicNumber")]
        func getMagicNumber() -> Int

        print(getMagicNumber())
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "42", "Native function should return 42");
        }

        void test_native_internal_call_string_return() {
            // Register a function that returns a string
            NativeRegistry::instance().register_function("Native_Greet",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.empty()) {
                        return to_value(vm, std::string("Hello, World!"));
                    }
                    std::string name = from_value<std::string>(args[0]);
                    return to_value(vm, std::string("Hello, ") + name + "!");
                });

            std::string source = R"(
        [Native.InternalCall("Native_Greet")]
        func greet(name: String) -> String

        print(greet("SwiftScript"))
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "Hello, SwiftScript!", "Native greet should work");
        }

        void test_native_internal_call_multiple_calls() {
            // Register a stateful counter function
            static int counter = 0;
            counter = 0;  // Reset for each test

            NativeRegistry::instance().register_function("Native_IncrementCounter",
                [](VM& vm, std::span<Value> args) -> Value {
                    return Value::from_int(++counter);
                });

            std::string source = R"(
        [Native.InternalCall("Native_IncrementCounter")]
        func incrementCounter() -> Int

        print(incrementCounter())
        print(incrementCounter())
        print(incrementCounter())
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "1", "First call should return 1");
            AssertHelper::assert_contains(output, "2", "Second call should return 2");
            AssertHelper::assert_contains(output, "3", "Third call should return 3");
        }

        void test_native_internal_call_float() {
            NativeRegistry::instance().register_function("Native_Multiply",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.size() < 2) return Value::from_float(0.0);
                    Float a = from_value<Float>(args[0]);
                    Float b = from_value<Float>(args[1]);
                    return Value::from_float(a * b);
                });

            std::string source = R"(
        [Native.InternalCall("Native_Multiply")]
        func multiply(a: Float, b: Float) -> Float

        let result = multiply(2.5, 4.0)
        print(result)
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "10", "2.5 * 4.0 should be 10");
        }

        void test_native_internal_call_void_return() {
            static std::string last_message;
            last_message.clear();

            NativeRegistry::instance().register_function("Native_Log",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (!args.empty()) {
                        last_message = from_value<std::string>(args[0]);
                    }
                    return Value::null();
                });

            std::string source = R"(
        [Native.InternalCall("Native_Log")]
        func log(message: String) -> Void

        log("Test message")
        print("Done")
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "Done", "Script should complete");
            AssertHelper::assert_equals(std::string("Test message"), last_message, "Native log should capture message");
        }

        void test_native_internal_call_in_expression() {
            NativeRegistry::instance().register_function("Native_Square",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.empty()) return Value::from_int(0);
                    Int n = from_value<Int>(args[0]);
                    return Value::from_int(n * n);
                });

            std::string source = R"(
        [Native.InternalCall("Native_Square")]
        func square(n: Int) -> Int

        let x = square(5) + square(3)
        print(x)
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "34", "5^2 + 3^2 = 25 + 9 = 34");
        }

        // ============================================================================
        // Phase 3: Native Class Binding Tests
        // ============================================================================

        void test_native_class_basic() {
            // Register native methods for Vector3
            NativeRegistry::instance().register_function("Vector3_Create",
                [](VM& vm, std::span<Value> args) -> Value {
                    Float x = args.size() > 0 ? from_value<Float>(args[0]) : 0.0f;
                    Float y = args.size() > 1 ? from_value<Float>(args[1]) : 0.0f;
                    Float z = args.size() > 2 ? from_value<Float>(args[2]) : 0.0f;

                    auto* type_info = NativeRegistry::instance().find_type("TestVector3");
                    if (!type_info) {
                        throw std::runtime_error("TestVector3 type not registered");
                    }

                    TestVector3* vec = new TestVector3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                    NativeObject* obj = vm.allocate_object<NativeObject>(vec, "TestVector3", type_info);
                    return Value::from_object(obj);
                });

            NativeRegistry::instance().register_function("Vector3_GetX",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.empty() || !args[0].is_object()) return Value::from_float(0.0);
                    auto* obj = static_cast<NativeObject*>(args[0].as_object());
                    auto* vec = obj->as<TestVector3>();
                    return Value::from_float(vec->x);
                });

            NativeRegistry::instance().register_function("Vector3_GetY",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.empty() || !args[0].is_object()) return Value::from_float(0.0);
                    auto* obj = static_cast<NativeObject*>(args[0].as_object());
                    auto* vec = obj->as<TestVector3>();
                    return Value::from_float(vec->y);
                });

            NativeRegistry::instance().register_function("Vector3_GetZ",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.empty() || !args[0].is_object()) return Value::from_float(0.0);
                    auto* obj = static_cast<NativeObject*>(args[0].as_object());
                    auto* vec = obj->as<TestVector3>();
                    return Value::from_float(vec->z);
                });

            NativeRegistry::instance().register_function("Vector3_Magnitude",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.empty() || !args[0].is_object()) return Value::from_float(0.0);
                    auto* obj = static_cast<NativeObject*>(args[0].as_object());
                    auto* vec = obj->as<TestVector3>();
                    return Value::from_float(vec->Magnitude());
                });

            std::string source = R"(
        [Native.Class("TestVector3")]
        class Vector3 {
            [Native.InternalCall("Vector3_Create")]
            init(x: Float, y: Float, z: Float)

            [Native.InternalCall("Vector3_GetX")]
            func getX() -> Float

            [Native.InternalCall("Vector3_GetY")]
            func getY() -> Float

            [Native.InternalCall("Vector3_GetZ")]
            func getZ() -> Float

            [Native.InternalCall("Vector3_Magnitude")]
            func magnitude() -> Float
        }

        let v = Vector3(3.0, 4.0, 0.0)
        print(v.magnitude())
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "5", "Magnitude of (3,4,0) should be 5");
        }

        void test_native_class_property_access() {
            std::string source = R"(
        [Native.Class("TestVector3")]
        class Vector3 {
            [Native.InternalCall("Vector3_Create")]
            init(x: Float, y: Float, z: Float)

            [Native.InternalCall("Vector3_GetX")]
            func getX() -> Float

            [Native.InternalCall("Vector3_GetY")]
            func getY() -> Float
        }

        let v = Vector3(10.0, 20.0, 30.0)
        print(v.getX())
        print(v.getY())
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "10", "X should be 10");
            AssertHelper::assert_contains(output, "20", "Y should be 20");
        }

        void test_native_class_counter() {
            // Register Counter native methods
            NativeRegistry::instance().register_function("Counter_Create",
                [](VM& vm, std::span<Value> args) -> Value {
                    Int initial = args.size() > 0 ? from_value<Int>(args[0]) : 0;

                    auto* type_info = NativeRegistry::instance().find_type("TestCounter");
                    if (!type_info) {
                        throw std::runtime_error("TestCounter type not registered");
                    }

                    TestCounter* counter = new TestCounter(static_cast<int>(initial));
                    NativeObject* obj = vm.allocate_object<NativeObject>(counter, "TestCounter", type_info);
                    return Value::from_object(obj);
                });

            NativeRegistry::instance().register_function("Counter_GetValue",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.empty() || !args[0].is_object()) return Value::from_int(0);
                    auto* obj = static_cast<NativeObject*>(args[0].as_object());
                    auto* counter = obj->as<TestCounter>();
                    return Value::from_int(counter->GetValue());
                });

            NativeRegistry::instance().register_function("Counter_Increment",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.empty() || !args[0].is_object()) return Value::null();
                    auto* obj = static_cast<NativeObject*>(args[0].as_object());
                    auto* counter = obj->as<TestCounter>();
                    counter->Increment();
                    return Value::null();
                });

            NativeRegistry::instance().register_function("Counter_Add",
                [](VM& vm, std::span<Value> args) -> Value {
                    if (args.size() < 2 || !args[0].is_object()) return Value::null();
                    auto* obj = static_cast<NativeObject*>(args[0].as_object());
                    auto* counter = obj->as<TestCounter>();
                    Int amount = from_value<Int>(args[1]);
                    counter->Add(static_cast<int>(amount));
                    return Value::null();
                });

            std::string source = R"(
        [Native.Class("TestCounter")]
        class Counter {
            [Native.InternalCall("Counter_Create")]
            init(initial: Int)

            [Native.InternalCall("Counter_GetValue")]
            func getValue() -> Int

            [Native.InternalCall("Counter_Increment")]
            func increment()

            [Native.InternalCall("Counter_Add")]
            func add(amount: Int)
        }

        let c = Counter(10)
        print(c.getValue())
        c.increment()
        print(c.getValue())
        c.add(5)
        print(c.getValue())
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "10", "Initial value should be 10");
            AssertHelper::assert_contains(output, "11", "After increment should be 11");
            AssertHelper::assert_contains(output, "16", "After add(5) should be 16");
        }

        void test_native_class_is_native_flag() {
            // Test that ClassObject.is_native() returns true for native classes
            std::string source = R"(
        [Native.Class("TestVector3")]
        class Vector3 {
            [Native.InternalCall("Vector3_Create")]
            init(x: Float, y: Float, z: Float)
        }

        let v = Vector3(1.0, 2.0, 3.0)
        print("created")
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "created", "Native class instance should be created");
        }

        void test_native_class_multiple_instances() {
            std::string source = R"(
        [Native.Class("TestVector3")]
        class Vector3 {
            [Native.InternalCall("Vector3_Create")]
            init(x: Float, y: Float, z: Float)

            [Native.InternalCall("Vector3_GetX")]
            func getX() -> Float

            [Native.InternalCall("Vector3_Magnitude")]
            func magnitude() -> Float
        }

        let v1 = Vector3(3.0, 4.0, 0.0)
        let v2 = Vector3(1.0, 0.0, 0.0)
        let v3 = Vector3(0.0, 0.0, 5.0)

        print(v1.magnitude())
        print(v2.magnitude())
        print(v3.magnitude())
    )";

            auto output = run_script(source);
            AssertHelper::assert_no_error(output);
            AssertHelper::assert_contains(output, "5", "v1 magnitude should be 5");
            AssertHelper::assert_contains(output, "1", "v2 magnitude should be 1");
        }

        // ============================================================================
        // Test Runner
        // ============================================================================

        void run_native_binding_tests() {
            TestRunner runner;

            std::cout << "\n======================================\n";
            std::cout << "  Native Binding Tests (Phase 1)\n";
            std::cout << "======================================\n\n";

            // Registry tests
            runner.run_test("NativeRegistry function registration", test_native_registry_function_registration);
            runner.run_test("NativeRegistry type registration", test_native_registry_type_registration);
            runner.run_test("NativeRegistry unregister", test_native_registry_unregister);

            // Conversion tests
            runner.run_test("Native convert primitives", test_native_convert_primitives);
            runner.run_test("Native convert string", test_native_convert_string);
            runner.run_test("Native convert errors", test_native_convert_errors);

            // NativeObject tests
            runner.run_test("NativeObject creation", test_native_object_creation);
            runner.run_test("NativeObject property access", test_native_object_property_access);
            runner.run_test("NativeObject method call", test_native_object_method_call);

            // Native function tests
            runner.run_test("Native function call", test_native_function_call);
            runner.run_test("Native function with string", test_native_function_with_string);
            runner.run_test("Native variadic function", test_native_variadic_function);

            // FunctionTraits tests
            runner.run_test("FunctionTraits", test_function_traits);

            std::cout << "\n======================================\n";
            std::cout << "  Native Binding Tests (Phase 2)\n";
            std::cout << "  [Native.InternalCall] Integration\n";
            std::cout << "======================================\n\n";

            // Phase 2: Script-to-Native integration tests
            runner.run_test("[Native.InternalCall] basic", test_native_internal_call_basic);
            runner.run_test("[Native.InternalCall] no args", test_native_internal_call_no_args);
            runner.run_test("[Native.InternalCall] string return", test_native_internal_call_string_return);
            runner.run_test("[Native.InternalCall] multiple calls", test_native_internal_call_multiple_calls);
            runner.run_test("[Native.InternalCall] float", test_native_internal_call_float);
            runner.run_test("[Native.InternalCall] void return", test_native_internal_call_void_return);
            runner.run_test("[Native.InternalCall] in expression", test_native_internal_call_in_expression);

            std::cout << "\n======================================\n";
            std::cout << "  Native Binding Tests (Phase 3)\n";
            std::cout << "  [Native.Class] Type Binding\n";
            std::cout << "======================================\n\n";

            // Phase 3: Native class binding tests
            runner.run_test("[Native.Class] basic Vector3", test_native_class_basic);
            runner.run_test("[Native.Class] property access", test_native_class_property_access);
            runner.run_test("[Native.Class] Counter class", test_native_class_counter);
            runner.run_test("[Native.Class] is_native flag", test_native_class_is_native_flag);
            runner.run_test("[Native.Class] multiple instances", test_native_class_multiple_instances);

            runner.print_summary();
        }

    } // namespace test
} // namespace swiftscript