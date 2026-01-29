struct Point {
    var x: Int
    var y: Int
}

extension Point {
    func distance() -> Int {
        return self.x + self.y
    }
}

var p = Point(x: 3, y: 4)
print(p.distance())
