struct Counter {
    var count: Int = 0
    
    mutating func increment() {
        self.count = self.count + 1
    }
}

var c = Counter()
print(c.count)
c.increment()
print(c.count)
