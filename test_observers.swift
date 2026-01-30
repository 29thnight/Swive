// Test property observers
class Counter {
    var value: Int = 0 {
        willSet {
            print("Will set to:")
            print(newValue)
        }
        didSet {
            print("Did set from:")
            print(oldValue)
        }
    }
}

var c = Counter()
print("Created counter")
c.value = 5
print("Final value:")
print(c.value)
