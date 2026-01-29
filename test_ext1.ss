struct Point {
    var x: Int
    var y: Int
}

extension Point {
    func sum() -> Int {
        return self.x + self.y
    }
}

var p = Point(3, 4)
print(p.sum())
