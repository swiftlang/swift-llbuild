// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest
import Core

class ArgumentsParserTests: XCTestCase {
    
    func testIntParse() throws {
        XCTAssertEqual(try Int.construct(from: "123"), 123)
        XCTAssertThrowsError(try Int.construct(from: "f")) { err in
            XCTAssertEqual(err as? ParseError, .cantParse(description: #"Can't construct integer value from "f"."#))
        }
    }
    
    func testStringParse() throws {
        XCTAssertEqual(try String.construct(from: "foobar"), "foobar")
        XCTAssertEqual(try String.construct(from: "foo bar"), "foo bar")
    }
    
    func testBoolParse() throws {
        XCTAssertEqual(try Bool.construct(from: ""), false)
        XCTAssertEqual(try Bool.construct(from: "yes"), true)
        XCTAssertEqual(try Bool.construct(from: "YES"), true)
        XCTAssertEqual(try Bool.construct(from: "true"), true)
        XCTAssertEqual(try Bool.construct(from: "True"), true)
        XCTAssertEqual(try Bool.construct(from: "255"), true)
        XCTAssertEqual(try Bool.construct(from: "0"), false)
        XCTAssertEqual(try Bool.construct(from: "-1"), false)
        XCTAssertEqual(try Bool.construct(from: "false"), false)
        XCTAssertEqual(try Bool.construct(from: "NO"), false)
        XCTAssertEqual(try Bool.construct(from: "False"), false)
    }
    
    func testParser() throws {
        let intOption = ArgumentsParser.Option(name: "int", kind: .both(short: "i", long: "int"), type: Int.self)
        let intOption2 = ArgumentsParser.Option(name: "int2", kind: .long(long: "int2"), type: Int.self)
        let stringOption = ArgumentsParser.Option(name: "string", kind: .short(short: "s"), type: String.self)
        let boolOption = ArgumentsParser.Option(name: "bool", needsValue: false, kind: .short(short: "b"), type: Bool.self)
        
        let parser = ArgumentsParser(options: [intOption, intOption2, stringOption, boolOption])
        
        func checkValues(_ args: [String], _ block: (ArgumentsParser.Values) throws -> Void = {_ in}) throws {
            let values = try parser.parse(args: args)
            try block(values)
        }
        
        try checkValues(["-s", "string"]) {
            XCTAssertEqual("string", $0[stringOption])
        }
        
        try checkValues(["-i", "123"]) {
            XCTAssertEqual(123, $0[intOption])
        }
        
        try checkValues(["--int2", "456"]) {
            XCTAssertEqual(456, $0[intOption2])
        }
        
        try checkValues(["--int", "123"]) {
            XCTAssertEqual(123, $0[intOption])
        }
        
        try checkValues(["-b"]) {
            XCTAssertEqual(true, $0[boolOption])
        }
        
        XCTAssertThrowsError(try checkValues(["-i", "123", "--int", "456"])) {
            XCTAssertEqual($0 as? ParseError, .tooManyArguments(rest: ["--int", "456"]))
        }
        
        XCTAssertThrowsError(try checkValues(["123"])) {
            XCTAssertEqual($0 as? ParseError, .tooManyArguments(rest: ["123"]))
        }
    }
    
    func testEnumParsing() throws {
        enum Foo: String, OptionParseable, CaseIterable {
            case bar, baz
        }
        
        XCTAssertEqual(try Foo.construct(from: "bar"), .bar)
        XCTAssertEqual(try Foo.construct(from: "baz"), .baz)
        XCTAssertThrowsError(try Foo.construct(from: "foo")) {
            XCTAssertEqual($0 as? ParseError, .cantParse(description: "Unknown raw value 'foo' for Foo. Possible values: [bar, baz]."))
        }
        
        enum Bar: Int, OptionParseable, CaseIterable {
            case foo, baz
        }
        
        XCTAssertEqual(try Bar.construct(from: "0"), .foo)
        XCTAssertEqual(try Bar.construct(from: "1"), .baz)
        XCTAssertThrowsError(try Bar.construct(from: "3")) {
            XCTAssertEqual($0 as? ParseError, .cantParse(description: "Unknown raw value '3' for Bar. Possible values: [0, 1]."))
        }
    }
    
}
