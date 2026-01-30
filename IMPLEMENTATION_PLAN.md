# SwiftScript 구현 계획서

## 문서 정보
- **작성일**: 2026-01-30
- **버전**: 1.0
- **목적**: SwiftScript 미구현 기능의 체계적 구현 로드맵

---

## 1. 현재 구현 상태 요약

### 1.1 구현 통계
| 항목 | 수치 |
|------|------|
| 토큰 종류 | 66개 |
| AST 노드 | 30개 (Expr: 16, Stmt: 14) |
| OpCode | 47개 |
| ObjectType | 15개 |
| 테스트 파일 | 17개 |
| 총 코드 라인 | ~8,775 LOC |

### 1.2 완전 구현된 기능
- ✅ 기본 타입 (Int, Float, Bool, String, Array, Dictionary)
- ✅ 변수/상수 (var, let, 타입 어노테이션)
- ✅ 제어문 (if/else, guard, while, for-in, switch)
- ✅ 함수 및 클로저
- ✅ 모든 기본 연산자
- ✅ 클래스 (상속, override, super, 계산 프로퍼티)
- ✅ 구조체 (memberwise init, 값 복사)
- ✅ 열거형 (raw value, 메서드, 계산 프로퍼티)
- ✅ 프로토콜 (메서드/프로퍼티 요구사항, 상속)
- ✅ 확장 (Extension)
- ✅ 옵셔널 (?, ??, ?., !, if let, guard let)
- ✅ 참조 카운팅 (weak, unowned)

### 1.3 부분 구현된 기능
| 기능 | 현재 상태 | 미완료 부분 |
|------|-----------|-------------|
| Struct mutating | 파싱 완료 | self 수정 로직 |
| Struct 프로퍼티 할당 | 미지원 | `p.x = 100` 파서 수정 |
| Associated Values | AST만 | 런타임 지원 |
| 비트 연산자 | 토큰만 | VM 옵코드 |
| 프로토콜 준수 검증 | 파싱만 | 컴파일 타임 검증 |

---

## 2. 구현 우선순위

### 🔴 Phase 1: 핵심 문법 완성 (즉시 필요)

#### 1.1 Struct 프로퍼티 직접 할당
- **난이도**: 중
- **영향도**: 높음 (현재 테스트 실패의 주요 원인)
- **설명**: `instance.property = value` 형식의 할당 지원
- **작업 내용**:
  - Parser에서 멤버 접근 후 할당 표현식 처리
  - Compiler에서 `OP_SET_PROPERTY` 생성
  - VM에서 구조체 프로퍼티 수정 로직

```swift
// 목표 문법
var point = Point(x: 0, y: 0)
point.x = 10  // 이것이 작동해야 함
point.y = 20
```

#### 1.2 Struct init에서 self 할당
- **난이도**: 중
- **영향도**: 높음
- **설명**: `self.property = value` 형식 지원
- **작업 내용**:
  - Parser에서 `self.` 접근 처리
  - init 컨텍스트에서 self 바인딩
  - mutating 메서드에서 self 수정 허용

```swift
// 목표 문법
struct Point {
    var x: Int
    var y: Int

    init(x: Int, y: Int) {
        self.x = x  // 이것이 작동해야 함
        self.y = y
    }

    mutating func move(dx: Int, dy: Int) {
        self.x = self.x + dx
        self.y = self.y + dy
    }
}
```

#### 1.3 Named Parameters (이름 있는 파라미터)
- **난이도**: 중
- **영향도**: 높음 (Swift 호환성)
- **설명**: 함수/init 호출 시 파라미터 이름 지정
- **작업 내용**:
  - Lexer에서 `name:` 패턴 인식
  - Parser에서 named argument 파싱
  - 호출 시 파라미터 이름 매칭

```swift
// 목표 문법
let p = Point(x: 10, y: 20)
func greet(name: String, times: Int) { }
greet(name: "Swift", times: 3)
```

#### 1.4 Associated Values (연관값)
- **난이도**: 높음
- **영향도**: 높음 (Enum 활용도)
- **설명**: Enum case에 데이터 첨부
- **작업 내용**:
  - AST에 연관값 타입 정보 추가
  - 런타임에서 연관값 저장/추출
  - switch에서 패턴 매칭으로 추출

```swift
// 목표 문법
enum Result {
    case success(value: Int)
    case failure(message: String)
}

let r = Result.success(value: 42)
switch r {
case .success(let v):
    print(v)
case .failure(let msg):
    print(msg)
}
```

---

### 🟠 Phase 2: 접근 제어 및 타입 안전성

#### 2.1 Access Control (접근 제어)
- **난이도**: 중
- **영향도**: 중 (캡슐화)
- **키워드**: `public`, `private`, `internal`, `fileprivate`
- **작업 내용**:
  - 토큰 및 파서 추가
  - 멤버 접근 시 가시성 검사
  - 모듈 경계에서 검증

```swift
// 목표 문법
class Account {
    private var balance: Int = 0
    public func deposit(amount: Int) {
        balance = balance + amount
    }
}
```

#### 2.2 Static 멤버
- **난이도**: 중
- **영향도**: 중 (유틸리티 함수)
- **설명**: 타입 레벨 프로퍼티/메서드
- **작업 내용**:
  - `static` 키워드 파싱
  - 클래스/구조체에 static 멤버 저장
  - 타입 이름으로 접근 허용

```swift
// 목표 문법
struct Math {
    static var pi: Float = 3.14159
    static func square(x: Int) -> Int {
        return x * x
    }
}
let area = Math.pi * r * r
```

#### 2.3 타입 캐스팅
- **난이도**: 중
- **영향도**: 중 (다형성)
- **키워드**: `as`, `as?`, `as!`, `is`
- **작업 내용**:
  - 타입 검사 연산자 구현
  - 다운캐스팅 로직
  - 옵셔널 캐스팅 결과

```swift
// 목표 문법
let animal: Animal = Dog()
if animal is Dog {
    let dog = animal as! Dog
    dog.bark()
}
```

#### 2.4 프로토콜 준수 검증
- **난이도**: 높음
- **영향도**: 높음 (타입 안전성)
- **설명**: 컴파일 타임에 프로토콜 요구사항 확인
- **작업 내용**:
  - 타입 선언 시 프로토콜 요구사항 수집
  - 모든 요구 메서드/프로퍼티 구현 확인
  - 미구현 시 컴파일 에러

---

### 🟡 Phase 3: 고급 프로퍼티

#### 3.1 Property Observers
- **난이도**: 중
- **영향도**: 중 (반응형 프로그래밍)
- **키워드**: `willSet`, `didSet`
- **작업 내용**:
  - 프로퍼티에 observer 블록 연결
  - 할당 전후 콜백 호출

```swift
// 목표 문법
var score: Int = 0 {
    willSet {
        print("점수가 \(newValue)로 변경됩니다")
    }
    didSet {
        print("점수가 \(oldValue)에서 변경되었습니다")
    }
}
```

#### 3.2 Lazy Properties
- **난이도**: 중
- **영향도**: 낮음 (최적화)
- **키워드**: `lazy var`
- **작업 내용**:
  - 초기 접근 시까지 초기화 지연
  - 초기화 여부 플래그 관리

```swift
// 목표 문법
class DataManager {
    lazy var data: [Int] = loadData()
}
```

#### 3.3 Subscript
- **난이도**: 중
- **영향도**: 중 (컬렉션 접근)
- **설명**: 인덱스 기반 접근 커스터마이징
- **작업 내용**:
  - `subscript` 키워드 파싱
  - get/set 블록 처리
  - `[]` 연산자 오버로딩

```swift
// 목표 문법
struct Matrix {
    subscript(row: Int, col: Int) -> Int {
        get { return data[row * cols + col] }
        set { data[row * cols + col] = newValue }
    }
}
```

---

### 🟢 Phase 4: 반복문 확장

#### 4.1 repeat-while
- **난이도**: 낮음
- **영향도**: 낮음
- **설명**: 최소 1회 실행 보장 루프

```swift
// 목표 문법
repeat {
    attempt()
} while !success
```

#### 4.2 for-in 컬렉션 순회
- **난이도**: 중
- **영향도**: 중
- **설명**: Array, Dictionary 직접 순회

```swift
// 목표 문법
let items = [1, 2, 3]
for item in items {
    print(item)
}

let dict = ["a": 1, "b": 2]
for (key, value) in dict {
    print("\(key): \(value)")
}
```

#### 4.3 where 절
- **난이도**: 중
- **영향도**: 낮음
- **설명**: 조건부 반복

```swift
// 목표 문법
for i in 1...10 where i % 2 == 0 {
    print(i)  // 짝수만
}
```

---

### 🔵 Phase 5: 에러 처리

#### 5.1 try/catch/throw
- **난이도**: 높음
- **영향도**: 높음 (안정성)
- **설명**: 구조화된 예외 처리

```swift
// 목표 문법
enum FileError: Error {
    case notFound
    case permissionDenied
}

func readFile(path: String) throws -> String {
    if !exists(path) {
        throw FileError.notFound
    }
    return content
}

do {
    let content = try readFile(path: "test.txt")
} catch FileError.notFound {
    print("파일을 찾을 수 없습니다")
} catch {
    print("알 수 없는 오류: \(error)")
}
```

#### 5.2 try? 및 try!
- **난이도**: 중
- **영향도**: 중
- **설명**: 옵셔널 에러 처리

```swift
// 목표 문법
let content = try? readFile(path: "test.txt")  // 실패시 nil
let content = try! readFile(path: "test.txt")  // 실패시 crash
```

---

### ⚪ Phase 6: 제네릭 및 고급 기능

#### 6.1 Generics
- **난이도**: 매우 높음
- **영향도**: 매우 높음 (재사용성)
- **설명**: 타입 매개변수화

```swift
// 목표 문법
func swap<T>(a: inout T, b: inout T) {
    let temp = a
    a = b
    b = temp
}

struct Stack<Element> {
    var items: [Element] = []
    mutating func push(item: Element) {
        items.append(item)
    }
}
```

#### 6.2 Type Constraints
- **난이도**: 높음
- **영향도**: 높음

```swift
// 목표 문법
func findMax<T: Comparable>(array: [T]) -> T? {
    // ...
}
```

#### 6.3 async/await
- **난이도**: 매우 높음
- **영향도**: 높음 (비동기 프로그래밍)

```swift
// 목표 문법
func fetchData() async -> Data {
    // ...
}

let data = await fetchData()
```

#### 6.4 Operator Overloading
- **난이도**: 중
- **영향도**: 중

```swift
// 목표 문법
struct Vector {
    var x: Int
    var y: Int
}

func +(lhs: Vector, rhs: Vector) -> Vector {
    return Vector(x: lhs.x + rhs.x, y: lhs.y + rhs.y)
}
```

---

## 3. 구현 일정 제안

### 마일스톤 1: 핵심 완성 (Phase 1)
- [ ] Struct 프로퍼티 할당
- [ ] Struct self 할당
- [ ] Named Parameters
- [ ] Associated Values

### 마일스톤 2: 타입 안전성 (Phase 2)
- [ ] Access Control
- [ ] Static 멤버
- [ ] 타입 캐스팅
- [ ] 프로토콜 준수 검증

### 마일스톤 3: 고급 기능 (Phase 3-4)
- [ ] Property Observers
- [ ] Lazy Properties
- [ ] Subscript
- [ ] repeat-while
- [ ] for-in 컬렉션
- [ ] where 절

### 마일스톤 4: 에러 처리 (Phase 5)
- [ ] try/catch/throw
- [ ] Error 프로토콜
- [ ] try?/try!

### 마일스톤 5: 제네릭 (Phase 6)
- [ ] 기본 Generics
- [ ] Type Constraints
- [ ] async/await (선택적)
- [ ] Operator Overloading

---

## 4. 기술적 고려사항

### 4.1 파일 수정이 필요한 컴포넌트

| 기능 | Lexer | Parser | Compiler | VM | Object |
|------|-------|--------|----------|-----|--------|
| Struct 프로퍼티 할당 | - | ✓ | ✓ | ✓ | - |
| self 할당 | - | ✓ | ✓ | - | - |
| Named Parameters | ✓ | ✓ | ✓ | - | - |
| Associated Values | - | ✓ | ✓ | ✓ | ✓ |
| Access Control | ✓ | ✓ | ✓ | - | - |
| Static 멤버 | ✓ | ✓ | ✓ | ✓ | ✓ |
| try/catch | ✓ | ✓ | ✓ | ✓ | ✓ |
| Generics | ✓ | ✓ | ✓ | ✓ | ✓ |

### 4.2 테스트 전략
- 각 기능별 단위 테스트 파일 작성
- 기존 테스트 회귀 방지
- 엣지 케이스 커버리지 확보

---

## 5. 참고 자료

- [Swift Language Guide](https://docs.swift.org/swift-book/)
- [Swift Evolution](https://github.com/apple/swift-evolution)
- 현재 코드베이스: `src/` 디렉토리

---

## 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|-----------|
| 1.0 | 2026-01-30 | 초기 계획서 작성 |
