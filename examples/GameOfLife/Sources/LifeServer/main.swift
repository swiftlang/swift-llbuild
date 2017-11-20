// This source file is part of the Swift.org open source project
//
// Copyright 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import Dispatch
import Foundation

import HTTP

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
let queue = DispatchQueue(label: "org.llbuild.examples.GameOfLife")
let board = LifeBoard(width: 16, height: 16, points: InitialState.boom.points)
var delegate = GameOfLifeBuildEngineDelegate(initialBoard: board)
var engine = BuildEngine(delegate: delegate)

// MARK: HTTP Server


let server = HTTPServer()
try! server.start(port: 8080) { (request, response) in
    print("note: \(CommandLine.arguments[0]) \(request.method) \(request.target)")

    switch request.target {
        // The index endpoint.
    case "/":
        response.writeHeader(status: .ok) 
        response.writeBody(try! Data(contentsOf: URL(fileURLWithPath: "Static/index.html")))
        response.done() 

        // The JavaScript to render the Game of Life.
    case "/Static/PlayLife.js":
        response.writeHeader(status: .ok) 
        response.writeBody(try! Data(contentsOf: URL(fileURLWithPath: "Static/PlayLife.js")))
        response.done() 

        // The JSON endpoints to fetch frames.
    case let t where t.starts(with: "/frame/"):
        let gen = Int(t.components(separatedBy: "/")[2]) ?? 0
        queue.sync {
            let frame = engine.build(key: Key("gen-\(gen)")).toBoard(board.width, board.height)
            response.writeHeader(status: .ok, headers: [.contentType: "application/javascript"])
            response.writeBody(try! JSONEncoder().encode(frame))
            response.done()
        }
        
    default:
        response.writeHeader(status: .notFound) 
        response.writeBody("404 Not Found") 
        response.done() 
    }

    return .discardBody 
}

RunLoop.current.run()
