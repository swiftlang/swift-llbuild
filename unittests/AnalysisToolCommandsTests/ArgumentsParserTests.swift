// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest
import AnalysisToolCommands

class ArgumentsParserTests: XCTestCase {
    
    func testIntParse() throws {
        XCTAssertEqual(try Int(raw: "123"), 123)
        XCTAssertThrowsError(try Int(raw: "f")) { err in
            XCTAssertEqual(err as? ParseError, .cantParse(description: #"Can't construct integer value from "f"."#))
        }
    }
    
    func testStringParse() throws {
        XCTAssertEqual(try String(raw: "foobar"), "foobar")
        XCTAssertEqual(try String(raw: "foo bar"), "foo bar")
    }
    
    func testBoolParse() throws {
        XCTAssertEqual(try Bool(raw: ""), false)
        XCTAssertEqual(try Bool(raw: "yes"), true)
        XCTAssertEqual(try Bool(raw: "YES"), true)
        XCTAssertEqual(try Bool(raw: "true"), true)
        XCTAssertEqual(try Bool(raw: "True"), true)
        XCTAssertEqual(try Bool(raw: "255"), true)
        XCTAssertEqual(try Bool(raw: "0"), false)
        XCTAssertEqual(try Bool(raw: "-1"), false)
        XCTAssertEqual(try Bool(raw: "false"), false)
        XCTAssertEqual(try Bool(raw: "NO"), false)
        XCTAssertEqual(try Bool(raw: "False"), false)
    }
    
    func testEnumParse() throws {
        enum Foo: String, OptionParsable, CaseIterable {
            case bar, baz
        }
        
        XCTAssertEqual(try Foo(raw: "bar"), .bar)
        XCTAssertEqual(try Foo(raw: "baz"), .baz)
        XCTAssertThrowsError(try Foo(raw: "foo")) {
            XCTAssertEqual($0 as? ParseError, .cantParse(description: "Unknown raw value 'foo' for Foo. Possible values: [bar, baz]."))
        }
        
        enum Bar: Int, OptionParsable, CaseIterable {
            case foo, baz
        }
        
        XCTAssertEqual(try Bar(raw: "0"), .foo)
        XCTAssertEqual(try Bar(raw: "1"), .baz)
        XCTAssertThrowsError(try Bar(raw: "3")) {
            XCTAssertEqual($0 as? ParseError, .cantParse(description: "Unknown raw value '3' for Bar. Possible values: [0, 1]."))
        }
    }
    
    func testParser() throws {
        final class Hello: Command {
            struct Options: OptionsType {
                enum Mode: String, CaseIterable, OptionParsable {
                    case full, half
                }
                
                var quite = false
                var path = ""
                var number: Int?
                var pretty = false
                var mode = Mode.half
                
                func bind(to binder: inout Binder<Hello.Options>) {
                    binder.bind(form: .short(short: "q"), keyPath: \.quite, required: true, isOption: true)
                    binder.bind(form: .both(short: "p", long: "path"), keyPath: \.path)
                    binder.bind(form: .short(short: "n"), keyPath: \.number)
                    binder.bind(form: .long(long: "pretty"), keyPath: \.pretty, isOption: true)
                    binder.bind(form: .short(short: "m"), keyPath: \.mode)
                }
            }
            
            static var options = Options()
            
            static func run(options: Hello.Options) throws {
                self.options = options
            }
        }
        
        func checkValues(_ args: [String], _ check: (Hello.Options) throws -> Void = { _ in }) throws {
            try Hello.run(args: args)
            try check(Hello.options)
        }
        
        try checkValues(["-q"]) {
            XCTAssertTrue($0.quite)
        }
        
        try checkValues(["-q", "-p", "/foo/bar"]) {
            XCTAssertTrue($0.quite)
            XCTAssertEqual($0.path, "/foo/bar")
            XCTAssertNil($0.number)
        }
        
        try checkValues(["-q", "--path", "/foo/bar"]) {
            XCTAssertTrue($0.quite)
            XCTAssertEqual($0.path, "/foo/bar")
            XCTAssertNil($0.number)
        }
        
        try checkValues(["-q", "-n", "123"]) {
            XCTAssertEqual($0.number, 123)
        }
        
        try checkValues(["-q", "--pretty"]) {
            XCTAssertTrue($0.pretty)
        }
        
        XCTAssertThrowsError(try checkValues(["-q", "-p", "/foo", "--path", "/bar"])) {
            XCTAssertEqual($0 as? ParseError, ParseError.tooManyArguments(rest: ["--path", "/bar"]))
        }
        
        XCTAssertThrowsError(try checkValues([])) {
            XCTAssertEqual($0 as? ParseError, ParseError.cantFindOption(form: .short(short: "q"), args: []))
        }
        
        XCTAssertThrowsError(try checkValues(["-q", "-n"])) {
            XCTAssertEqual($0 as? ParseError, ParseError.cantFindRequiredValue(form: .short(short: "n"), args: []))
        }
        
        XCTAssertThrowsError(try checkValues(["-q", "-p"])) {
            XCTAssertEqual($0 as? ParseError, ParseError.cantFindRequiredValue(form: .both(short: "p", long: "path"), args: []))
        }
        
        XCTAssertThrowsError(try checkValues(["-q", "-q"])) {
            XCTAssertEqual($0 as? ParseError, ParseError.tooManyArguments(rest: ["-q"]))
        }
    }
    
}
