// This source file is part of the Swift.org open source project
//
// Copyright 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import Foundation

import llbuildSwift

/// Specifier for a cell in a particular game state.
public struct Cell {
    public let x: Int
    public let y: Int
    /// The generation of the cell.
    public let gen: Int
    
    public init(x: Int, y: Int, gen: Int) {
        self.x = x
        self.y = y
        self.gen = gen
    }
    
    /// Convert a cell to a buildable Key.
    public func toKey() -> Key {
        return Key("cell:\(x),\(y),\(gen)")
    }
    
    /// Attempt to convert to a cell from a buildable Key.
    public static func fromKey(_ key: Key) -> Cell? {
        var keyStr = key.toString()
        guard let range = keyStr.range(of: "cell:") else { return nil }
        guard range.lowerBound == keyStr.startIndex else { return nil }
        keyStr.removeSubrange(range)
        
        // We should now have 3 numbers separated by ','.
        let components = keyStr.components(separatedBy: ",")
        guard components.count == 3 else { return nil }
        guard let x = Int(components[0]), let y = Int(components[1]), let gen = Int(components[2]) else { return nil }
        return Cell(x: x, y: y, gen: gen)
    }
}

// Game Of Life Build System

/// Trivial task for assigning the value of a static cell (e.g., an initial one).
class StaticCellTask: Task {
    let isLive: Bool
    
    init(isLive: Bool) { self.isLive = isLive }
    
    func start(_ engine: TaskBuildEngine) {}
    func provideValue(_ engine: TaskBuildEngine, inputID: Int, value: Value) {}
    func inputsAvailable(_ engine: TaskBuildEngine) {
        engine.taskIsComplete(Value([isLive ? 1 : 0]), forceChange: false)
    }
}

/// Rule for driving a static cell task.
class StaticCellRule: Rule {
    let isLive: Bool
    
    init(isLive: Bool) {
        self.isLive = isLive
    }
    
    func createTask() -> Task {
        return StaticCellTask(isLive: isLive)
    }
    
    func isResultValid(_ priorValue: Value) -> Bool {
        return false //priorValue.data.count == 1 && priorValue.data[0] == (isLive ? 1 : 0)
    }
}

/// Compute the value for a particular cell.
class ComputeCellTask: Task {
    /// The cell being computed.
    let cell: Cell
    
    /// Whether the cell was live in the previous generation.
    var wasLive: Bool = false
    
    /// The number of adjacent live cells.
    var numAdjacentLive: Int = 0
    
    init(_ cell: Cell) {
        assert(cell.gen > 0)
        self.cell = cell
    }
    
    func start(_ engine: TaskBuildEngine) {
        // Request the necessary inputs.
        for oy in [-1,0,1] {
            for ox in [-1,0,1] {
                let key = Cell(x: cell.x+ox, y: cell.y+oy, gen: cell.gen-1).toKey()
                if (oy == 0 && ox == 0) {
                    engine.taskNeedsInput(key, inputID: 0)
                } else {
                    engine.taskNeedsInput(key, inputID: 1)
                }
            }
        }
    }
    
    func provideValue(_ engine: TaskBuildEngine, inputID: Int, value: Value) {
        assert(value.data.count == 1)
        let inputIsLive = value.data[0] != 0
        
        // If this is the request for the cell's prior value, set whether it was live.
        if inputID == 0 {
            wasLive = inputIsLive
        } else {
            // Otherwise, accumulate the number of adjacent live cells.
            numAdjacentLive += inputIsLive ? 1 : 0
        }
    }
    
    func inputsAvailable(_ engine: TaskBuildEngine) {
        // Determine the output result.
        var isLive = wasLive
        if (wasLive) {
            if numAdjacentLive < 2 || numAdjacentLive > 3 {
                isLive = false
            }
        } else {
            if numAdjacentLive == 3 {
                isLive = true
            }
        }
        
        engine.taskIsComplete(Value([isLive ? 1 : 0]), forceChange: false)
    }
}

class ComputeCellRule: Rule {
    let cell: Cell
    
    init(cell: Cell) { self.cell = cell }
    
    func createTask() -> Task {
        return ComputeCellTask(cell)
    }
}

/// Compute the value for an entire generation.
class ComputeGenTask: Task {
    let width: Int
    let height: Int
    let gen: Int
    
    var result: [UInt8]
    
    init(width: Int, height: Int, gen: Int) {
        self.width = width
        self.height = height
        self.gen = gen
        self.result = Array(repeating: UInt8(0), count: width * height)
    }
    
    func start(_ engine: TaskBuildEngine) {
        // Request the necessary inputs.
        for y in 0..<height {
            for x in 0..<width {
                engine.taskNeedsInput(Cell(x: x, y: y, gen: gen).toKey(), inputID: y*width + x)
            }
        }
    }
    
    func provideValue(_ engine: TaskBuildEngine, inputID: Int, value: Value) {
        assert(value.data.count == 1)
        result[inputID] = value.data[0]
    }
    
    func inputsAvailable(_ engine: TaskBuildEngine) {
        engine.taskIsComplete(Value(result), forceChange: false)
    }
}

class ComputeGenRule: Rule {
    let width: Int
    let height: Int
    let gen: Int
    
    init(width: Int, height: Int, gen: Int) {
        self.width = width
        self.height = height
        self.gen = gen
    }
    
    func createTask() -> Task {
        return ComputeGenTask(width: width, height: height, gen: gen)
    }
}

//

public protocol GameOfLifeBoard {
    var width: Int { get }
    var height: Int { get }
    var data: [[Bool]] { get }
}

open class GameOfLifeBuildEngineDelegate: BuildEngineDelegate {
    let initialBoard: GameOfLifeBoard
    
    public init(initialBoard: GameOfLifeBoard) {
        self.initialBoard = initialBoard
    }
    
    open func lookupRule(_ key: Key) -> Rule {
        // Check if this is a cell rule.
        if let cell = Cell.fromKey(key) {
            // If this cell is outside the board, it is always dead.
            if cell.x < 0 || cell.y < 0 || cell.x >= initialBoard.width || cell.y >= initialBoard.height {
                return StaticCellRule(isLive: false)
            }
            
            // If this is a lookup in generation 0, return a rule to provide the initial state.
            if cell.gen == 0 {
                return StaticCellRule(isLive: initialBoard.data[cell.y][cell.x])
            }
            
            // Otherwise, return a normal compute rule.
            return ComputeCellRule(cell: cell)
        }
        
        // Check if this is a gen rule.
        var keyStr = key.toString()
        if let range = keyStr.range(of: "gen-") {
            if range.lowerBound == keyStr.startIndex {
                keyStr.removeSubrange(range)
                if let gen = Int(keyStr) {
                    return ComputeGenRule(width: initialBoard.width, height: initialBoard.height, gen: gen)
                }
            }
        }
        
        fatalError("lookup of unknown rule \(key)")
    }
}
