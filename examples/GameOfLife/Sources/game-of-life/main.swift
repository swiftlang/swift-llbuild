// This source file is part of the Swift.org open source project
//
// Copyright 2017-2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import Dispatch
import Foundation

import GameOfLife
import llbuildSwift

// MARK: Shared Game of Life

/// Interesting initial Game of Life states.
enum InitialState {
    /// A typical glider.
    case glider

    /// A simple toggling state.
    case toggle

    /// An explosion!
    case boom

    /// The initial points for the state.
    var points: [(Int, Int)] {
        switch self {
        case .glider:
            return [(1,1), (2,2), (3,2), (2,3), (1,3)]
        case .toggle:
            return [(5,5), (6,5), (4,5), (5,4)]
        case .boom:
            return [(5,5), (6,5), (4,5), (5,4), (5,6), (7,9), (7,8), (8,8)]
        }
    }
}

/// The shared life board to render.
let board = LifeBoard(width: 16, height: 16, points: InitialState.boom.points)
var delegate = GameOfLifeBuildEngineDelegate(initialBoard: board)
var engine = BuildEngine(delegate: delegate)

// Just print the frames to the console.

for gen in 0 ..< 30 {
    let frame = engine.build(key: Key("gen-\(gen)")).toBoard(board.width, board.height)
    print(frame)
    print()
}

