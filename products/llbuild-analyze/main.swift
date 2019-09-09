// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import TSCUtility
import TSCBasic

public final class HelloWorldTool: Tool<HelloWorldTool.Options> {

    public struct Options: Option {
        var quite = false
        var name: String?
        
        public init() {}
    }

    public override class var toolName: String { return "hello" }
    
    required init(args: [String]) throws {
        try super.init(usage: "Use me", overview: "Some overview", args: args)
    }
    
    override class func defineArguments(parser: ArgumentParser, binder: ArgumentBinder<Options>) {
        
        binder.bind(
            option: parser.add(option: "--quite", shortName: "-q", kind: Bool.self, usage: "Operate quitly"),
            to: { $0.quite = $1 })
        
        binder.bind(
            option: parser.add(option: "-n", usage: "Specify a name"),
            to: { $0.name = $1 })
    }
    
    public override func runImpl() throws {
        guard false == options.quite else { return }
        print("Hello \(options.name ?? "World")")
    }
}

try [HelloWorldTool.self].run()
