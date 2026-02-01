# SwiftScript

SwiftScript는 Swift 문법의 핵심 요소를 지원하는 학습/실험용 스크립트 언어 프로젝트입니다. 파서/타입체커/VM을 갖춘 인터프리터 구조로 구성되어 있으며, Swift 스타일의 문법을 C++20 + STL 기반으로 구현하는 것을 목표로 합니다.【F:GRAMMAR_TODO.md†L1-L76】

## 프로젝트 목표

- Swift와 유사한 문법을 빠르게 실험하고 확장할 수 있는 스크립트 런타임 제공
- 파서/타입 체커/VM을 갖춘 전체 컴파일 파이프라인 학습용 코드베이스 제공
- Swift 핵심 문법(선언, 제어문, 타입 시스템, 옵셔널, 프로토콜 등)을 폭넓게 지원【F:GRAMMAR_TODO.md†L15-L116】

## 지원 문법 (요약)

> 아래 내용은 현재 구현/테스트 완료 범위를 기준으로 정리했습니다.

### 1) 리터럴 및 기본 타입
- 정수/실수/문자열/불리언/널 리터럴
- 기본 타입: `Int`, `Float`, `Bool`, `String`, `Array`, `Dictionary`【F:GRAMMAR_TODO.md†L17-L59】

```swift
let i: Int = 10
let f: Float = 3.14
let s: String = "hello"
let ok: Bool = true
let nums: Array<Int> = [1, 2, 3]
let dict: Dictionary<String, Int> = ["a": 1]

let parsed: Int = Int("123")
let ratio: Float = Float("3.5")
let flag: Bool = Bool("true")
let text: String = String(10)
let welcome = "Hello, \(s)! version \(i)"
```

### 2) 변수/상수 선언
- `var` / `let` 선언 및 타입 어노테이션
- `let` 재할당 금지 검증【F:GRAMMAR_TODO.md†L95-L110】

```swift
var count: Int = 0
let name: String = "SwiftScript"
```

### 3) 표현식/연산자
- 산술/비교/논리/비트 연산자
- 할당/복합 할당, 범위 연산자
- 연산자 오버로딩(기존 연산자 재정의)【F:GRAMMAR_TODO.md†L19-L24】【F:GRAMMAR_TODO.md†L215-L233】

```swift
let a = 10 + 20
let b = a >> 1
let isEqual = (a == 30)
```

### 4) 제어문
- `if/else`, `guard`, `while`, `repeat-while`, `for-in`, `switch`
- `for-in where` 절 지원【F:GRAMMAR_TODO.md†L27-L54】【F:GRAMMAR_TODO.md†L84-L92】

```swift
for i in 1...5 where i > 2 {
    print(i)
}
```

### 5) 함수/클로저
- 함수 선언, 파라미터/반환 타입
- 이름 있는 파라미터 호출
- 클로저(파라미터, 반환 타입, 본문)【F:GRAMMAR_TODO.md†L25-L52】【F:GRAMMAR_TODO.md†L61-L74】

```swift
func greet(name: String, times: Int) -> String {
    return name
}

greet(name: "Swift", times: 3)
```

### 6) 클래스/구조체/열거형
- 클래스 상속, `super` 호출
- 구조체 값 의미론 및 `mutating` 메서드
- 열거형 `rawValue` 및 연관 값(associated values)
- 프로퍼티 옵저버(`willSet`, `didSet`) 및 `lazy` 프로퍼티【F:GRAMMAR_TODO.md†L56-L132】

```swift
struct Point {
    var x: Int
    var y: Int

    mutating func move(dx: Int, dy: Int) {
        x = x + dx
        y = y + dy
    }
}

enum Result {
    case success(value: Int)
    case failure(message: String)
}
```

### 7) 프로토콜/확장(Extension)
- 프로토콜 선언 및 준수 검증
- 타입 확장(Extension)에서 메서드/프로퍼티 추가
- 접근 제어(`public`, `private`, `internal`) 지원【F:GRAMMAR_TODO.md†L33-L116】

```swift
protocol Identifiable {
    var id: Int { get }
}

extension Point: Identifiable {
    var id: Int { return x }
}
```

### 8) 옵셔널
- 옵셔널 타입 `T?`
- 옵셔널 체이닝 `?.`
- nil 병합 연산자 `??`
- 강제 언래핑 `!`
- `if let` / `guard let` 바인딩【F:GRAMMAR_TODO.md†L23-L52】

```swift
var value: Int? = 10
if let v = value {
    print(v)
}
```

### 9) 컬렉션/첨자(Subscript)
- 배열/딕셔너리 리터럴
- 기본 배열/딕셔너리 첨자 읽기/쓰기【F:GRAMMAR_TODO.md†L23-L160】

```swift
var arr = [1, 2, 3]
let first = arr[0]

var dict = ["a": 1]
dict["a"] = 2
```

### 10) 에러 처리
- `throw`, `try`, `catch`, `do-catch` 문법 지원【F:GRAMMAR_TODO.md†L31-L52】

```swift
do {
    try risky()
} catch {
    print("error")
}
```

### 11) 제네릭
- 제네릭 함수/구조체/열거형/확장
- 제약(`T: Protocol`) 및 `where` 절
- 중첩 제네릭 타입 지원【F:GRAMMAR_TODO.md†L174-L214】

```swift
struct Box<T> {
    var value: T
}

func identity<T>(value: T) -> T {
    return value
}
```

### 12) 타입 캐스팅
- `as`, `as?`, `as!`, `is` 지원【F:GRAMMAR_TODO.md†L88-L92】

```swift
if value is String {
    let s = value as! String
    print(s)
}
```

## 미지원/제한 사항
- 커스텀 `subscript` 선언 및 복합 파라미터 subscript (미구현)
- 커스텀 연산자(`prefix`/`infix`/`postfix`) 정의 (미구현)
- `fileprivate` 접근 제어 (파일 범위 개념 필요)【F:GRAMMAR_TODO.md†L119-L238】

## 실행/테스트 참고
- Windows 빌드 출력(`x64\Debug\SwiftScript.exe`)로 테스트 실행 예시가 문서에 정리되어 있습니다.【F:IMPLEMENTATION_STATUS.md†L86-L95】
