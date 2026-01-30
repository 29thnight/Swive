// 제네릭 없이 동작하는 Box 구조체 예제 (현재 SwiftScript에서 동작)

// Int용 Box
struct IntBox {
    var value: Int
    func describe() {
        print(value)
    }
}

// String용 Box
struct StringBox {
    var value: String
    func describe() {
        print(value)
    }
}

var intBox = IntBox(10)
var stringBox = StringBox("SwiftScript")

print("Int Box:")
intBox.describe()

print("String Box:")
stringBox.describe()
