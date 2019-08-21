//// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest
import AnalysisToolCommands

class RunTests: XCTestCase {
    
    func test() throws {
        struct Mock: Command {
            struct Options: OptionsType {
                var flag = 0
                func bind(to binder: inout Binder<Mock.Options>) {
                    binder.bind(form: .short(short: "f"), keyPath: \.flag)
                }
            }
            
            static var ran = false
            
            
            static func run(options: Mock.Options) throws {
                XCTAssertFalse(ran, "Expect a command to run once.")
                ran = true
            }
        }
        
        AnalysisToolCommands.run(using: [Mock.self], arguments: ["llbuild-analysis", "mock"])
        XCTAssertTrue(Mock.ran)
    }
    
}
