// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import AnalysisToolCommands

struct HelloWorld: Command {
    struct Options: OptionsType {
        var quite = false
        var name: String?
        
        func bind(to binder: inout Binder<HelloWorld.Options>) {
            binder.bind(form: .both(short: "q", long: "quite"), keyPath: \.quite, isOption: true)
            binder.bind(form: .short(short: "n"), keyPath: \.name)
        }
    }
    
    static var toolName: String { return "hello" }
    
    static func run(options: HelloWorld.Options) throws {
        if options.quite { return }
        print("Hello \(options.name ?? "World")")
    }
}

run(using: [
    HelloWorld.self,
    ])
