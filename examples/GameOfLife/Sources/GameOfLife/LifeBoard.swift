// This source file is part of the Swift.org open source project
//
// Copyright 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

/// A representation of a board in Conway's "Game of Life".
public final class LifeBoard: Codable {
    public let width: Int
    public let height: Int
    public var data: [[Bool]]
    
    public init(width: Int, height: Int, points: [(Int, Int)] = []) {
        self.width = width
        self.height = height
        self.data = (0 ..< height).map{ _ in (0 ..< width).map{ _ in false } }

        add(points: points)
    }
    
    public func add(points: [(Int,Int)]) {
        for (x, y) in points {
            self.data[y][x] = true
        }
    }
}
