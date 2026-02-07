# SwiftScript Native Binding Implementation Plan

## Overview

C++ 타입, 함수, 멤버 변수를 SwiftScript에 노출하는 네이티브 바인딩 시스템 구현 계획서입니다.

### 목표
- `[Native.InternalCall("함수이름")]` 어트리뷰트로 C++ 함수 호출
- `[Native.Class("타입이름")]` / `[Native.Struct("타입이름")]` 으로 C++ 타입 바인딩
- `[Native.Property("이름")]` / `[Native.Field("이름")]` 으로 멤버 접근
- Fluent API를 통한 간편한 C++ 측 등록

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           SwiftScript 측 (스크립트)                           │
├─────────────────────────────────────────────────────────────────────────────┤
│  [Native.Class("UnityEngine.Transform")]                                     │
│  class Transform {                                                           │
│      [Native.Property("position")]                                           │
│      var position: Vector3 { get set }                                       │
│                                                                              │
│      [Native.InternalCall("Transform_Translate")]                            │
│      func Translate(x: Float, y: Float, z: Float) -> Void                    │
│  }                                                                           │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
                            ┌─────────────────┐
                            │   Compiler      │
                            │  (어트리뷰트    │
                            │   메타데이터    │
                            │   생성)         │
                            └────────┬────────┘
                                     │
                                     ▼
                            ┌─────────────────┐
                            │   Assembly      │
                            │  (바이트코드 +  │
                            │   네이티브 메타)│
                            └────────┬────────┘
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              VM Runtime                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐       │
│  │  NativeRegistry  │◄───│   NativeObject   │◄───│  OpCode Handler  │       │
│  │  (함수/타입 등록)│    │   (래퍼 객체)    │    │ (OP_NATIVE_CALL) │       │
│  └──────────────────┘    └──────────────────┘    └──────────────────┘       │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          C++ Native Code                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│  NativeBinder::RegisterClass<Transform>("UnityEngine.Transform")             │
│      .Property("position", &Transform::GetPosition, &Transform::SetPosition) │
│      .Method("Transform_Translate", &Transform::Translate);                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Foundation Infrastructure

### 1.1 NativeObject 클래스 추가

**파일:** `src/common/ss_value.hpp`

```cpp
// C++ 네이티브 객체를 감싸는 래퍼
class NativeObject : public Object {
public:
    void* native_ptr;              // 실제 C++ 객체 포인터
    std::string type_name;         // 등록된 타입 이름 (예: "UnityEngine.Transform")
    bool prevent_release{false};   // GC가 native_ptr을 해제하지 않도록 함

    NativeObject(void* ptr, const std::string& type);
    ~NativeObject();

    std::string to_string() const override;
    size_t memory_size() const override;
};
```

**작업 내용:**
- [ ] `ObjectType::Native` 열거형 추가 (`ss_core.hpp`)
- [ ] `NativeObject` 클래스 구현 (`ss_value.hpp`)
- [ ] `object_type_name()` 함수에 Native 케이스 추가

### 1.2 NativeRegistry 시스템

**새 파일:** `src/common/ss_native_registry.hpp`, `ss_native_registry.cpp`

```cpp
namespace swiftscript {

// 네이티브 함수 시그니처
using NativeFunction = std::function<Value(VM&, std::span<Value>)>;
using NativeMethod = std::function<Value(VM&, void*, std::span<Value>)>;
using NativeGetter = std::function<Value(VM&, void*)>;
using NativeSetter = std::function<void(VM&, void*, Value)>;

// 프로퍼티 정보
struct NativePropertyInfo {
    std::string name;
    NativeGetter getter;
    NativeSetter setter;  // nullptr이면 읽기 전용
};

// 메서드 정보
struct NativeMethodInfo {
    std::string name;
    NativeMethod func;
    int param_count;
};

// 타입 정보
struct NativeTypeInfo {
    std::string name;
    size_t size;
    std::function<void*(void)> constructor;
    std::function<void(void*)> destructor;
    std::unordered_map<std::string, NativePropertyInfo> properties;
    std::unordered_map<std::string, NativeMethodInfo> methods;
    bool is_value_type{false};  // struct면 true, class면 false
};

// 중앙 레지스트리
class NativeRegistry {
public:
    static NativeRegistry& instance();

    // 함수 등록/조회
    void register_function(const std::string& name, NativeFunction func);
    NativeFunction* find_function(const std::string& name);

    // 타입 등록/조회
    void register_type(const std::string& name, NativeTypeInfo info);
    NativeTypeInfo* find_type(const std::string& name);

    // 편의 메서드
    bool has_function(const std::string& name) const;
    bool has_type(const std::string& name) const;

private:
    std::unordered_map<std::string, NativeFunction> functions_;
    std::unordered_map<std::string, NativeTypeInfo> types_;
};

} // namespace swiftscript
```

**작업 내용:**
- [ ] `NativeRegistry` 싱글톤 구현
- [ ] 함수 등록/조회 메커니즘
- [ ] 타입 정보 등록/조회 메커니즘
- [ ] 스레드 안전성 고려 (필요시)

### 1.3 Value 타입 변환 유틸리티

**새 파일:** `src/common/ss_native_convert.hpp`

```cpp
namespace swiftscript {

// C++ 타입 -> Value 변환
template<typename T>
Value to_value(VM& vm, const T& value);

// 특수화
template<> Value to_value(VM& vm, const int& value);
template<> Value to_value(VM& vm, const int64_t& value);
template<> Value to_value(VM& vm, const float& value);
template<> Value to_value(VM& vm, const double& value);
template<> Value to_value(VM& vm, const bool& value);
template<> Value to_value(VM& vm, const std::string& value);

// Value -> C++ 타입 변환
template<typename T>
T from_value(const Value& value);

// 특수화
template<> int from_value(const Value& value);
template<> int64_t from_value(const Value& value);
template<> float from_value(const Value& value);
template<> double from_value(const Value& value);
template<> bool from_value(const Value& value);
template<> std::string from_value(const Value& value);

// 네이티브 객체 포인터 추출
template<typename T>
T* from_native_value(const Value& value);

} // namespace swiftscript
```

**작업 내용:**
- [ ] 기본 타입 변환 함수 구현
- [ ] 객체 타입 변환 (String, List, Map 등)
- [ ] NativeObject에서 포인터 추출
- [ ] 에러 처리 (타입 불일치 시)

---

## Phase 2: Function Binding ([Native.InternalCall])

### 2.1 OpCode 추가

**파일:** `src/common/ss_opcodes.def`

```cpp
// 기존 opcodes 아래에 추가
X(OP_NATIVE_CALL)          // 네이티브 함수 호출
```

### 2.2 컴파일러 수정

**파일:** `src/common/ss_compiler.hpp`, `ss_compiler.cpp`

```cpp
// 어트리뷰트 처리를 위한 헬퍼
struct NativeCallInfo {
    std::string native_name;
    bool is_valid{false};
};

class Compiler {
    // ... 기존 코드 ...

    // Native 어트리뷰트 파싱
    NativeCallInfo extract_native_call_attribute(const std::vector<Attribute>& attrs);

    // FuncDeclStmt 처리 시 Native 어트리뷰트 확인
    void visit(FuncDeclStmt* stmt);  // 수정
};
```

**컴파일러 로직 변경:**
```cpp
void Compiler::visit(FuncDeclStmt* stmt) {
    auto native_info = extract_native_call_attribute(stmt->attributes);

    if (native_info.is_valid) {
        // 네이티브 함수: 본문 컴파일 대신 메타데이터만 기록
        // OP_NATIVE_CALL을 emit하도록 설정
        emit_native_function_stub(stmt, native_info.native_name);
    } else {
        // 기존 일반 함수 컴파일 로직
        // ...
    }
}
```

**작업 내용:**
- [ ] `extract_native_call_attribute()` 함수 구현
- [ ] 네이티브 함수용 스텁 생성 로직
- [ ] Assembly에 네이티브 함수 메타데이터 저장

### 2.3 VM OpCode 핸들러

**파일:** `src/common/ss_vm_opcodes.inl` 또는 새 파일 `ss_vm_opcodes_native.inl`

```cpp
template<>
inline void VM::execute_op<OP_NATIVE_CALL>() {
    // 1. 함수 이름 인덱스 읽기
    uint16_t name_idx = read_short();
    std::string func_name = current_chunk()->get_string(name_idx);

    // 2. 인자 개수 읽기
    uint8_t arg_count = read_byte();

    // 3. NativeRegistry에서 함수 찾기
    auto* func = NativeRegistry::instance().find_function(func_name);
    if (!func) {
        throw runtime_error("Native function not found: " + func_name);
    }

    // 4. 스택에서 인자 수집
    std::vector<Value> args;
    args.reserve(arg_count);
    for (int i = arg_count - 1; i >= 0; --i) {
        args.push_back(peek(i));
    }
    std::reverse(args.begin(), args.end());

    // 5. 네이티브 함수 호출
    Value result = (*func)(*this, std::span<Value>(args));

    // 6. 스택 정리 및 결과 푸시
    pop_n(arg_count);
    push(result);
}
```

**작업 내용:**
- [ ] `OP_NATIVE_CALL` 핸들러 구현
- [ ] 인자 전달 메커니즘
- [ ] 반환값 처리
- [ ] 에러 핸들링

### 2.4 사용 예시

**C++ 측 등록:**
```cpp
void register_debug_functions() {
    NativeRegistry::instance().register_function(
        "Debug_Log",
        [](VM& vm, std::span<Value> args) -> Value {
            if (args.empty()) return Value::null();
            std::cout << args[0].to_string() << std::endl;
            return Value::null();
        }
    );
}
```

**SwiftScript 측:**
```swift
[Native.InternalCall("Debug_Log")]
func Log(message: String) -> Void

// 사용
Log("Hello from SwiftScript!")
```

---

## Phase 3: Type Binding ([Native.Class], [Native.Struct])

### 3.1 추가 OpCodes

**파일:** `src/common/ss_opcodes.def`

```cpp
X(OP_NATIVE_CONSTRUCT)     // 네이티브 객체 생성
X(OP_NATIVE_GET_PROPERTY)  // 네이티브 프로퍼티 읽기
X(OP_NATIVE_SET_PROPERTY)  // 네이티브 프로퍼티 쓰기
X(OP_NATIVE_GET_FIELD)     // 네이티브 필드 읽기
X(OP_NATIVE_SET_FIELD)     // 네이티브 필드 쓰기
X(OP_NATIVE_METHOD_CALL)   // 네이티브 메서드 호출
```

### 3.2 컴파일러 확장

**어트리뷰트 처리:**
```cpp
struct NativeTypeInfo {
    std::string native_type_name;
    bool is_class{false};
    bool is_struct{false};
};

struct NativePropertyInfo {
    std::string native_property_name;
    bool is_read_only{false};
};

struct NativeFieldInfo {
    std::string native_field_name;
};

// 컴파일러에 추가
NativeTypeInfo extract_native_type_attribute(const std::vector<Attribute>& attrs);
NativePropertyInfo extract_native_property_attribute(const std::vector<Attribute>& attrs);
NativeFieldInfo extract_native_field_attribute(const std::vector<Attribute>& attrs);
```

**ClassDeclStmt/StructDeclStmt 처리:**
```cpp
void Compiler::visit(ClassDeclStmt* stmt) {
    auto native_info = extract_native_type_attribute(stmt->attributes);

    if (native_info.is_class) {
        // 네이티브 클래스: 프로퍼티/메서드를 네이티브 바인딩으로 처리
        compile_native_class(stmt, native_info);
    } else {
        // 기존 일반 클래스 컴파일
        // ...
    }
}
```

**작업 내용:**
- [ ] `[Native.Class]`, `[Native.Struct]` 어트리뷰트 파싱
- [ ] `[Native.Property]`, `[Native.Field]` 어트리뷰트 파싱
- [ ] 네이티브 타입용 메타데이터 테이블 생성
- [ ] 프로퍼티/필드 접근 코드 생성

### 3.3 VM 핸들러 구현

```cpp
// 네이티브 객체 생성
template<>
inline void VM::execute_op<OP_NATIVE_CONSTRUCT>() {
    uint16_t type_idx = read_short();
    std::string type_name = current_chunk()->get_string(type_idx);

    auto* type_info = NativeRegistry::instance().find_type(type_name);
    if (!type_info || !type_info->constructor) {
        throw runtime_error("Cannot construct native type: " + type_name);
    }

    void* native_ptr = type_info->constructor();
    auto* obj = allocate_object<NativeObject>(native_ptr, type_name);
    push(Value::from_object(obj));
}

// 프로퍼티 읽기
template<>
inline void VM::execute_op<OP_NATIVE_GET_PROPERTY>() {
    uint16_t prop_idx = read_short();
    std::string prop_name = current_chunk()->get_string(prop_idx);

    Value receiver = pop();
    if (!receiver.is_object()) {
        throw runtime_error("Cannot get property of non-object");
    }

    auto* native_obj = dynamic_cast<NativeObject*>(receiver.as_object());
    if (!native_obj) {
        throw runtime_error("Not a native object");
    }

    auto* type_info = NativeRegistry::instance().find_type(native_obj->type_name);
    auto it = type_info->properties.find(prop_name);
    if (it == type_info->properties.end()) {
        throw runtime_error("Property not found: " + prop_name);
    }

    Value result = it->second.getter(*this, native_obj->native_ptr);
    push(result);
}

// 프로퍼티 쓰기
template<>
inline void VM::execute_op<OP_NATIVE_SET_PROPERTY>() {
    uint16_t prop_idx = read_short();
    std::string prop_name = current_chunk()->get_string(prop_idx);

    Value value = pop();
    Value receiver = pop();

    auto* native_obj = dynamic_cast<NativeObject*>(receiver.as_object());
    auto* type_info = NativeRegistry::instance().find_type(native_obj->type_name);
    auto it = type_info->properties.find(prop_name);

    if (!it->second.setter) {
        throw runtime_error("Property is read-only: " + prop_name);
    }

    it->second.setter(*this, native_obj->native_ptr, value);
}

// 메서드 호출
template<>
inline void VM::execute_op<OP_NATIVE_METHOD_CALL>() {
    uint16_t method_idx = read_short();
    uint8_t arg_count = read_byte();
    std::string method_name = current_chunk()->get_string(method_idx);

    // 스택: [receiver, arg0, arg1, ...argN]
    Value receiver = peek(arg_count);
    auto* native_obj = dynamic_cast<NativeObject*>(receiver.as_object());

    auto* type_info = NativeRegistry::instance().find_type(native_obj->type_name);
    auto it = type_info->methods.find(method_name);

    std::vector<Value> args;
    for (int i = arg_count - 1; i >= 0; --i) {
        args.push_back(peek(i));
    }
    std::reverse(args.begin(), args.end());

    Value result = it->second.func(*this, native_obj->native_ptr, std::span<Value>(args));

    pop_n(arg_count + 1);  // args + receiver
    push(result);
}
```

**작업 내용:**
- [ ] `OP_NATIVE_CONSTRUCT` 핸들러
- [ ] `OP_NATIVE_GET_PROPERTY` 핸들러
- [ ] `OP_NATIVE_SET_PROPERTY` 핸들러
- [ ] `OP_NATIVE_GET_FIELD` 핸들러
- [ ] `OP_NATIVE_SET_FIELD` 핸들러
- [ ] `OP_NATIVE_METHOD_CALL` 핸들러

---

## Phase 4: Fluent Binding API

### 4.1 ClassBinder 템플릿

**새 파일:** `src/common/ss_native_binder.hpp`

```cpp
namespace swiftscript {

template<typename T>
class ClassBinder {
public:
    explicit ClassBinder(const std::string& name) : info_{name, sizeof(T)} {
        info_.constructor = []() -> void* { return new T(); };
        info_.destructor = [](void* ptr) { delete static_cast<T*>(ptr); };
    }

    ~ClassBinder() {
        NativeRegistry::instance().register_type(info_.name, std::move(info_));
    }

    // 프로퍼티 바인딩 (getter + setter)
    template<typename Getter, typename Setter>
    ClassBinder& Property(const std::string& name, Getter get, Setter set) {
        info_.properties[name] = NativePropertyInfo{
            name,
            [get](VM& vm, void* obj) -> Value {
                return to_value(vm, (static_cast<T*>(obj)->*get)());
            },
            [set](VM& vm, void* obj, Value v) {
                using SetterParamType = std::decay_t<
                    typename function_traits<Setter>::template arg<0>::type>;
                (static_cast<T*>(obj)->*set)(from_value<SetterParamType>(v));
            }
        };
        return *this;
    }

    // 읽기 전용 프로퍼티
    template<typename Getter>
    ClassBinder& PropertyReadOnly(const std::string& name, Getter get) {
        info_.properties[name] = NativePropertyInfo{
            name,
            [get](VM& vm, void* obj) -> Value {
                return to_value(vm, (static_cast<T*>(obj)->*get)());
            },
            nullptr
        };
        return *this;
    }

    // 필드 직접 바인딩
    template<typename FieldType>
    ClassBinder& Field(const std::string& name, FieldType T::* field) {
        info_.properties[name] = NativePropertyInfo{
            name,
            [field](VM& vm, void* obj) -> Value {
                return to_value(vm, static_cast<T*>(obj)->*field);
            },
            [field](VM& vm, void* obj, Value v) {
                static_cast<T*>(obj)->*field = from_value<FieldType>(v);
            }
        };
        return *this;
    }

    // 메서드 바인딩
    template<typename Ret, typename... Args>
    ClassBinder& Method(const std::string& name, Ret(T::*method)(Args...)) {
        info_.methods[name] = NativeMethodInfo{
            name,
            [method](VM& vm, void* obj, std::span<Value> args) -> Value {
                return invoke_method(vm, static_cast<T*>(obj), method, args,
                                     std::index_sequence_for<Args...>{});
            },
            sizeof...(Args)
        };
        return *this;
    }

    // const 메서드
    template<typename Ret, typename... Args>
    ClassBinder& Method(const std::string& name, Ret(T::*method)(Args...) const) {
        info_.methods[name] = NativeMethodInfo{
            name,
            [method](VM& vm, void* obj, std::span<Value> args) -> Value {
                return invoke_method(vm, static_cast<T*>(obj), method, args,
                                     std::index_sequence_for<Args...>{});
            },
            sizeof...(Args)
        };
        return *this;
    }

    // 정적 메서드
    template<typename Ret, typename... Args>
    ClassBinder& StaticMethod(const std::string& name, Ret(*func)(Args...)) {
        // 정적 함수로 등록
        NativeRegistry::instance().register_function(
            info_.name + "_" + name,
            [func](VM& vm, std::span<Value> args) -> Value {
                return invoke_static(vm, func, args,
                                     std::index_sequence_for<Args...>{});
            }
        );
        return *this;
    }

private:
    NativeTypeInfo info_;

    // 메서드 호출 헬퍼
    template<typename Ret, typename... Args, size_t... Is>
    static Value invoke_method(VM& vm, T* obj, Ret(T::*method)(Args...),
                               std::span<Value> args, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<Ret>) {
            (obj->*method)(from_value<std::decay_t<Args>>(args[Is])...);
            return Value::null();
        } else {
            return to_value(vm, (obj->*method)(from_value<std::decay_t<Args>>(args[Is])...));
        }
    }
};

// 편의 함수
template<typename T>
ClassBinder<T> RegisterClass(const std::string& name) {
    return ClassBinder<T>(name);
}

template<typename T>
ClassBinder<T> RegisterStruct(const std::string& name) {
    auto binder = ClassBinder<T>(name);
    // 구조체는 값 타입으로 표시
    return binder;
}

} // namespace swiftscript
```

**작업 내용:**
- [ ] `ClassBinder` 템플릿 구현
- [ ] 메서드 호출 헬퍼 (`invoke_method`)
- [ ] `function_traits` 타입 특성 구현
- [ ] `RegisterClass` / `RegisterStruct` 편의 함수

### 4.2 사용 예시

**C++ 게임 엔진 타입:**
```cpp
struct Vector3 {
    float x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    float Magnitude() const {
        return std::sqrt(x*x + y*y + z*z);
    }

    void Normalize() {
        float m = Magnitude();
        if (m > 0) { x /= m; y /= m; z /= m; }
    }

    Vector3 operator+(const Vector3& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }
};

class Transform {
    Vector3 position_;
    Vector3 rotation_;
public:
    Vector3 GetPosition() const { return position_; }
    void SetPosition(const Vector3& v) { position_ = v; }

    Vector3 GetRotation() const { return rotation_; }

    void Translate(float x, float y, float z) {
        position_.x += x;
        position_.y += y;
        position_.z += z;
    }
};
```

**바인딩 등록:**
```cpp
void RegisterEngineBindings() {
    // Vector3 구조체
    RegisterStruct<Vector3>("UnityEngine.Vector3")
        .Field("x", &Vector3::x)
        .Field("y", &Vector3::y)
        .Field("z", &Vector3::z)
        .Method("Magnitude", &Vector3::Magnitude)
        .Method("Normalize", &Vector3::Normalize);

    // Transform 클래스
    RegisterClass<Transform>("UnityEngine.Transform")
        .Property("position", &Transform::GetPosition, &Transform::SetPosition)
        .PropertyReadOnly("rotation", &Transform::GetRotation)
        .Method("Translate", &Transform::Translate);
}
```

**SwiftScript 선언:**
```swift
// Vector3.ss
[Native.Struct("UnityEngine.Vector3")]
struct Vector3 {
    [Native.Field("x")]
    var x: Float

    [Native.Field("y")]
    var y: Float

    [Native.Field("z")]
    var z: Float

    [Native.InternalCall("Vector3_Magnitude")]
    func Magnitude() -> Float

    [Native.InternalCall("Vector3_Normalize")]
    mutating func Normalize() -> Void
}

// Transform.ss
[Native.Class("UnityEngine.Transform")]
class Transform {
    [Native.Property("position")]
    var position: Vector3 { get set }

    [Native.Property("rotation", readOnly: true)]
    var rotation: Vector3 { get }

    [Native.InternalCall("Transform_Translate")]
    func Translate(x: Float, y: Float, z: Float) -> Void
}

// 사용 예시
func movePlayer(transform: Transform) {
    var pos = transform.position
    pos.x += 10.0
    transform.position = pos

    transform.Translate(1.0, 0.0, 0.0)

    print(pos.Magnitude())
}
```

---

## Phase 5: Advanced Features (Optional)

### 5.1 콜백 지원

SwiftScript 클로저를 C++ 콜백으로 전달:

```cpp
// C++ 측
using Callback = std::function<void(int)>;
void SetOnClick(Callback cb);

// 바인딩
RegisterClass<Button>("UI.Button")
    .Method("SetOnClick", [](VM& vm, Button* btn, std::span<Value> args) {
        ClosureObject* closure = dynamic_cast<ClosureObject*>(args[0].as_object());
        btn->SetOnClick([&vm, closure](int value) {
            vm.call_closure(closure, {Value::from_int(value)});
        });
        return Value::null();
    });
```

### 5.2 상속 지원

네이티브 클래스를 SwiftScript에서 상속:

```swift
[Native.Class("Engine.Component")]
class Component {
    [Native.InternalCall("Component_Update")]
    func Update() -> Void
}

// SwiftScript에서 상속
class PlayerController: Component {
    override func Update() {
        // 스크립트 로직
        super.Update()
    }
}
```

### 5.3 제네릭 지원

```cpp
// C++ 제네릭 컨테이너
template<typename T>
class NativeList {
    std::vector<T> items;
public:
    void Add(const T& item) { items.push_back(item); }
    T Get(int index) const { return items[index]; }
};

// 특수화 바인딩
RegisterClass<NativeList<int>>("Collections.IntList")
    .Method("Add", &NativeList<int>::Add)
    .Method("Get", &NativeList<int>::Get);
```

---

## Implementation Checklist

### Phase 1: Foundation (예상: 2-3일)
- [ ] `ObjectType::Native` 추가
- [ ] `NativeObject` 클래스 구현
- [ ] `NativeRegistry` 싱글톤 구현
- [ ] 타입 변환 유틸리티 (`to_value`, `from_value`)
- [ ] 단위 테스트 작성

### Phase 2: Function Binding (예상: 2-3일)
- [ ] `OP_NATIVE_CALL` OpCode 추가
- [ ] 컴파일러: `[Native.InternalCall]` 어트리뷰트 파싱
- [ ] 컴파일러: 네이티브 함수 스텁 생성
- [ ] VM: `OP_NATIVE_CALL` 핸들러 구현
- [ ] 통합 테스트

### Phase 3: Type Binding (예상: 3-4일)
- [ ] 추가 OpCodes (`OP_NATIVE_CONSTRUCT`, `OP_NATIVE_GET/SET_PROPERTY` 등)
- [ ] 컴파일러: `[Native.Class/Struct]` 어트리뷰트 파싱
- [ ] 컴파일러: `[Native.Property/Field]` 어트리뷰트 파싱
- [ ] VM: 프로퍼티/필드 접근 핸들러
- [ ] VM: 메서드 호출 핸들러
- [ ] 통합 테스트

### Phase 4: Fluent API (예상: 2-3일)
- [ ] `ClassBinder` 템플릿 구현
- [ ] 메서드 호출 헬퍼 구현
- [ ] `function_traits` 타입 특성
- [ ] 예제 바인딩 작성
- [ ] 문서화

### Phase 5: Advanced (선택적)
- [ ] 콜백 지원
- [ ] 상속 지원
- [ ] 제네릭 지원

---

## File Changes Summary

| 파일 | 변경 내용 |
|------|----------|
| `ss_core.hpp` | `ObjectType::Native` 추가 |
| `ss_value.hpp` | `NativeObject` 클래스 추가 |
| `ss_opcodes.def` | Native 관련 OpCodes 추가 |
| `ss_compiler.hpp/cpp` | 어트리뷰트 파싱, 네이티브 코드 생성 |
| `ss_vm_opcodes.inl` | Native OpCode 핸들러 |
| **새 파일** `ss_native_registry.hpp/cpp` | NativeRegistry 구현 |
| **새 파일** `ss_native_convert.hpp` | 타입 변환 유틸리티 |
| **새 파일** `ss_native_binder.hpp` | Fluent API (ClassBinder) |

---

## Attribute Reference

| 어트리뷰트 | 대상 | 용도 | 예시 |
|-----------|------|------|------|
| `[Native.InternalCall("name")]` | 함수/메서드 | C++ 함수 호출 | `[Native.InternalCall("Debug_Log")]` |
| `[Native.Class("name")]` | 클래스 | C++ 클래스 바인딩 | `[Native.Class("Transform")]` |
| `[Native.Struct("name")]` | 구조체 | C++ 구조체 바인딩 | `[Native.Struct("Vector3")]` |
| `[Native.Property("name")]` | 프로퍼티 | getter/setter 바인딩 | `[Native.Property("position")]` |
| `[Native.Property("name", readOnly: true)]` | 프로퍼티 | 읽기 전용 | `[Native.Property("rotation", readOnly: true)]` |
| `[Native.Field("name")]` | 변수 | 필드 직접 접근 | `[Native.Field("x")]` |

---

## Notes

1. **메모리 관리**: `NativeObject`의 `native_ptr`은 기본적으로 `NativeTypeInfo::destructor`를 통해 해제됩니다. `prevent_release`가 true면 해제하지 않습니다 (외부 소유 객체).

2. **스레드 안전성**: 현재 설계는 단일 스레드를 가정합니다. 멀티스레드 지원 시 `NativeRegistry`에 뮤텍스 추가가 필요합니다.

3. **에러 처리**: 네이티브 함수에서 예외 발생 시 SwiftScript 예외로 변환하는 메커니즘이 필요합니다.

4. **성능**: 문자열 기반 조회는 해시맵을 사용하므로 O(1)이지만, 성능이 중요한 경우 인덱스 기반 조회로 최적화할 수 있습니다.
