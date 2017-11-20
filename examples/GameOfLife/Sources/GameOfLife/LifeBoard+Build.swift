// This source file is part of the Swift.org open source project
//
// Copyright 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import llbuildSwift

// Extensions we use.
extension LifeBoard: GameOfLifeBoard {}
extension Value {
    public func toBool() -> Bool {
        assert(data.count == 1)
        return data[0] != 0
    }
    
    public func toBoard(_ width: Int, _ height: Int) -> LifeBoard {
        assert(data.count == width * height)
        let board = LifeBoard(width: width, height: height)
        for y in 0..<board.height {
            for x in 0..<board.width {
                board.data[y][x] = data[y*board.width + x] != 0
            }
        }
        return board
    }
}

