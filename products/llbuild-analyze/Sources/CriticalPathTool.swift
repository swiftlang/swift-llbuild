// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import TSCUtility
import TSCBasic
import llbuildAnalysis
import llbuildSwift

import struct Foundation.Data
import class Foundation.FileManager
import class Foundation.JSONEncoder

public final class CriticalPathTool: Tool<CriticalPathTool.Options> {
    
    public struct Options: Option {
        public enum OutputFormat: String, CaseIterable {
            case json, graphviz
        }
        
        var database: AbsolutePath!
        var outputPath: AbsolutePath?
        var clientSchemaVersion = 9
        var outputFormat: OutputFormat = .json
        var pretty = false
        var quiet = false
        
        public init() {}
    }
    
    public override class var toolName: String { return "critical-path" }
    
    public required init(args: [String]) throws {
        try super.init(
            usage: "Use this tool to find the critical path in a given build database.",
            overview: "The tool needs the path to the build database and is able to output the path in multiple different formats.",
            args: args)
    }
    
    override class func defineArguments(parser: ArgumentParser, binder: ArgumentBinder<CriticalPathTool.Options>) {
        
        binder.bind(
            positional: parser.add(positional: "database", usage: "Path to the build database"),
            to: { $0.database = AbsolutePath($1) })
        
        binder.bind(
            option: parser.add(option: "--output", shortName: "-o", usage: "Optional output path"),
            to: { $0.outputPath = AbsolutePath($1) })
        
        binder.bind(
            option: parser.add(option: "--clientSchemaVersion", shortName: "-version", usage: "The version of the client schema (default is 9)"),
            to: { $0.clientSchemaVersion = $1 })
        
        binder.bind(
            option: parser.add(option: "--outputFormat", shortName: "-f", usage: "The format of the output, default is json. Possible values: \(Options.OutputFormat.allCases.map({ $0.rawValue }))."),
            to: { $0.outputFormat = Options.OutputFormat(rawValue: $1) ?? .json })
        
        binder.bind(
            option: parser.add(option: "--pretty", usage: "If output format is json, the output will be pretty printed"),
            to: { $0.pretty = $1 })
        
        binder.bind(
            option: parser.add(option: "--quiet", shortName: "-q", usage: "There will be no output on stdout."),
            to: { $0.quiet = $1 })
        
    }
    
    public override func runImpl() throws {
        guard let databasePath = options.database else {
            throw ArgumentParserError.expectedValue(option: "database path")
        }
        
        let database = try BuildDB(path: databasePath.pathString, clientSchemaVersion: UInt32(options.clientSchemaVersion))
        let allKeysWithResult = try database.getKeysWithResult()
        let solver = CriticalBuildPath.Solver(keys: allKeysWithResult)
        let path = solver.run()
        
        // Output
        if let outputPath = options.outputPath {
            let data: Data
            switch options.outputFormat {
            case .json:
                data = try json(path, allKeyResults: allKeysWithResult, buildKeyLookup: solver.keyLookup)
            case .graphviz:
                data = graphViz(path, buildKeyLookup: solver.keyLookup)
            }
            try verifyOutputPath()
            FileManager.default.createFile(atPath: outputPath.pathString, contents: data)
        }
        
        if options.quiet { return }
        print(path.elements.isEmpty ? "Couldn't critical path from database at \(options.database.pathString) because no builds were build." : "Critical Path:\n\(path)")
    }
    
    public func json(_ path: CriticalBuildPath, allKeyResults: BuildDBKeysWithResult, buildKeyLookup: IdentifierFactory<BuildKey>) throws -> Data {
        let encoder = JSONEncoder()
        if options.pretty {
            encoder.outputFormatting = [.prettyPrinted]
            if #available(OSX 10.13, *) {
                encoder.outputFormatting.insert(.sortedKeys)
            }
        }
        return try encoder.encode(PathOutput(path, allKeyResults: allKeyResults, buildKeyLookup: buildKeyLookup))
    }
    
    private func verifyOutputPath() throws {
        guard let outputPath = options.outputPath else { return }
        if FileManager.default.fileExists(atPath: outputPath.pathString) {
            throw StringError("Can't output critical path to \(outputPath) - file exists.")
        }
    }
    
    private func graphViz(_ path: CriticalBuildPath, buildKeyLookup: IdentifierFactory<BuildKey>) -> Data {
        var result = "digraph G {\n\tedge [style=dotted]\n"
        
        func string(for key: BuildKey) -> String {
            // TODO: This is obviously just temporary - need a shorter version for visualization
            return buildKeyLookup.identifier(element: key).description
        }
        
        for element in path {
            for dep in element.result.dependencies {
                result += "\t\"\(string(for: dep))\" -> \"\(string(for: element.key))\"\n"
            }
        }
        
        result += "}"
        return result.data(using: .utf8) ?? Data()
    }
}

// MARK: - JSON Serialization
struct PathOutput: Encodable {
    enum CodingKeys: CodingKey {
        case path, mapping
    }
    
    let allKeyResults: BuildDBKeysWithResult
    let lookup: IdentifierFactory<BuildKey>
    let path: CriticalBuildPath
    
    init(_ path: CriticalBuildPath, allKeyResults: BuildDBKeysWithResult, buildKeyLookup: IdentifierFactory<BuildKey>) {
        self.path = path
        self.allKeyResults = allKeyResults
        self.lookup = buildKeyLookup
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(EncodableCriticalBuildPath(path, lookup: lookup), forKey: .path)
        try container.encode(EncodableMapping(allKeyResults, lookup: lookup), forKey: .mapping)
    }
}

struct EncodableMapping: Encodable {
    enum CodingKeys: CodingKey {
        case key, result
    }
    
    let allKeysWithResult: BuildDBKeysWithResult
    let lookup: IdentifierFactory<BuildKey>
    init(_ allKeysWithResult: BuildDBKeysWithResult, lookup: IdentifierFactory<BuildKey>) {
        self.allKeysWithResult = allKeysWithResult
        self.lookup = lookup
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.unkeyedContainer()
        for element in allKeysWithResult {
            var elementContainer = container.nestedContainer(keyedBy: CodingKeys.self)
            try elementContainer.encode(EncodableBuildKey(element.key), forKey: .key)
            try elementContainer.encode(EncodableRuleResult(element.result, lookup: lookup), forKey: .result)
        }
    }
}

struct EncodableCriticalBuildPath: Encodable {
    let elements: [EncodableCriticalBuildPathElement]
    init(_ path: CriticalBuildPath, lookup: IdentifierFactory<BuildKey>) {
        self.elements = path.elements.map({ EncodableCriticalBuildPathElement($0, lookup: lookup) })
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.unkeyedContainer()
        try container.encode(contentsOf: elements)
    }
}

struct EncodableCriticalBuildPathElement: Encodable {
    let element: CriticalBuildPath.Element
    let lookup: IdentifierFactory<BuildKey>
    init(_ element: CriticalBuildPath.Element, lookup: IdentifierFactory<BuildKey>) {
        self.element = element
        self.lookup = lookup
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(lookup.identifier(element: element.key))
    }
}

struct EncodableBuildKey: Encodable {
    enum CodingKeys: CodingKey {
        case kind, key
    }
    
    let key: BuildKey
    init(_ key: BuildKey) {
        self.key = key
    }
    
    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(key.kind.description, forKey: .kind)
        try container.encode(key.key, forKey: .key)
    }
}

struct EncodableBuildValueKind: Encodable {
    let kind: BuildValue.Kind
    init(_ kind: BuildValue.Kind) {
        self.kind = kind
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(kind.description)
    }
}

struct EncodableBuildValue: Encodable {
    enum CodingKeys: CodingKey {
        case kind
    }
    
    let value: BuildValue
    init(_ value: BuildValue) {
        self.value = value
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(EncodableBuildValueKind(value.kind), forKey: .kind)
    }
}

struct EncodableRuleResult: Encodable {
    enum CodingKeys: CodingKey {
        case value, signature, computedAt, builtAt, duration, dependencies
    }
    
    let result: RuleResult
    let lookup: IdentifierFactory<BuildKey>
    init(_ result: RuleResult, lookup: IdentifierFactory<BuildKey>) {
        self.result = result
        self.lookup = lookup
    }
    
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(EncodableBuildValue(result.value), forKey: .value)
        try container.encode(result.signature, forKey: .signature)
        try container.encode(result.computedAt, forKey: .computedAt)
        try container.encode(result.builtAt, forKey: .builtAt)
        try container.encode(result.duration, forKey: .duration)
        let deps = result.dependencies.map(lookup.identifier(element:))
        try container.encode(deps, forKey: .dependencies)
    }
}
