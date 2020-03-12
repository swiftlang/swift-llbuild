// This source file is part of the Swift.org open source project
//
// Copyright 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import Foundation

/// An individual rule in a Ninja manifest.
public struct Rule: Codable, Equatable {
    /// The command to run for this rule, if present.
    public let command: String?

    /// The description of this rule, if present.
    public let description: String?

    public init(
        command: String? = nil,
        description: String? = nil
    ) {
        self.command = command
        self.description = description
    }
}

/// An individual command in a Ninja manifest.
public struct Command: Codable, Equatable {
    /// The name of the rule which produced this command.
    public let rule: String

    /// The list of inputs to the command.
    public let inputs: [String]

    /// The list of outputs of the command.
    public let outputs: [String]

    /// The command itself (a string to run with "/bin/sh -c").
    public let command: String

    /// The description of the string, or empty.
    public let description: String

    public init(
        rule: String,
        inputs: [String],
        outputs: [String],
        command: String,
        description: String
    ) {
        self.rule = rule
        self.inputs = inputs
        self.outputs = outputs
        self.command = command
        self.description = description
    }
}

public class NinjaManifest: Codable {
    public enum Error: Swift.Error {
        case unableToLocateBinary
    }

    /// The name of the source manifest file.
    public let filename: String

    /// The dictionary of build rules.
    public let rules: [String: Rule]

    /// The list of build commands.
    public let commands: [Command]

    private static func findBinaryPath() throws -> URL {
        guard let exePath = Bundle.main.executablePath else {
            throw Error.unableToLocateBinary
        }

        // Return the FooBar.xctest name if we're running under xctest.
        if exePath.hasSuffix("xctest") {
            for bundle in Bundle.allBundles {
                if bundle.bundlePath.hasSuffix(".xctest") {
                    return URL(fileURLWithPath: bundle.bundlePath)
                }
            }
        }

        return URL(fileURLWithPath: exePath)
    }
    
    private static func findLLBuildBinary() throws -> URL {
        return try findBinaryPath().deletingLastPathComponent().appendingPathComponent("llbuild")
    }
    
    public init(path: String) throws {
        // For now, we depend on the `llbuild` binary to be adjust to the
        // package using this logic, rather than building APIs to llbuild's
        // Ninja layer. This will eventually need to get cleaned up.
        let p = Process()
        p.launchPath = try Self.findLLBuildBinary().path
        p.arguments = ["ninja", "load-manifest", "--json", path]
        let pipe = Pipe()
        p.standardOutput = pipe
        p.launch()
        var data: Data!
        let sema = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            data = pipe.fileHandleForReading.readDataToEndOfFile()
            sema.signal()
        }
        p.waitUntilExit()
        sema.wait()
        let parsed = try JSONDecoder().decode(NinjaManifest.self, from: data)
        self.filename = parsed.filename
        self.rules = parsed.rules
        self.commands = parsed.commands
    }
}
