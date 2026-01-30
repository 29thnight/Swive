// Test builtin protocol conformance
struct Container<T> where T: Comparable {
    var item: T
}

var c = Container<Int>(42)
print(c.item)
