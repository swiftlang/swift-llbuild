// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import Core
import llbuildSwift
import llbuildAnalysis

import class Foundation.JSONEncoder
import struct Foundation.Data
import class Foundation.FileManager

public final class CriticalPathTool: Command<CriticalPathTool.Options> {
    public struct Options: OptionsType {
        let databasePath: String
        let outputPath: String?
        let quiet: Bool
        let pretty: Bool
        
        private enum Const {
            static let databaseOption = ArgumentsParser.Option(name: "databasePath", required: true, kind: .both(short: "d", long: "database"), type: String.self)
            static let outputOption = ArgumentsParser.Option(name: "outputPath", kind: .both(short: "o", long: "output"), type: String.self)
            static let quietOption = ArgumentsParser.Option(name: "quiet", needsValue: false, kind: .both(short: "q", long: "quiet"), type: Bool.self)
            static let prettyOption = ArgumentsParser.Option(name: "prettyPrint", needsValue: false, kind: .long(long: "pretty"), type: Bool.self)
        }
        
        public static let options: [ArgumentsParser.Option] = [
            Const.databaseOption,
            Const.outputOption,
            Const.quietOption,
            Const.prettyOption,
        ]
        
        public init(values: ArgumentsParser.Values) {
            self.databasePath = values[Const.databaseOption]
            self.outputPath = values[Const.outputOption]
            self.quiet = values[Const.quietOption] ?? false
            self.pretty = values[Const.prettyOption] ?? false
        }
    }
    
    override public class var toolName: String { "critical-path" }
    
    public override func run() throws {
        let database = try BuildDB(path: options.databasePath, clientSchemaVersion: 9)
        let solver = try CriticalPathSolver(database: database)
        let path = try solver.calculateCriticalPath()
        if let outputPath = options.outputPath {
            let data = try json(path)
            try verifyOutput()
            FileManager.default.createFile(atPath: outputPath, contents: data)
        }
        
        if options.quiet { return }
        let output = path.map { "Critical Path:\n\($0)" } ?? "Couldn't critical path from database at \(options.databasePath) because no builds were build."
        print(output)
    }
    
    public func json(_ path: CriticalPath?) throws -> Data {
        let encoder = JSONEncoder()
        if options.pretty {
            encoder.outputFormatting = [.prettyPrinted]
            if #available(OSX 10.13, *) {
                encoder.outputFormatting.insert(.sortedKeys)
            }
        }
        guard let path = path else {
            return try encoder.encode(ErrorOutput(error: "No path calculated."))
        }
        return try encoder.encode(PathOutput(path: path))
    }
    
    private func verifyOutput() throws {
        if let outputPath = options.outputPath {
            if FileManager.default.fileExists(atPath: outputPath) {
                throw Error.usage(description: "Can't output critical path to \(outputPath) - file exists.")
            }
        }
    }
}

private struct ErrorOutput: Encodable {
    let error: String
}

private struct PathOutput: Encodable {
    let path: CriticalPath
}

extension BuildKey.Kind: Encodable {}
extension BuildKey: Encodable {
    enum CodingKeys: CodingKey {
        case kind, key
    }
    
    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(kind.description, forKey: .kind)
        try container.encode(key, forKey: .key)
    }
}

extension BuildValue.Kind: Encodable {}

extension BuildValue: Encodable {
    enum CodingKeys: CodingKey {
        case kind
    }
    
    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(kind.description, forKey: .kind)
    }
}

extension RuleResult: Encodable {
    enum CodingKeys: CodingKey {
        case signature, value, start, end, dependencies
    }
    
    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(signature, forKey: .signature)
        try container.encode(value, forKey: .value)
        try container.encode(start, forKey: .start)
        try container.encode(end, forKey: .end)
        try container.encode(dependencies, forKey: .dependencies)
    }
}

extension CriticalPath: Encodable {
    struct Element: Encodable {
        let key: BuildKey
        let result: RuleResult
    }
    
    enum CodingKeys: CodingKey {
        case cost, realisticCost, elements
    }
    
    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(cost, forKey: .cost)
        try container.encode(realisticCost, forKey: .realisticCost)
        try container.encode(map(Element.init), forKey: .elements)
    }
}
