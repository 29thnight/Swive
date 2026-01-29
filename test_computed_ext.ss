struct Box {
    var size: Int
}

extension Box {
    var doubled: Int {
        return self.size * 2
    }
}

var box = Box(20)
print(box.doubled)
