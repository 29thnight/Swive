#include "pch_.h"
#include "ss_project.hpp"
#include "ss_project_resolver.hpp"
#include "ss_vm.hpp"
#include "ss_chunk.hpp"
#include "ss_native_registry.hpp"
#include "ss_native_convert.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace swiftscript
{
    // .ssasm ���Ͽ��� Chunk�� �о� VM���� ����
    inline Value AssmblyRun(VM& vm, const std::string& ssasm_path)
    {
        std::ifstream in(ssasm_path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot open .ssasm file: " + ssasm_path);
        }
        Assembly chunk = Assembly::deserialize(in);
        return vm.execute(chunk);
    }
}

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

void test_vector3_native_binding() {
    using namespace swiftscript;
    auto& registry = NativeRegistry::instance();
    registry.clear();

    // 타입 등록 (기존 코드)
    NativeTypeInfo vector3_info;
    // ... 기존 타입 정보 ...
    registry.register_type("TestVector3", std::move(vector3_info));

    // 함수 등록 추가
    registry.register_function("Vector3_Create", [](VM& vm, std::span<Value> args) -> Value {
        if (args.size() != 3) {
            throw std::runtime_error("Vector3_Create requires 3 arguments");
        }
        float x = from_value<float>(args[0]);
        float y = from_value<float>(args[1]);
        float z = from_value<float>(args[2]);

        auto* native_obj = vm.allocate_object<NativeObject>(new TestVector3(x, y, z), "TestVector3");
        return Value::from_object(native_obj);
        });

    registry.register_function("Vector3_GetX", [](VM& vm, std::span<Value> args) -> Value {
        if (args.size() != 1 || !args[0].is_object()) {
            throw std::runtime_error("Vector3_GetX requires self argument");
        }
        auto* native_obj = static_cast<NativeObject*>(args[0].as_object());
        auto* vec = static_cast<TestVector3*>(native_obj->native_ptr);
        return Value::from_float(vec->x);
        });

    registry.register_function("Vector3_SetX", [](VM& vm, std::span<Value> args) -> Value {
        if (args.size() != 2 || !args[0].is_object()) {
			throw std::runtime_error("Vector3_SetX requires self and value arguments");
        }
        auto* native_obj = static_cast<NativeObject*>(args[0].as_object());
        auto* vec = static_cast<TestVector3*>(native_obj->native_ptr);
        float new_x = from_value<float>(args[1]);
        vec->x = new_x;
        return Value::null();
        });

    registry.register_function("Vector3_GetY", [](VM& vm, std::span<Value> args) -> Value {
        if (args.size() != 1 || !args[0].is_object()) {
            throw std::runtime_error("Vector3_GetY requires self argument");
        }
        auto* native_obj = static_cast<NativeObject*>(args[0].as_object());
        auto* vec = static_cast<TestVector3*>(native_obj->native_ptr);
        return Value::from_float(vec->y);
        });

    registry.register_function("Vector3_SetY", [](VM& vm, std::span<Value> args) -> Value {
		if (args.size() != 2 || !args[0].is_object()) {
            throw std::runtime_error("Vector3_SetY requires self and value arguments");
        }
        auto* native_obj = static_cast<NativeObject*>(args[0].as_object());
        auto* vec = static_cast<TestVector3*>(native_obj->native_ptr);
        float new_y = from_value<float>(args[1]);
        vec->y = new_y;
        return Value::null();
		});

    registry.register_function("Vector3_GetZ", [](VM& vm, std::span<Value> args) -> Value {
        if (args.size() != 1 || !args[0].is_object()) {
            throw std::runtime_error("Vector3_GetZ requires self argument");
        }
        auto* native_obj = static_cast<NativeObject*>(args[0].as_object());
        auto* vec = static_cast<TestVector3*>(native_obj->native_ptr);
        return Value::from_float(vec->z);
        });

	registry.register_function("Vector3_SetZ", [](VM& vm, std::span<Value> args) -> Value {
        if (args.size() != 2 || !args[0].is_object()) {
            throw std::runtime_error("Vector3_SetZ requires self and value arguments");
        }
        auto* native_obj = static_cast<NativeObject*>(args[0].as_object());
        auto* vec = static_cast<TestVector3*>(native_obj->native_ptr);
        float new_z = from_value<float>(args[1]);
        vec->z = new_z;
		return Value::null();
		});

    registry.register_function("Vector3_Magnitude", [](VM& vm, std::span<Value> args) -> Value {
        if (args.size() != 1 || !args[0].is_object()) {
            throw std::runtime_error("Vector3_Magnitude requires self argument");
        }
        auto* native_obj = static_cast<NativeObject*>(args[0].as_object());
        auto* vec = static_cast<TestVector3*>(native_obj->native_ptr);
        return Value::from_float(vec->Magnitude());
        });
}


int main(int argc, char* argv[]) {
    using namespace swiftscript;

    test_vector3_native_binding();

    if (argc < 2) {
        std::cerr << "Usage: SwiftScriptVM <program>.ssasm\n";
        return 1;
    }

    std::string ssasm_path = argv[1];

    try {
        VM vm;
        Value result = AssmblyRun(vm, ssasm_path);
        std::cout << "Program finished. Return value: " << result.to_string() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "VM error: " << e.what() << std::endl;
        return 2;
    }

    return 0;
}