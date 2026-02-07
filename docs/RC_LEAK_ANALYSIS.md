# SwiftScript 참조 카운트 누수 분석 보고서

## 개요

SwiftScript 인터프리터에서 다음 테스트 코드 실행 시 메모리 누수가 발생합니다.

```swift
class Test {
    var count: Int = 5
    var name: String = "SampleTest"

    init {
        print("Test class initialized")
    }

    deinit {
        print("Test class deinitialized")
    }

    func run() {
        for i in 1...count {
            print(i)
        }
        print("Test run method completed.")
    }

    func description() -> String {
        return "Test class with name: \(name) and count: \(count)"
    }
}

extension Test {
    func extendedRun() {
        for i in 1...5 {
            print(i)
        }
        print("Test extendedRun method completed.")
    }
}

func main() {
    var testInstance = Test()
    testInstance.run()
    testInstance.extendedRun()
    print(testInstance.description())
}
```

---

## 누수된 객체 목록

프로그램 종료 시 다음 객체들이 해제되지 않고 강제 삭제됩니다:

| 주소 | 타입 | 참조 카운트 | 설명 |
|------|------|-------------|------|
| `0x...E400` | String | rc: 1 | description() 최종 반환값 |
| `0x...E360` | String | rc: 1 | 문자열 보간 임시 객체 |
| `0x...E2C0` | String | rc: 1 | 문자열 보간 임시 객체 |
| `0x...E220` | String | rc: 1 | 문자열 보간 임시 객체 |
| `0x...D970` | String | rc: 1 | 문자열 보간 임시 객체 |
| `0x...D7E0` | Instance | rc: 6 | Test 인스턴스 (testInstance) |
| `0x...BE70` | String | rc: 2 | 클래스 기본값 "SampleTest" |

---

## 핵심 문제점

### 문제 1: OP_RETURN에서 non-initializer 함수의 이중 retain

**위치**: `ss_vm.cpp:531-532`

```cpp
case OpCode::OP_RETURN:
{
    Value result = pop();  // ownership 이전 (release 없음)
    // ... 중간 처리 ...

    if (frame.is_initializer) {
        stack_.push_back(result);  // retain 없이 직접 push (정상)
    } else {
        push(result);  // ← 문제! push()가 retain()을 호출
    }
}
```

**원인**:
- `pop()`은 스택에서 값을 가져오면서 ownership을 caller에게 이전 (release 안함)
- `push()`는 값을 스택에 넣으면서 `RC::retain()` 호출
- 결과적으로 반환값의 참조 카운트가 +1 누적됨

**영향**:
- 모든 함수 반환값(특히 String)의 rc가 1 증가
- description() 메서드에서 생성된 모든 문자열이 누수

---

### 문제 2: BoundMethod 호출 시 receiver의 과도한 retain

**위치**: `ss_vm_opcodes_basic.inl:449-452`

```cpp
// Bound method: inject receiver
if (obj->type == ObjectType::BoundMethod) {
    auto* bound = static_cast<BoundMethodObject*>(obj);

    Value receiver_val = Value::from_object(bound->receiver);
    if (receiver_val.is_object() && receiver_val.ref_type() == RefType::Strong && receiver_val.as_object()) {
        RC::retain(receiver_val.as_object());  // ← 문제! 이중 retain
    }
    vm.stack_.insert(vm.stack_.begin() + static_cast<long>(callee_index + 1), receiver_val);
    // ...
}
```

**원인**:
1. BoundMethodObject 생성자에서 이미 receiver를 retain
2. 메서드 호출 시 receiver를 스택에 넣으면서 또 retain
3. 메서드 호출 완료 후 불균형한 release

**참조 카운트 흐름**:
```
인스턴스 생성:           rc: 0 → 1 (스택 push)
init BoundMethod 생성:   rc: 1 → 2 (생성자에서 retain)
init 호출 준비:          rc: 2 → 3 (스택 삽입 시 retain)
init 완료 후:            rc: 3 → 2 (일부 release)
run() BoundMethod 생성:  rc: 2 → 3 → 4 ...
extendedRun() 호출:      rc: 4 → 5 → 6 ...
```

**영향**:
- Instance 객체의 rc가 메서드 호출할 때마다 누적
- 최종적으로 rc: 6으로 남아 해제되지 않음
- deinit이 호출되지 않음

---

### 문제 3: 클래스 property 기본값의 이중 참조

**위치**: `ss_vm_opcodes_basic.inl:352-358`

```cpp
for (const auto& property : (*it)->properties) {
    Value prop_value = property.default_value;  // ClassObject의 기본값 참조
    if (prop_value.is_object() && prop_value.ref_type() == RefType::Strong) {
        RC::retain(prop_value.as_object());     // Instance 필드용으로 retain
    }
    instance->fields[property.name] = prop_value;
}
```

**원인**:
- ClassObject의 `default_value`가 String 객체를 참조 (rc: 1)
- Instance 초기화 시 같은 객체를 retain (rc: 2)
- Instance 해제 시 `release_children()`에서 release (rc: 1)
- ClassObject 해제 시 release (rc: 0) - 하지만 Instance가 먼저 해제되지 않으면 문제

**영향**:
- "SampleTest" 문자열이 rc: 2로 남음
- ClassObject와 Instance 간의 공유 참조 불일치

---

## 참조 카운트 흐름 상세 분석

### Instance (Test 객체) rc: 6 누적 과정

```
1. allocate_object<InstanceObject>     → rc: 0
2. RC::retain(instance) (스택용)        → rc: 1
3. BoundMethod(init) 생성자에서 retain  → rc: 2
4. init 호출 시 receiver 스택 삽입      → rc: 3
5. init 완료, BoundMethod 해제         → rc: 2 (하지만 스택에 여전히 있음)
6. run() 호출 준비: BoundMethod 생성    → rc: 3
7. run() 호출: receiver 스택 삽입       → rc: 4
8. run() 완료 후 불완전 release         → rc: 3~4
9. extendedRun(), description() 반복   → rc: 5~6
10. main() 종료 시                     → rc: 6 (누수)
```

### String 반환값 rc: 1 누적 과정

```
1. description() 내부에서 String 생성   → rc: 0
2. push_new() 또는 push()로 스택에     → rc: 1
3. OP_RETURN에서 pop()                → rc: 1 (ownership 이전)
4. push(result)로 호출자 스택에        → rc: 2 (이중 retain!)
5. 호출자에서 사용 후 discard()        → rc: 1
6. 프로그램 종료                      → rc: 1 (누수)
```

---

## 수정 방안

### 수정 1: OP_RETURN의 non-initializer 경로 수정

**파일**: `ss_vm.cpp`

**현재 코드**:
```cpp
if (frame.is_initializer) {
    stack_.push_back(result);
} else {
    push(result);  // 문제: 이중 retain
}
```

**수정 코드**:
```cpp
if (frame.is_initializer) {
    stack_.push_back(result);
} else {
    stack_.push_back(result);  // ownership transfer: retain 없이 직접 push
}
```

또는 더 명확하게:
```cpp
// pop()에서 ownership을 이전받았으므로 retain 없이 스택에 추가
stack_.push_back(result);  // initializer 여부와 관계없이 동일 처리
```

---

### 수정 2: BoundMethod 호출 시 receiver retain 제거

**파일**: `ss_vm_opcodes_basic.inl`

**옵션 A**: BoundMethod 생성자에서 retain 제거
```cpp
// ss_value.hpp의 BoundMethodObject 생성자
BoundMethodObject(Object* recv, Value meth)
    : Object(ObjectType::BoundMethod), receiver(recv), method(meth) {
    // receiver retain 제거 - 호출 시점에만 retain
}
```

**옵션 B**: 호출 시점의 retain 제거 (권장)
```cpp
// ss_vm_opcodes_basic.inl:449-452
Value receiver_val = Value::from_object(bound->receiver);
// retain 제거: BoundMethod가 이미 receiver를 소유
vm.stack_.insert(vm.stack_.begin() + static_cast<long>(callee_index + 1), receiver_val);
```

**주의**: 둘 중 하나만 선택해야 하며, 어느 쪽이 receiver의 생명주기를 관리할지 명확히 해야 함

---

### 수정 3: Instance 해제 시 필드 release 확인

**파일**: `ss_core.cpp` 또는 `ss_value.hpp`

Instance의 `release_children()` 또는 소멸자에서 모든 필드를 release해야 함:

```cpp
void InstanceObject::release_children(VM* vm) {
    for (auto& [name, value] : fields) {
        if (value.is_object() && value.ref_type() == RefType::Strong && value.as_object()) {
            RC::release(vm, value.as_object());
        }
    }
    fields.clear();
}
```

---

### 수정 4: 클래스 기본값의 deep copy 또는 참조 정책 통일

**옵션 A**: Instance 초기화 시 기본값을 deep copy
```cpp
for (const auto& property : (*it)->properties) {
    Value prop_value = deep_copy(property.default_value);  // 새 객체 생성
    instance->fields[property.name] = prop_value;
    // deep_copy가 rc:1로 생성하므로 추가 retain 불필요
}
```

**옵션 B**: ClassObject의 기본값에 대해 weak reference 사용
```cpp
// ClassObject의 default_value는 weak reference로 저장
// Instance가 이를 사용할 때만 retain
```

---

## 우선순위

| 순위 | 수정 항목 | 영향도 | 복잡도 |
|------|----------|--------|--------|
| 1 | OP_RETURN 이중 retain 수정 | 높음 | 낮음 |
| 2 | BoundMethod receiver retain 정책 통일 | 높음 | 중간 |
| 3 | Instance 필드 release 확인 | 중간 | 낮음 |
| 4 | 클래스 기본값 참조 정책 | 중간 | 높음 |

---

## 테스트 방법

수정 후 다음을 확인:

1. **LEAK WARNING 제거**: 프로그램 종료 시 누수 경고가 없어야 함
2. **deinit 호출**: Instance가 정상적으로 해제되어 deinit이 호출되어야 함
3. **참조 카운트 균형**: ALLOCATE 수 == DEALLOCATE 수 확인

```
Expected output:
Test class initialized
1
2
3
4
5
Test run method completed.
1
2
3
4
5
Test extendedRun method completed.
Test class with name: SampleTest and count: 5
Test class deinitialized  ← 이 메시지가 출력되어야 함
Program finished. Return value: null
(LEAK WARNING 없음)
```

---

## 관련 파일

- `src/common/ss_vm.cpp`: OP_RETURN 처리, push/pop 함수
- `src/common/ss_vm_opcodes_basic.inl`: OP_CALL 처리, BoundMethod 호출
- `src/common/ss_core.cpp`: RC::retain, RC::release, release_children
- `src/common/ss_value.hpp`: BoundMethodObject, InstanceObject 정의

---

## 결론

SwiftScript의 참조 카운트 누수는 **일관되지 않은 ownership 전달 정책**에서 비롯됩니다. 핵심 문제는:

1. **OP_RETURN에서 pop() 후 push()의 이중 retain** - 가장 빈번한 누수 원인
2. **BoundMethod의 receiver에 대한 이중 retain** - Instance 누수의 주원인
3. **ClassObject와 Instance 간의 기본값 공유 문제** - 복잡한 생명주기 이슈

수정 1번(OP_RETURN)만 적용해도 대부분의 String 누수가 해결되며, 수정 2번(BoundMethod)을 적용하면 Instance 누수가 해결됩니다.
