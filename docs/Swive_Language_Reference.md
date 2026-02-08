# Swive Language Reference

## Overview

swive는 Swift에서 영감을 받은 정적 타입 스크립트 언어입니다. 클래스, 구조체, 열거형, 프로토콜, 제네릭, 클로저, 옵셔널, 패턴 매칭, 에러 핸들링 등 현대적인 프로그래밍 패러다임을 지원하며, 네이티브 C++ 바인딩을 통해 확장 가능한 런타임 환경을 제공합니다.

**파일 확장자:** `.ss`
**프로젝트 파일:** `.ssproject`
**문자열 보간:** `${expr}` 구문
**실행 모델:** 바이트코드 컴파일 + VM 실행

---

## 목차

1. [기본 문법](#1-기본-문법)
2. [타입 시스템](#2-타입-시스템)
3. [변수와 상수](#3-변수와-상수)
4. [연산자](#4-연산자)
5. [제어 흐름](#5-제어-흐름)
6. [함수](#6-함수)
7. [클로저](#7-클로저)
8. [클래스](#8-클래스)
9. [구조체](#9-구조체)
10. [열거형](#10-열거형)
11. [프로토콜](#11-프로토콜)
12. [제네릭](#12-제네릭)
13. [익스텐션](#13-익스텐션)
14. [옵셔널](#14-옵셔널)
15. [컬렉션](#15-컬렉션)
16. [튜플](#16-튜플)
17. [문자열](#17-문자열)
18. [에러 핸들링](#18-에러-핸들링)
19. [어트리뷰트](#19-어트리뷰트)
20. [접근 제어](#20-접근-제어)
21. [모듈 시스템](#21-모듈-시스템)
22. [네이티브 바인딩](#22-네이티브-바인딩)
23. [빌트인 함수](#23-빌트인-함수)
24. [키워드 목록](#24-키워드-목록)
25. [게임엔진 임베딩](#25-게임엔진-임베딩)

---

## 1. 기본 문법

### 주석

```swift
// 한 줄 주석

/* 블록 주석 */

/* 중첩된
   /* 블록 주석도 */
   지원합니다 */
```

### 세미콜론

문장 끝에 세미콜론은 선택 사항입니다. 한 줄에 여러 문장을 작성할 때 사용합니다.

```swift
let a = 1; let b = 2
let c = 3
```

### 출력

`print`는 빌트인 문장(statement)입니다.

```swift
print("Hello, World!")
print(42)
print("결과: ${1 + 2}")
```

---

## 2. 타입 시스템

### 빌트인 타입

| 타입 | 설명 | 예시 |
|------|------|------|
| `Int` | 64비트 정수 | `42`, `-7` |
| `Float` | 64비트 부동소수점 | `3.14`, `-0.5` |
| `Bool` | 불리언 | `true`, `false` |
| `String` | 문자열 | `"hello"` |
| `Void` | 반환값 없음 | - |
| `Any` | 모든 타입 수용 | - |
| `Array` | 동적 배열 | `[1, 2, 3]` |
| `Dictionary` | 키-값 맵 | `["key": value]` |

### 타입 어노테이션

```swift
let name: String = "swive"
let count: Int = 42
let pi: Float = 3.14
let active: Bool = true
```

### 타입 추론

초기값이 있으면 타입을 생략할 수 있습니다.

```swift
let name = "swive"   // String으로 추론
let count = 42              // Int로 추론
let pi = 3.14               // Float로 추론
let active = true           // Bool로 추론
```

### 타입 변환

```swift
let x = Int(3.14)       // 3
let y = Float(42)        // 42.0
let s = String(123)      // "123"
let b = Bool(1)          // true
```

### 타입 캐스팅

```swift
let value: Any = 42

// 안전한 캐스팅 (옵셔널 반환)
let num = value as? Int

// 강제 캐스팅 (실패 시 런타임 에러)
let num2 = value as! Int

// 직접 캐스팅
let num3 = value as Int

// 타입 확인
if value is Int {
    print("정수입니다")
}
```

### 빌트인 프로토콜 적합성

| 타입 | 적합 프로토콜 |
|------|--------------|
| `Int` | Equatable, Comparable, Hashable, Numeric, SignedNumeric, CustomStringConvertible |
| `Float` | Equatable, Comparable, Hashable, Numeric, SignedNumeric, CustomStringConvertible |
| `Bool` | Equatable, Hashable, CustomStringConvertible |
| `String` | Equatable, Comparable, Hashable, CustomStringConvertible |

---

## 3. 변수와 상수

### 변수 선언 (`var`)

```swift
var count = 0
count = 10          // 변경 가능

var name: String = "hello"
name = "world"
```

### 상수 선언 (`let`)

```swift
let pi = 3.14159
// pi = 3.0       // 컴파일 에러: let은 재할당 불가
```

### 튜플 디스트럭처링

```swift
let (x, y) = (10, 20)
let (name: a, age: b) = (name: "Kim", age: 25)
```

### `lazy` 프로퍼티

초기화를 첫 접근 시까지 지연합니다.

```swift
class DataManager {
    lazy var data: String = "loaded"
}
```

### `weak` / `unowned` 참조

```swift
weak var delegate: MyProtocol?
unowned var parent: ParentClass
```

---

## 4. 연산자

### 산술 연산자

| 연산자 | 설명 | 예시 |
|--------|------|------|
| `+` | 덧셈 / 문자열 연결 | `3 + 4`, `"a" + "b"` |
| `-` | 뺄셈 | `10 - 3` |
| `*` | 곱셈 | `2 * 5` |
| `/` | 나눗셈 | `10 / 3` |
| `%` | 나머지 | `10 % 3` |

### 비교 연산자

| 연산자 | 설명 |
|--------|------|
| `==` | 같음 |
| `!=` | 다름 |
| `<` | 작음 |
| `>` | 큼 |
| `<=` | 작거나 같음 |
| `>=` | 크거나 같음 |

### 논리 연산자

| 연산자 | 설명 |
|--------|------|
| `&&` | 논리 AND |
| `\|\|` | 논리 OR |
| `!` | 논리 NOT |

### 비트 연산자

| 연산자 | 설명 |
|--------|------|
| `&` | 비트 AND |
| `\|` | 비트 OR |
| `^` | 비트 XOR |
| `~` | 비트 NOT |
| `<<` | 좌측 시프트 |
| `>>` | 우측 시프트 |

### 복합 대입 연산자

```swift
var x = 10
x += 5      // x = x + 5
x -= 3      // x = x - 3
x *= 2      // x = x * 2
x /= 4      // x = x / 4
x %= 3      // x = x % 3
x &= 0xFF   // x = x & 0xFF
x |= 0x01   // x = x | 0x01
x ^= 0x0F   // x = x ^ 0x0F
x <<= 2     // x = x << 2
x >>= 1     // x = x >> 1
```

### 범위 연산자

```swift
let inclusive = 1...5    // 1, 2, 3, 4, 5
let exclusive = 1..<5    // 1, 2, 3, 4
```

### 삼항 연산자

```swift
let result = condition ? valueIfTrue : valueIfFalse
```

### nil 병합 연산자

```swift
let value = optionalValue ?? defaultValue
```

### 옵셔널 체이닝

```swift
let name = person?.address?.street
```

### 연산자 우선순위 (높은 순)

| 우선순위 | 연산자 |
|----------|--------|
| 13 | `*`, `/`, `%` |
| 12 | `+`, `-` |
| 11 | `<<`, `>>` |
| 9 | `<`, `>`, `<=`, `>=` |
| 8 | `==`, `!=` |
| 7 | `&` (비트) |
| 6 | `^` (비트) |
| 5 | `\|` (비트) |
| 4 | `&&` (논리) |
| 3 | `\|\|` (논리) |
| 2 | `?`, `??` (삼항, nil 병합) |
| 1 | `=`, `+=`, `-=` 등 (대입) |

---

## 5. 제어 흐름

### if / else

```swift
if temperature > 30 {
    print("덥습니다")
} else if temperature > 20 {
    print("적당합니다")
} else {
    print("춥습니다")
}
```

### if let (옵셔널 바인딩)

```swift
if let value = optionalValue {
    print("값이 있습니다: ${value}")
} else {
    print("nil입니다")
}
```

### guard let

```swift
guard let value = optionalValue else {
    print("nil이므로 종료")
    return
}
print("값: ${value}")
```

### switch / case

```swift
switch direction {
    case .north:
        print("북쪽")
    case .south:
        print("남쪽")
    case .east, .west:
        print("동쪽 또는 서쪽")
    default:
        print("알 수 없음")
}
```

#### 열거형 패턴 매칭 (Associated Values)

```swift
switch result {
    case .success(message):
        print("성공: ${message}")
    case .failure(code, message):
        print("실패 ${code}: ${message}")
}
```

### for-in 루프

```swift
for i in 0..<10 {
    print(i)
}

for item in array {
    print(item)
}
```

#### where 절

```swift
for i in 0..<100 where i % 2 == 0 {
    print("짝수: ${i}")
}
```

### while 루프

```swift
var count = 0
while count < 10 {
    print(count)
    count += 1
}
```

### repeat-while 루프

```swift
var count = 0
repeat {
    print(count)
    count += 1
} while count < 10
```

### break / continue

```swift
for i in 0..<10 {
    if i == 5 { break }
    if i % 2 == 0 { continue }
    print(i)
}
```

---

## 6. 함수

### 기본 함수 선언

```swift
func greet() {
    print("Hello!")
}

func add(a: Int, b: Int) -> Int {
    return a + b
}
```

### 파라미터 레이블

```swift
// 외부 레이블과 내부 이름이 다른 경우
func move(from start: Int, to end: Int) {
    print("${start} → ${end}")
}
move(from: 0, to: 10)

// 외부 레이블 생략 (_)
func square(_ value: Int) -> Int {
    return value * value
}
square(5)
```

### 기본값 파라미터

```swift
func greet(name: String = "World") {
    print("Hello, ${name}!")
}
greet()              // "Hello, World!"
greet(name: "Kim")   // "Hello, Kim!"
```

### expected 함수 (에러 핸들링)

`expected` 키워드로 실패 가능한 함수를 선언합니다. 자세한 내용은 [에러 핸들링](#18-에러-핸들링)을 참조하세요.

```swift
func divide(a: Int, b: Int) -> Int expected String {
    if (b == 0) {
        return expected.error("Division by zero")
    }
    return a / b
}
```

### 연산자 오버로딩

```swift
class Vector2 {
    var x: Float
    var y: Float

    func +(other: Vector2) -> Vector2 {
        return Vector2(x: x + other.x, y: y + other.y)
    }

    func ==(other: Vector2) -> Bool {
        return x == other.x && y == other.y
    }
}
```

### 함수 타입

```swift
let operation: (Int, Int) -> Int = add
let result = operation(3, 4)
```

---

## 7. 클로저

### 클로저 구문

```swift
let add = { (a: Int, b: Int) -> Int in
    return a + b
}
let result = add(3, 4)
```

### 간단한 블록 클로저

```swift
let greet = {
    print("Hello!")
}
greet()
```

### 변수 캡처

클로저는 외부 변수를 자동으로 캡처합니다.

```swift
var counter = 0
let increment = {
    counter += 1
}
increment()
print(counter) // 1
```

---

## 8. 클래스

### 클래스 선언

```swift
class Animal {
    var name: String
    var age: Int

    init(name: String, age: Int) {
        self.name = name
        self.age = age
    }

    func speak() -> String {
        return "${name} says hello"
    }

    deinit {
        print("${name} deallocated")
    }
}
```

### 상속

```swift
class Dog : Animal {
    var breed: String

    init(name: String, age: Int, breed: String) {
        self.breed = breed
        super.init(name: name, age: age)
    }

    override func speak() -> String {
        return "${name} barks!"
    }
}
```

### 프로토콜 적합

```swift
class Cat : Animal, Describable, Equatable {
    func describe() -> String {
        return "Cat: ${name}"
    }
}
```

### 프로퍼티

#### 저장 프로퍼티

```swift
class Point {
    var x: Float = 0.0
    var y: Float = 0.0
    let label: String = "origin"
}
```

#### 연산 프로퍼티 (Computed Property)

```swift
class Circle {
    var radius: Float = 0.0

    var area: Float {
        get {
            return 3.14159 * radius * radius
        }
        set {
            radius = Float(newValue / 3.14159)
        }
    }

    // 읽기 전용 (축약형)
    var diameter: Float {
        return radius * 2.0
    }
}
```

#### 프로퍼티 옵저버

```swift
class StepCounter {
    var steps: Int = 0 {
        willSet {
            print("곧 변경됩니다")
        }
        didSet {
            print("변경 완료: ${steps}")
        }
    }
}
```

#### 지연 프로퍼티 (Lazy Property)

```swift
class DataLoader {
    lazy var data: String = "heavy data loaded"
}
```

#### 정적 프로퍼티 / 메서드

```swift
class MathUtils {
    static var pi: Float = 3.14159

    static func square(x: Float) -> Float {
        return x * x
    }
}
let p = MathUtils.pi
let s = MathUtils.square(x: 5.0)
```

---

## 9. 구조체

### 구조체 선언

구조체는 **값 타입**입니다. 할당 시 복사됩니다.

```swift
struct Point {
    var x: Float
    var y: Float

    init(x: Float, y: Float) {
        self.x = x
        self.y = y
    }

    func distanceTo(other: Point) -> Float {
        let dx = x - other.x
        let dy = y - other.y
        return Float((dx * dx + dy * dy))
    }
}
```

### mutating 메서드

구조체의 프로퍼티를 변경하는 메서드는 `mutating` 키워드가 필요합니다.

```swift
struct Counter {
    var count: Int = 0

    mutating func increment() {
        count += 1
    }

    mutating func reset() {
        count = 0
    }
}

var c = Counter()
c.increment()
print(c.count)  // 1
```

> **주의:** `let`으로 선언한 구조체 인스턴스에서는 `mutating` 메서드를 호출할 수 없습니다.

### 프로토콜 적합

```swift
struct Size : Equatable, CustomStringConvertible {
    var width: Float
    var height: Float

    func ==(other: Size) -> Bool {
        return width == other.width && height == other.height
    }
}
```

### 정적 메서드

```swift
struct Temperature {
    var celsius: Float

    static func fromFahrenheit(f: Float) -> Temperature {
        return Temperature(celsius: (f - 32.0) / 1.8)
    }
}
```

### 연산 프로퍼티, 프로퍼티 옵저버

클래스와 동일한 문법을 지원합니다.

---

## 10. 열거형

### 기본 열거형

```swift
enum Direction {
    case north
    case south
    case east
    case west
}

let dir = Direction.north
```

### 원시값 (Raw Values)

```swift
enum Planet : Int {
    case mercury = 1
    case venus = 2
    case earth = 3
    case mars = 4
}

let earth = Planet.earth
print(earth.rawValue)  // 3
```

```swift
enum Color : String {
    case red = "RED"
    case green = "GREEN"
    case blue = "BLUE"
}
```

### 연관값 (Associated Values)

```swift
enum Result {
    case success(message: String)
    case failure(code: Int, message: String)
}

let r = Result.success(message: "OK")

switch r {
    case .success(message):
        print("성공: ${message}")
    case .failure(code, message):
        print("실패 [${code}]: ${message}")
}
```

### 열거형 메서드 / 연산 프로퍼티

```swift
enum Weekday {
    case monday
    case tuesday
    case wednesday
    case thursday
    case friday

    var isWeekend: Bool {
        return false
    }

    func describe() -> String {
        return "A weekday"
    }
}
```

---

## 11. 프로토콜

### 프로토콜 선언

```swift
protocol Describable {
    func describe() -> String
    var name: String { get }
}
```

### 프로퍼티 요구사항

```swift
protocol HasArea {
    var area: Float { get }         // 읽기 전용
    var name: String { get set }    // 읽기/쓰기
}
```

### 메서드 요구사항

```swift
protocol Resettable {
    func reset()
    mutating func clear()   // 구조체용 mutating 메서드
}
```

### 프로토콜 상속

```swift
protocol Comparable : Equatable {
    func <(other: Self) -> Bool
}
```

### 프로토콜 적합 구현

```swift
class Shape : Describable, HasArea {
    var name: String = "shape"

    func describe() -> String {
        return "A ${name}"
    }

    var area: Float {
        return 0.0
    }
}
```

### 빌트인 프로토콜 계층

```
Equatable
├── Comparable
├── Hashable
Numeric
├── SignedNumeric
CustomStringConvertible
```

---

## 12. 제네릭

### 제네릭 함수

```swift
func swap<T>(a: T, b: T) -> (T, T) {
    return (b, a)
}

let result = swap<Int>(a: 1, b: 2)
```

### 제네릭 구조체

```swift
struct Box<T> {
    var value: T

    init(value: T) {
        self.value = value
    }

    func get() -> T {
        return value
    }
}

let intBox = Box<Int>(value: 42)
let strBox = Box<String>(value: "hello")
```

### 제네릭 클래스

```swift
class Stack<T> {
    var items: Array<T>

    init() {
        items = []
    }

    func push(item: T) {
        items.append(item)
    }

    func pop() -> T? {
        // ...
    }
}
```

### 제네릭 제약 (where 절)

```swift
func findMax<T>(array: Array<T>) -> T where T: Comparable {
    var max = array[0]
    for item in array {
        if item > max {
            max = item
        }
    }
    return max
}
```

### 복수 제약

```swift
func process<T, U>(a: T, b: U) -> String where T: Comparable, U: CustomStringConvertible {
    return String(b)
}
```

### 제네릭 특수화 (내부 동작)

제네릭 타입은 사용 시점에 구체 타입으로 특수화됩니다:
- `Box<Int>` → 내부 이름 `Box_Int`
- `Box<Array<String>>` → 내부 이름 `Box_Array_String`

---

## 13. 익스텐션

### 기존 타입 확장

```swift
extension Int {
    func isEven() -> Bool {
        return self % 2 == 0
    }

    var doubled: Int {
        return self * 2
    }
}

let x = 4
print(x.isEven())   // true
print(x.doubled)     // 8
```

### 프로토콜 적합 추가

```swift
extension String : Describable {
    func describe() -> String {
        return "String: ${self}"
    }
}
```

### 구조체 / 클래스 확장

```swift
extension Point {
    static func origin() -> Point {
        return Point(x: 0.0, y: 0.0)
    }

    mutating func translate(dx: Float, dy: Float) {
        x += dx
        y += dy
    }
}
```

---

## 14. 옵셔널

### 옵셔널 타입

```swift
var name: String? = "Kim"
var age: Int? = nil
```

### 옵셔널 바인딩

```swift
// if let
if let n = name {
    print("이름: ${n}")
}

// guard let
guard let n = name else {
    return
}
print("이름: ${n}")
```

### 강제 언래핑

```swift
let n: String = name!   // nil이면 런타임 에러
```

### 옵셔널 체이닝

```swift
let street = person?.address?.street   // 어느 하나라도 nil이면 전체가 nil
```

### nil 병합 연산자

```swift
let displayName = name ?? "Unknown"
```

---

## 15. 컬렉션

### Array

```swift
let numbers = [1, 2, 3, 4, 5]
let empty: Array<Int> = []

// 프로퍼티
print(numbers.count)      // 5
print(numbers.isEmpty)    // false

// 메서드
var mutable = [1, 2, 3]
mutable.append(4)         // [1, 2, 3, 4]

// 서브스크립트 접근
let first = numbers[0]    // 1
mutable[0] = 10           // [10, 2, 3, 4]
```

### Dictionary

```swift
let scores = ["Alice": 95, "Bob": 87, "Charlie": 92]
let empty: Dictionary<String, Int> = [:]

// 서브스크립트 접근
let aliceScore = scores["Alice"]     // 95 (옵셔널)

// 변경
var mutable = ["a": 1]
mutable["b"] = 2
```

### 범위 (Range)

```swift
let inclusive = 1...5     // 1, 2, 3, 4, 5
let exclusive = 1..<5     // 1, 2, 3, 4

for i in 0..<10 {
    print(i)
}
```

---

## 16. 튜플

### 튜플 리터럴

```swift
let point = (10, 20)
let named = (x: 10, y: 20)
let mixed = (name: "Kim", age: 25, active: true)
```

### 튜플 접근

```swift
// 인덱스 접근
let x = point.0    // 10
let y = point.1    // 20

// 레이블 접근
let x = named.x    // 10
let y = named.y    // 20
```

### 튜플 디스트럭처링

```swift
let (a, b) = point
let (name: n, age: a) = (name: "Kim", age: 25)
```

### 튜플 타입 어노테이션

```swift
let coord: (Int, Int) = (10, 20)
let person: (name: String, age: Int) = (name: "Kim", age: 25)
```

---

## 17. 문자열

### 문자열 리터럴

```swift
let greeting = "Hello, World!"
```

### 이스케이프 시퀀스

| 시퀀스 | 의미 |
|--------|------|
| `\"` | 큰따옴표 |
| `\\` | 백슬래시 |
| `\n` | 줄바꿈 |
| `\r` | 캐리지 리턴 |
| `\t` | 탭 |
| `\0` | 널 문자 |
| `\xHH` | 16진수 바이트 |
| `\u{HHHH}` | 유니코드 스칼라 |

### 문자열 보간

`${expression}` 구문으로 문자열 안에 표현식을 삽입합니다.

```swift
let name = "World"
let greeting = "Hello, ${name}!"
let calc = "2 + 3 = ${2 + 3}"
let nested = "magnitude: ${v1.magnitude()}"
```

### 문자열 연결

```swift
let full = "Hello" + " " + "World"
```

---

## 18. 에러 핸들링

swive는 `expected` 키워드 기반의 타입 안전한 에러 핸들링 시스템을 제공합니다. 함수가 실패할 수 있는 경우, 반환 타입에 `expected` 키워드로 에러 타입을 명시합니다.

### expected 함수 선언

`-> ReturnType expected ErrorType` 구문으로 실패 가능한 함수를 선언합니다.

```swift
func divide(a: Int, b: Int) -> Int expected String {
    if (b == 0) {
        return expected.error("Division by zero")
    }
    return a / b
}
```

- `return expected.error(value)` — 에러 값을 반환합니다.
- `return value` — 성공 값을 반환합니다 (자동으로 `$Expected.value(value)`로 래핑).
- 반환 타입이 `Int`, 에러 타입이 `String`이므로 성공 시 `Int`, 실패 시 `String`을 전달합니다.

### switch 패턴 매칭으로 결과 처리

expected 함수의 반환값은 `.value`와 `.error` 케이스를 가진 열거형처럼 동작합니다.

```swift
let result = divide(a: 10, b: 0)
switch result {
    case .value(v):
        print("결과: ${v}")
    case .error(e):
        print("에러: ${e}")
}
```

### if-let 스타일 처리

`if let`을 사용하면 성공 값을 간편하게 추출할 수 있습니다. 에러인 경우 `nil`로 변환되어 `else` 분기로 진입합니다.

```swift
if let v = divide(a: 10, b: 2) {
    print("성공: ${v}")
} else {
    print("실패")
}
```

### guard-let 스타일 처리

```swift
guard let v = divide(a: 10, b: 2) else {
    print("실패")
    return
}
print("성공: ${v}")
```

### 구조체 / 클래스 메서드에서의 사용

구조체와 클래스의 메서드에서도 `expected`를 사용할 수 있습니다.

```swift
struct Parser {
    func parse(input: String) -> Int expected String {
        if (input.isEmpty) {
            return expected.error("Empty input")
        }
        return Int(input)
    }
}
```

### 에러 타입

에러 타입은 어떤 타입이든 사용할 수 있습니다.

```swift
// String 에러
func fetchData(url: String) -> String expected String {
    return expected.error("Network timeout")
}

// Int 에러 코드
func connect(host: String) -> Bool expected Int {
    return expected.error(404)
}
```

---

## 19. 어트리뷰트

어트리뷰트는 `[AttributeName]` 구문으로 선언에 메타데이터를 부여합니다.

### 빌트인 어트리뷰트

| 어트리뷰트 | 설명 | 사용 대상 |
|------------|------|-----------|
| `[Deprecated("message")]` | 사용불가 예정 타입에 사용(사용 시 경고 발생) | 클래스, 구조체, 열거형 |
| `[Obsolete("message")]` | 사용불가 타입에 사용(사용 시 에러 발생) | 클래스, 구조체, 열거형 |
| `[Native.InternalCall]` | 네이티브 C++ 함수 바인딩 | 함수 |
| `[Native.Class("type")]` | 네이티브 C++ 클래스 바인딩 | 클래스 |
| `[Native.Struct("type")]` | 네이티브 C++ 구조체 바인딩 | 구조체 |
| `[Native.Property("name")]` | 네이티브 프로퍼티 바인딩 | 프로퍼티 |
| `[Native.Field("name")]` | 네이티브 필드 바인딩 | 필드 |
| `[Range(min, max)]` | 프로퍼티 값 범위 제한 (컴파일 타임 + 런타임 검증) | 프로퍼티 |

### 사용 예시

```swift
[Deprecated("Use NewClass instead")]
class OldClass {
    // ...
}

[Obsolete("This class has been removed")]
class RemovedClass {
    // ...
}

// OldClass 사용 → 경고(warning) 발생
// RemovedClass 사용 → 에러(error) 발생
```

### Range 어트리뷰트

프로퍼티에 `[Range(min, max)]`를 지정하면 값의 범위를 제한합니다. 리터럴 값은 컴파일 타임에, 변수 값은 런타임에 검증됩니다.

```swift
class Player {
    [Range(0, 100)]
    public var health: Int

    [Range(1, 99)]
    public var level: Int

    init(health: Int, level: Int) {
        self.health = health
        self.level = level
    }
}

let p = Player(health: 50, level: 1)    // OK
let p2 = Player(health: 200, level: 1)  // 컴파일 에러: Value 200 out of range [0, 100] for property 'health'
```

### 커스텀 어트리뷰트 선언

`attribute` 키워드로 커스텀 어트리뷰트를 정의할 수 있습니다.

```swift
attribute Serializable()
attribute Description(text: String)
attribute Validate(min: Int, max: Int)
```

### 커스텀 어트리뷰트 사용

선언한 어트리뷰트를 클래스, 구조체, 함수, 프로퍼티 등에 부착합니다.

```swift
[Serializable]
[Description("사용자 정보를 나타내는 클래스")]
class User {
    [Validate(1, 100)]
    var id: Int

    var name: String

    init(id: Int, name: String) {
        self.id = id
        self.name = name
    }
}
```

### 커스텀 어트리뷰트의 역할

커스텀 어트리뷰트는 **메타데이터 전용**입니다. swive 코드 자체에서는 동작을 정의하지 않으며, 네이티브(C++) 측에서 리플렉션 API를 통해 어트리뷰트 정보를 읽어 활용합니다.

| 구분 | 어트리뷰트 | 동작 방식 |
|------|-----------|-----------|
| **빌트인** | `Deprecated`, `Obsolete`, `Range`, `Native.*` | 컴파일러/VM이 직접 처리 |
| **커스텀** | 사용자 정의 어트리뷰트 | 메타데이터로만 존재, C++ 네이티브 코드에서 활용 |

> **참고:** 미등록 어트리뷰트를 사용하면 컴파일 에러가 발생합니다. 반드시 `attribute` 키워드로 먼저 선언해야 합니다.

### 네이티브 함수 바인딩

```swift
//[Native.InternalCall("노출된 이름")]
[Native.InternalCall("NativeClass_NativeAdd")] 
func nativeAdd(a: Int, b: Int) -> Int;   // 본문 없음, C++ 구현
```

---

## 20. 접근 제어

### 접근 수준

| 키워드 | 범위 |
|--------|------|
| `public` | 어디서든 접근 가능 |
| `internal` | 같은 모듈 내에서 접근 (기본값) |
| `fileprivate` | 같은 파일 내에서만 접근 |
| `private` | 같은 선언 내에서만 접근 |

### 사용 예시

```swift
public class APIClient {
    private var token: String = ""

    public func request(url: String) -> String {
        return fetch(url: url)
    }

    private func fetch(url: String) -> String {
        // 내부 구현
        return ""
    }
}

// 외부에서
let client = APIClient()
client.request(url: "https://...")  // OK
// client.token                     // 에러: private
// client.fetch(url: "...")         // 에러: private
```

### 타입 선언에 적용

```swift
public class PublicClass { }
private struct PrivateStruct { }
internal enum InternalEnum { }
```

---

## 21. 모듈 시스템

### import 구문

```swift
import MathLib
import "path/to/module.ss"
```

### 프로젝트 구조

`.ssproject` 파일로 프로젝트를 관리합니다:

```xml
<Project>
    <Name>MyProject</Name>
    <Entry>Scripts/main.ss</Entry>
    <ImportRoots>
        <Root>Libs</Root>
        <Root>Scripts</Root>
    </ImportRoots>
</Project>
```

`<ImportRoots>`에 지정된 경로에서 모듈을 검색합니다.

---

## 22. 네이티브 바인딩

swive는 C++ 네이티브 바인딩을 통해 확장 가능합니다.

### swive 측 선언

```swift
[Native.Class("Vector3")]
class Vector3 {
    [Native.Field("x")]
    var x: Float

    [Native.Field("y")]
    var y: Float

    [Native.Field("z")]
    var z: Float

    [Native.InternalCall]
    init(x: Float, y: Float, z: Float);

    [Native.InternalCall]
    func magnitude() -> Float;
}
```

### C++ 측 등록

```cpp
NativeRegistry registry;

// 함수 등록
registry.register_function("Vector3_Create",
    [](VM& vm, std::span<Value> args) -> Value {
        float x = args[0].as_float();
        float y = args[1].as_float();
        float z = args[2].as_float();
        auto* obj = vm.allocate_object<NativeObject>(
            new Vector3(x, y, z), "Vector3");
        return Value::from_object(obj);
    });

// 타입 등록
NativeTypeInfo info;
info.name = "Vector3";
info.constructor = []() -> void* { return new Vector3(); };
info.destructor = [](void* p) { delete static_cast<Vector3*>(p); };
info.methods["magnitude"] = { /* ... */ };
registry.register_type("Vector3", info);
```

---

## 23. 빌트인 함수

### 전역 함수

| 함수 | 시그니처 | 설명 |
|------|----------|------|
| `print` | `print(Any)` | 콘솔에 값 출력 (문장) |
| `readLine` | `readLine() -> String?` | 콘솔에서 한 줄 읽기 |

---

### Array 빌트인 메서드/프로퍼티

| 멤버 | 타입 | 설명 |
|------|------|------|
| `count` | `Int` (프로퍼티) | 배열 요소 수 |
| `isEmpty` | `Bool` (프로퍼티) | 배열이 비어있는지 |
| `append(element)` | `Void` (메서드) | 요소 추가 |

---

## 24. 키워드 목록

### 선언 키워드 (15개)

`func` `class` `struct` `enum` `protocol` `extension` `attribute`
`var` `let` `init` `deinit` `static` `override` `mutating` `import`

### 제어 흐름 키워드 (13개)

`if` `else` `guard` `switch` `case` `default`
`for` `in` `while` `repeat` `break` `continue` `return`

### 접근 제어 키워드 (4개)

`public` `private` `internal` `fileprivate`

### 에러 핸들링 키워드 (1개)

`expected`

### 프로퍼티 관련 키워드 (5개)

`get` `set` `willSet` `didSet` `lazy`

### 참조 관련 키워드 (2개)

`weak` `unowned`

### 타입 관련 키워드 (3개)

`as` `is` `where`

### 리터럴 / 특수 키워드 (6개)

`true` `false` `nil` `null` `self` `super`

**총 49개 키워드**

---

## 부록: 빠른 참조 예제

```swift
import MathLib

// 제네릭 구조체
struct Pair<T, U> {
    var first: T
    var second: U

    init(first: T, second: U) {
        self.first = first
        self.second = second
    }
}

// 프로토콜
protocol Printable {
    func toString() -> String
}

// 어트리뷰트가 있는 클래스
[Deprecated("Use ModernAnimal instead")]
class Animal : Printable {
    private var name: String
    [Range(0, 100)]
    public var age: Int

    var description: String {
        return "${name} (${age} years old)"
    }

    init(name: String, age: Int) {
        self.name = name
        self.age = age
    }

    func toString() -> String {
        return description
    }
}

// 익스텐션
extension Animal {
    func makeSound() -> String {
        return "Some sound"
    }
}

// 열거형 with 연관값
enum NetworkResult {
    case success(data: String)
    case failure(code: Int, message: String)
}

// expected 에러 핸들링
func divide(a: Int, b: Int) -> Int expected String {
    if (b == 0) {
        return expected.error("Division by zero")
    }
    return a / b
}

// 메인 함수
func main() {
    // 제네릭 구조체
    let pair = Pair<Int, String>(first: 42, second: "hello")
    print("Pair: ${pair.first}, ${pair.second}")

    // 옵셔널
    let input: String? = readLine()
    let name = input ?? "Anonymous"
    print("Hello, ${name}!")

    // 에러 핸들링 - switch 패턴 매칭
    let result = divide(a: 10, b: 0)
    switch result {
        case .value(v): print("Result: ${v}")
        case .error(e): print("Error: ${e}")
    }

    // 에러 핸들링 - if-let 스타일
    if let v = divide(a: 10, b: 2) {
        print("Success: ${v}")
    } else {
        print("Failed")
    }

    // 클래스 인스턴스
    let animal = Animal(name: "Buddy", age: 50)
    print("Animal: ${animal.toString()}")
    print("Sound: ${animal.makeSound()}")

    // 패턴 매칭
    let result_val = NetworkResult.success(data: "OK")
    switch result_val {
        case .success(data):
            print("Data: ${data}")
        case .failure(code, message):
            print("Error ${code}: ${message}")
    }

    // for-in with where
    for i in 1...100 where i % 15 == 0 {
        print("FizzBuzz: ${i}")
    }
}
```

---

## 25. 게임엔진 임베딩

Swive VM은 C API를 통해 다양한 게임엔진 및 호스트 애플리케이션에 임베드할 수 있습니다.

### 지원 엔진

| 엔진 | 연동 방식 | 설명 |
|------|---------|------|
| **Unreal Engine** | C++ 직접 호출 / DLL | `ss_embed.h` 헤더 포함 후 직접 호출 |
| **Unity** | C# P/Invoke | DLL로 빌드 후 `[DllImport]`로 호출 |
| **Godot** | GDExtension / C++ | 네이티브 모듈로 등록 |
| **커스텀 엔진** | C FFI | 어떤 언어든 C 함수 호출이 가능하면 연동 |

### 기본 사용 흐름

```
1. ss_create_context()           // 컨텍스트 생성
2. ss_register_function()        // 호스트 함수 등록
3. ss_set_print_callback()       // 출력 리다이렉션
4. ss_compile() / ss_load_bytecode()  // 스크립트 로드
5. ss_execute()                  // 실행
6. ss_call_function()            // 스크립트 함수 호출
7. ss_destroy_context()          // 정리
```

### C/C++ 임베딩 예제

```c
#include "ss_embed.h"

// 호스트 함수: 게임 오브젝트 위치 반환
SSResult get_player_pos(SSContext ctx, const SSValue* args,
                        int argc, SSValue* result) {
    float x = get_player_x();  // 엔진 API
    *result = SS_FLOAT(x);
    return SS_OK;
}

// 출력 리다이렉션
void engine_log(SSContext ctx, const char* msg, void* ud) {
    EngineConsole_Print(msg);  // 엔진 콘솔로 출력
}

int main() {
    SSContext ctx = ss_create_context();

    // 출력 리다이렉션
    ss_set_print_callback(ctx, engine_log, NULL);

    // 호스트 함수 등록
    ss_register_function(ctx, "getPlayerX", get_player_pos);

    // 글로벌 변수 주입
    ss_set_global(ctx, "SCREEN_WIDTH", SS_INT(1920));

    // 스크립트 컴파일 & 실행
    SSScript script = NULL;
    const char* code = "let x = getPlayerX()\nprint(\"Player X: ${x}\")";
    if (ss_compile(ctx, code, "game.ss", &script) == SS_OK) {
        SSValue result;
        ss_execute(ctx, script, &result);
        ss_destroy_script(script);
    } else {
        printf("Compile error: %s\n", ss_get_last_error(ctx));
    }

    ss_destroy_context(ctx);
    return 0;
}
```

### Unity (C#) P/Invoke 예제

```csharp
using System;
using System.Runtime.InteropServices;

public static class SwiveScript {
    const string DLL = "swive_embed";

    [DllImport(DLL)] public static extern IntPtr ss_create_context();
    [DllImport(DLL)] public static extern void ss_destroy_context(IntPtr ctx);
    [DllImport(DLL)] public static extern int ss_run(IntPtr ctx,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string source, IntPtr result);
    [DllImport(DLL)] public static extern int ss_register_function(IntPtr ctx,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string name, NativeFunc func);
    [DllImport(DLL)] public static extern IntPtr ss_get_last_error(IntPtr ctx);

    public delegate int NativeFunc(IntPtr ctx, IntPtr args, int argc, IntPtr result);
}

// 사용 예시
public class ScriptManager : MonoBehaviour {
    IntPtr ctx;

    void Awake() {
        ctx = SwiveScript.ss_create_context();
        SwiveScript.ss_register_function(ctx, "log", LogCallback);
    }

    void OnDestroy() {
        SwiveScript.ss_destroy_context(ctx);
    }

    public void RunScript(string code) {
        int result = SwiveScript.ss_run(ctx, code, IntPtr.Zero);
        if (result != 0) {
            Debug.LogError(Marshal.PtrToStringUTF8(
                SwiveScript.ss_get_last_error(ctx)));
        }
    }

    static int LogCallback(IntPtr ctx, IntPtr args, int argc, IntPtr result) {
        Debug.Log("[Swive] script log");
        return 0;
    }
}
```

### Unreal Engine 예제

```cpp
#include "ss_embed.h"

UCLASS()
class USwiveComponent : public UActorComponent {
    GENERATED_BODY()
    SSContext ScriptContext = nullptr;

    virtual void BeginPlay() override {
        ScriptContext = ss_create_context();

        // UE 로그로 리다이렉션
        ss_set_print_callback(ScriptContext,
            [](SSContext, const char* msg, void*) {
                UE_LOG(LogTemp, Log, TEXT("Swive: %s"),
                       UTF8_TO_TCHAR(msg));
            }, nullptr);

        // 네이티브 함수 등록
        ss_register_function(ScriptContext, "getActorName",
            [](SSContext ctx, const SSValue* args, int argc,
               SSValue* result) -> SSResult {
                auto* comp = (USwiveComponent*)ss_get_user_data(ctx);
                FString name = comp->GetOwner()->GetName();
                *result = SS_STRING(TCHAR_TO_UTF8(*name));
                return SS_OK;
            });

        ss_set_user_data(ScriptContext, this);
    }

    virtual void EndPlay(const EEndPlayReason::Type Reason) override {
        ss_destroy_context(ScriptContext);
        ScriptContext = nullptr;
    }

    UFUNCTION(BlueprintCallable)
    void RunScript(const FString& Code) {
        SSValue result;
        SSResult res = ss_run(ScriptContext,
                              TCHAR_TO_UTF8(*Code), &result);
        if (res != SS_OK) {
            UE_LOG(LogTemp, Error, TEXT("Script error: %s"),
                   UTF8_TO_TCHAR(ss_get_last_error(ScriptContext)));
        }
    }
};
```

### 프리컴파일 바이트코드

빌드 타임에 스크립트를 미리 컴파일하여 `.ssasm` 파일로 배포할 수 있습니다.

```c
// 컴파일 타임 (빌드 툴)
void* bytecode = NULL;
size_t bytecode_size = 0;
ss_compile_to_bytecode(ctx, source, "main.ss", &bytecode, &bytecode_size);
// bytecode를 파일로 저장...
ss_free_buffer(bytecode);

// 런타임 (게임 실행 시)
SSScript script = NULL;
ss_load_bytecode_file(ctx, "scripts/main.ssasm", &script);
ss_execute(ctx, script, NULL);
ss_destroy_script(script);
```

### API 요약

| 함수 | 설명 |
|------|------|
| `ss_create_context()` | VM 컨텍스트 생성 |
| `ss_create_context_ex(stack, debug)` | 커스텀 설정으로 생성 |
| `ss_destroy_context(ctx)` | 컨텍스트 해제 |
| `ss_compile(ctx, src, name, out)` | 소스 코드 컴파일 |
| `ss_compile_checked(ctx, src, name, out)` | 타입 체크 포함 컴파일 |
| `ss_load_bytecode(ctx, data, size, out)` | 바이트코드 로드 (메모리) |
| `ss_load_bytecode_file(ctx, path, out)` | 바이트코드 로드 (파일) |
| `ss_compile_to_bytecode(ctx, src, name, out, size)` | 바이트코드로 직렬화 |
| `ss_destroy_script(script)` | 스크립트 핸들 해제 |
| `ss_execute(ctx, script, result)` | 스크립트 실행 |
| `ss_run(ctx, src, result)` | 컴파일+실행 원스텝 |
| `ss_call_function(ctx, name, args, argc, result)` | 글로벌 함수 호출 |
| `ss_register_function(ctx, name, func)` | 네이티브 함수 등록 |
| `ss_unregister_function(ctx, name)` | 네이티브 함수 해제 |
| `ss_set_global(ctx, name, val)` | 글로벌 변수 설정 |
| `ss_get_global(ctx, name, out)` | 글로벌 변수 읽기 |
| `ss_set_print_callback(ctx, func, ud)` | 출력 리다이렉션 |
| `ss_set_error_callback(ctx, func, ud)` | 에러 콜백 설정 |
| `ss_get_last_error(ctx)` | 마지막 에러 메시지 |
| `ss_set_base_directory(ctx, dir)` | 모듈 기본 경로 설정 |
| `ss_add_import_path(ctx, path)` | 모듈 검색 경로 추가 |
| `ss_set_user_data(ctx, ptr)` | 사용자 데이터 저장 |
| `ss_get_user_data(ctx)` | 사용자 데이터 조회 |
| `ss_get_memory_stats(ctx, ...)` | 메모리 통계 조회 |
| `ss_version()` | 버전 문자열 반환 |

### 값 타입 (SSValue)

| 타입 | 매크로 | 설명 |
|------|--------|------|
| `SS_TYPE_NULL` | `SS_NULL()` | null 값 |
| `SS_TYPE_BOOL` | `SS_BOOL(b)` | 불리언 |
| `SS_TYPE_INT` | `SS_INT(i)` | 64비트 정수 |
| `SS_TYPE_FLOAT` | `SS_FLOAT(f)` | 64비트 실수 |
| `SS_TYPE_STRING` | `SS_STRING(s)` | 문자열 (콜백 스코프 내 유효) |
| `SS_TYPE_OBJECT` | `SS_OBJECT(p)` | 네이티브 객체 포인터 |

### 네이티브 객체 수명 관리

게임엔진에서 스크립트에 엔진 객체(Transform, GameObject 등)를 노출할 때, 객체의 소유권이 중요합니다.

#### 소유권 모델

| 모드 | 설명 | 사용 시나리오 |
|------|------|-------------|
| `SS_OWNERSHIP_VM` | VM이 native_ptr 소유. RC=0이면 destructor 호출 후 해제 | 스크립트 전용 객체 |
| `SS_OWNERSHIP_ENGINE` | 엔진이 native_ptr 소유. VM은 래퍼만 해제, native_ptr 안 건드림 | 엔진 오브젝트 노출 |

#### 수명 흐름도

```
[SS_OWNERSHIP_VM] (기본값)
  스크립트 참조 → RC++ → ... → RC=0 → ~NativeObject() → destructor(native_ptr) → 메모리 해제

[SS_OWNERSHIP_ENGINE]
  엔진이 포인터 전달 → ss_wrap_native(ENGINE) → 스크립트 참조
  ... → RC=0 → ~NativeObject() → release_notify(콜백) → 래퍼만 해제 (native_ptr 유지)

  엔진이 먼저 파괴 → ss_invalidate_native(ptr) → native_ptr = null → 스크립트에서 null 반환
```

#### 엔진 소유 객체 예제 (C++)

```c
// 1. 엔진 객체를 스크립트에 노출 (소유권은 엔진에)
Transform* tr = gameObject->GetTransform();
SSValue val;
ss_wrap_native(ctx, tr, "Transform", SS_OWNERSHIP_ENGINE, &val);
ss_set_global(ctx, "transform", val);

// 2. 스크립트에서 사용 가능:
//    let pos = transform.x   (정상 동작)

// 3. 엔진에서 오브젝트 파괴 시 - dangling 방지
ss_invalidate_native(ctx, tr);
// 이후 스크립트에서 transform 접근 → null 반환 (크래시 없음)
```

#### Release 콜백 (엔진 GC 연동)

```c
// VM이 엔진 소유 객체의 마지막 참조를 해제할 때 알림
void on_script_release(SSContext ctx, void* ptr,
                       const char* type, void* ud) {
    // Unreal: GC 대상으로 전환
    UObject* obj = static_cast<UObject*>(ptr);
    obj->RemoveFromRoot();

    // Unity: ref count 감소
    // custom_ref_release(ptr);
}
ss_set_release_callback(ctx, on_script_release, NULL);
```

#### 수명 관리 API

| 함수 | 설명 |
|------|------|
| `ss_wrap_native(ctx, ptr, type, ownership, out)` | 엔진 포인터를 스크립트 값으로 래핑 |
| `ss_set_ownership(ctx, val, mode)` | 소유권 모드 변경 |
| `ss_get_ownership(ctx, val, out)` | 소유권 모드 조회 |
| `ss_set_release_callback(ctx, func, ud)` | VM 해제 알림 콜백 설정 |
| `ss_invalidate_native(ctx, ptr)` | 엔진측에서 객체 무효화 (dangling 방지) |
| `ss_get_native_ptr(ctx, val)` | SSValue에서 원본 native 포인터 추출 |

### 에러 코드 (SSResult)

| 코드 | 값 | 설명 |
|------|----|------|
| `SS_OK` | 0 | 성공 |
| `SS_ERROR_COMPILE` | 1 | 컴파일 실패 |
| `SS_ERROR_RUNTIME` | 2 | 런타임 에러 |
| `SS_ERROR_INVALID_ARG` | 3 | 잘못된 인자 |
| `SS_ERROR_NOT_FOUND` | 4 | 함수/변수 없음 |
| `SS_ERROR_OUT_OF_MEMORY` | 5 | 메모리 부족 |
| `SS_ERROR_IO` | 6 | 파일 I/O 에러 |
| `SS_ERROR_TYPE_CHECK` | 7 | 타입 체크 실패 |
