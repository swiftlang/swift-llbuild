// This source file is part of the Swift.org open source project
//
// Copyright 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest

// The Swift package has llbuildSwift as module
#if SWIFT_PACKAGE
import llbuild
import llbuildSwift
#else
import llbuild
#endif

@available(macOS 10.15, *)
class SwiftNinjaTests: XCTestCase {
  func testBasics() throws {
    let ruleFile = makeTemporaryFile("""
            rule CMD
                command = ls $in $out $statementvar
                description = xyz $statementvar\n
            """)
    let manifestFile = makeTemporaryFile("""
            include \(URL(fileURLWithPath: ruleFile).lastPathComponent)
            topvar = x
            build output: CMD input1 input2 | implicit-input || order-only-input
                generator = true
                restat = true
                statementvar = $topvar y\n
            """)
    let manifest = try NinjaManifest(
      path: manifestFile,
      workingDirectory: URL(fileURLWithPath: manifestFile)
        .deletingLastPathComponent().path)

    let expectedRule = NinjaRule(
      name: "CMD", variables: ["command": "ls $in $out $statementvar",
                               "description": "xyz $statementvar"])
    XCTAssertEqual(manifest.rules["CMD"], expectedRule)
    XCTAssertEqual(manifest.statements, [
      NinjaBuildStatement(
        rule: expectedRule,
        command: "ls input1 input2 output x y",
        description: "xyz x y",
        explicitInputs: ["input1", "input2"],
        implicitInputs: ["implicit-input"],
        orderOnlyInputs: ["order-only-input"],
        outputs: ["output"],
        variables: [
          "generator": "true",
          "restat": "true",
          "statementvar": "x y"
        ],
        generator: true,
        restat: true)
    ])
  }

  func testMissingRule() {
    let manifestFile = makeTemporaryFile("""
            build output: CMD input\n
            """)
    func assertExpectedError(_ manifestErrors: String?) {
      guard var errors = manifestErrors else {
        XCTFail("Missing expected manifest error")
        return
      }
      if errors.isEmpty {
        errors = "<empty>"
      }
      XCTAssertTrue(errors.hasSuffix("1:14: unknown rule\n"),
                    "Missing expected error in:\n\(errors)")
    }

    XCTAssertThrowsError(try NinjaManifest(
      path: manifestFile,
      workingDirectory: URL(fileURLWithPath: manifestFile)
        .deletingLastPathComponent().path)) { error in
      guard case NinjaError.invalidManifest(let errors) = error else {
        XCTFail("Load error was not a NinjaError.invalidManifest")
        return
      }
      assertExpectedError(errors)
    }

    let (manifest, errors) = NinjaManifest.createNonThrowing(
      path: manifestFile,
      workingDirectory: URL(fileURLWithPath: manifestFile)
        .deletingLastPathComponent().path)

    XCTAssertNil(manifest.rules["CMD"])
    XCTAssertEqual(manifest.statements, [
      NinjaBuildStatement(
        rule: manifest.rules["phony"]!,
        command: "",
        description: "",
        explicitInputs: ["input"],
        implicitInputs: [],
        orderOnlyInputs: [],
        outputs: ["output"],
        variables: [:],
        generator: false,
        restat: false)
    ])
    assertExpectedError(errors)
  }
}
