// Simple generic test
struct Box<T> {
    var value: T
}

var box = Box<Int>(42)
print(box.value)
