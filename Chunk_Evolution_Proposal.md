# 제안: SwiftScript `Chunk`를 메타데이터 기반 어셈블리 포맷으로의 진화

## 1. 개요 및 동기

현재 SwiftScript의 `Chunk`는 주로 실행 가능한 바이트코드 명령어와 상수 목록을 담는 컨테이너 역할을 합니다. 이는 인터프리터 실행에는 효과적이지만, 언어의 기능을 확장하는 데 한계가 있습니다.

본 문서는 `Chunk` 구조를 확장하여, 타입 정보, 멤버, 관계 등 풍부한 **메타데이터**를 포함하는 자기 기술적인(self-describing) **어셈블리(Assembly)** 포맷으로 발전시키는 설계안을 제안합니다. 이 변화는 다음과 같은 고급 기능을 구현하기 위한 필수적인 기반이 됩니다.

*   **리플렉션 (Reflection)**: 런타임에 코드의 구조(타입, 메서드 등)를 분석하고 동적으로 상호작용합니다.
*   **향상된 툴링**: 디버거, 코드 자동 완성(IntelliSense) 등 개발 도구가 코드 구조를 정확히 이해할 수 있습니다.
*   **직렬화 (Serialization)**: 객체의 상태를 저장하고 복원하는 과정을 자동화합니다.
*   **AOP (Aspect-Oriented Programming)**: 어트리뷰트(Attribute)를 통해 코드에 부가적인 기능을 주입합니다.
*   **미래의 JIT 컴파일**: 최적화 컴파일에 필요한 충분한 정보를 제공합니다.

## 2. 새로운 최상위 구조: `Assembly`

컴파일과 배포의 새로운 기본 단위가 될 `Assembly`라는 최상위 컨테이너를 도입합니다. 하나의 `Assembly` 파일(`.ssasm` 확장자)은 다음과 같은 요소들을 포함하게 됩니다.

```cpp
struct Assembly {
    // 1. 어셈블리 자체에 대한 정보
    AssemblyManifest manifest;

    // 2. 이 어셈블리에 정의된 모든 타입의 메타데이터 테이블
    std::vector<TypeDef> type_definitions;

    // 3. 타입의 멤버들에 대한 메타데이터 테이블
    std::vector<MethodDef> method_definitions;
    std::vector<FieldDef> field_definitions;
    std::vector<PropertyDef> property_definitions;

    // 4. 어셈블리 전역에서 사용되는 리소스
    std::vector<Value> global_constant_pool; // 통합된 상수 풀
    std::vector<uint8_t> signature_blob;     // 메서드 시그니처 데이터

    // 5. 실제 바이트코드
    std::vector<MethodBody> method_bodies;
};
```

## 3. 메타데이터 테이블 상세 설계

### 3.1. `TypeDef` (타입 정의 테이블)

어셈블리에 정의된 모든 클래스, 구조체, 열거형, 인터페이스의 정보를 담습니다.

```cpp
enum TypeFlags {
    Public, Private,
    Class, Struct, Enum, Interface,
    Final, Abstract
};

struct TypeDef {
    string_idx name;             // 타입의 이름 (상수 풀 인덱스)
    string_idx namespace_name;   // 네임스페이스 (상수 풀 인덱스)
    TypeFlags flags;             // 타입의 특성을 나타내는 플래그
    type_idx base_type;          // 상속하는 부모 타입의 인덱스
    range method_list;           // 이 타입에 속한 메서드 목록 (MethodDef 테이블의 범위)
    range field_list;            // 이 타입에 속한 필드 목록 (FieldDef 테이블의 범위)
    std::vector<type_idx> interfaces; // 구현하는 인터페이스 목록
};
```

### 3.2. `MethodDef` (메서드 정의 테이블)

모든 메서드(전역 함수 포함)의 정보를 담습니다.

```cpp
enum MethodFlags {
    Static, Virtual, Override,
    Mutating // for structs
};

struct MethodDef {
    string_idx name;        // 메서드 이름 (상수 풀 인덱스)
    MethodFlags flags;      // 메서드 특성 플래그
    signature_idx signature; // 메서드 시그니처 인덱스 (파라미터, 반환 타입)
    body_idx body_ptr;      // 실제 바이트코드가 저장된 MethodBody의 인덱스
};
```

### 3.3. `FieldDef` (필드 정의 테이블)

클래스나 구조체에 속한 멤버 변수의 정보를 담습니다.

```cpp
struct FieldDef {
    string_idx name;  // 필드 이름
    FieldFlags flags; // Public, Private, Static 등
    type_idx type;    // 필드의 타입 인덱스
};
```

## 4. `Chunk`의 재정의: `MethodBody`

기존의 `Chunk`는 `MethodBody`로 재정의되어, 특정 메서드 하나에 대한 바이트코드와 지역 정보를 담는 역할에 집중합니다.

```cpp
// 기존 Chunk와 유사하지만, 상수 풀은 전역 풀을 참조하도록 변경
struct MethodBody {
    std::vector<uint8_t> bytecode;     // 바이트코드 명령어
    std::vector<uint32_t> line_info;   // 디버깅을 위한 라인 정보
    uint32_t max_stack_depth;          // 이 메서드가 필요로 하는 최대 스택 깊이 (JIT/AOT에 유용)
    // 지역 변수 디버그 정보 (선택적)
};
```

`MethodBody`는 더 이상 자체적인 상수 풀을 가지지 않고, `OP_CONSTANT` 명령어의 피연산자로 `Assembly`의 `global_constant_pool` 인덱스를 사용합니다. 이를 통해 중복을 제거하고 메모리를 절약합니다.

## 5. 컴파일러와 VM 변경 사항

### 5.1. 컴파일러 (`Compiler`)

*   **메타데이터 빌더 추가**: 컴파일 과정에서 소스 코드를 분석하여 `TypeDef`, `MethodDef` 등의 메타데이터 테이블을 생성하는 새로운 단계가 필요합니다.
*   **어셈블리 생성**: 컴파일의 최종 결과물은 더 이상 단일 `Chunk`가 아닌, 모든 메타데이터와 바이트코드를 포함하는 완전한 `Assembly` 객체가 됩니다.

### 5.2. 가상 머신 (`VM`)

*   **로더 변경**: VM은 시작 시 `Assembly` 전체를 메모리에 로드합니다.
*   **메서드 호출 방식 변경**:
    1. `OP_CALL` 명령어를 만나면, 피연산자로 받은 `method_idx`를 이용해 `MethodDef` 테이블을 조회합니다.
    2. `MethodDef`에서 `body_ptr`를 얻어 실제 바이트코드가 담긴 `MethodBody`를 찾습니다.
    3. `MethodDef`의 `signature` 정보를 통해 인자 개수와 타입을 검증합니다.
    4. 새로운 `CallFrame`을 생성하고 `MethodBody`의 바이트코드를 실행합니다.
*   **타입 검사 활용**: `OP_CAST`, `OP_IS_TYPE` 등의 명령어는 이제 메타데이터 테이블을 조회하여 런타임에 정확하고 풍부한 타입 검사를 수행합니다.

## 6. 단계적 구현 로드맵

이 거대한 변화를 관리하기 위해 다음과 같은 단계적 접근을 제안합니다.

*   **1단계: 기본 어셈블리 구조 도입**
    *   `Assembly`와 최소한의 `TypeDef`, `MethodDef` 구조를 구현합니다.
    *   컴파일러와 VM이 이 새로운 구조를 통해 코드를 로드하고 실행하도록 수정합니다. (기능은 기존과 동일)
*   **2단계: 풍부한 메타데이터 확장**
    *   상속, 인터페이스, 플래그 등 모든 메타데이터 필드를 구체적으로 구현하고 컴파일러가 해당 정보를 채우도록 합니다.
    *   VM이 타입 검사와 가상 메서드 호출에 메타데이터를 활용하도록 수정합니다.
*   **3단계: 리플렉션 API 구현**
    *   로드된 메타데이터에 접근할 수 있는 `getType(string)`, `getMethods()`, `invoke()`와 같은 리플렉션 API를 제공합니다.
*   **4단계: 바이너리 파일 포맷 정의 및 구현**
    *   `Assembly` 구조를 디스크에 저장하고 읽을 수 있는 표준 바이너리 파일 포맷(`.ssasm`)을 설계하고 직렬화/역직렬화 로직을 구현합니다.

## 7. 결론

제안된 설계는 SwiftScript를 단순한 스크립트 언어에서 벗어나, 현대적인 프로그래밍 언어 플랫폼으로 나아가는 중요한 첫걸음입니다. 초기 구현 비용은 높지만, 이를 통해 얻게 될 언어의 표현력, 안정성, 확장성은 그 이상의 가치를 제공할 것입니다.