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
import class Foundation.ListFormatter
import class Foundation.FileManager
import class Foundation.JSONEncoder

public final class CriticalPathTool: Tool<CriticalPathTool.Options> {
    
    public struct Options: Option {
        public enum OutputFormat: String, CaseIterable, CommandLineArgumentChoices {
            case json, graphviz
        }

        public enum GraphvizDisplay: String, CaseIterable, CommandLineArgumentChoices {
            case criticalPath
            case everything
        }
        
        var database: AbsolutePath!
        var outputPath: AbsolutePath?
        var clientSchemaVersion = 9
        var outputFormat: OutputFormat = .json
        var graphvizDisplay: GraphvizDisplay = .criticalPath
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
            option: parser.add(option: "--outputFormat", shortName: "-f", usage: "The format of the output, default is json. Possible values: \(Options.OutputFormat.helpDescription)."),
            to: { $0.outputFormat = Options.OutputFormat(rawValue: $1) ?? .json })
        
        binder.bind(
            option: parser.add(option: "--pretty", usage: "If output format is json, the output will be pretty printed"),
            to: { $0.pretty = $1 })

        binder.bind(
            option: parser.add(option: "--graphvizOutput", usage: "Graphviz display options. Possible values: \(Options.GraphvizDisplay.helpDescription)."),
            to: { $0.graphvizDisplay = Options.GraphvizDisplay(rawValue: $1) ?? .everything }
        )
        
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

        var edges = Set<DirectedEdge>()

        if var last = path.first {
            for item in path[1..<path.endIndex] {
                edges.insert(DirectedEdge(a: last.key, b: item.key, isCritical: true))
                last = item
            }
        }

        if options.graphvizDisplay == .everything {
            for element in path {
                for dep in element.result.dependencies {
                    edges.insert(DirectedEdge(a: dep, b: element.key))
                }
            }
        }

        result += edges.map{ $0.graphVizString }.joined()
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


protocol GraphVizNode {
    var graphVizName: String { get }
}

extension BuildKey.Command: GraphVizNode {
    var graphVizName: String {
        "Command:\(self.name)"
    }
}

extension BuildKey.CustomTask: GraphVizNode {
    var graphVizName: String {
        "CustomTask:\(self.name)"
    }
}

extension BuildKey.DirectoryContents: GraphVizNode {
    var graphVizName: String {
        "DirectoryContents:\(self.path)"
    }
}

extension BuildKey.FilteredDirectoryContents: GraphVizNode {
    var graphVizName: String {
        "FilteredDirectoryContents:\(self.path)"
    }
}

extension BuildKey.DirectoryTreeSignature: GraphVizNode {
    var graphVizName: String {
        "DirectoryTreeSignature:\(self.path)"
    }
}

extension BuildKey.DirectoryTreeStructureSignature: GraphVizNode {
    var graphVizName: String {
        "DirectoryTreeStructureSignature:\(self.path)"
    }
}

extension BuildKey.Node: GraphVizNode {
    var graphVizName: String {
        "Node:\(self.path)"
    }
}

extension BuildKey.Stat: GraphVizNode {
    var graphVizName: String {
        "Stat:\(self.path)"
    }
}

extension BuildKey.Target: GraphVizNode {
    var graphVizName: String {
        "Target:\(self.name)"
    }
}

/// Struct to represent a directed edge in GraphViz from a -> b.
/// `hash()` and `==` only take the both edges into account, not
/// `isCritical`, so the graph can be represented as a `Set<DirectedEdge>`
/// and gurantee that there is only one edge between two verticies.
struct DirectedEdge: Hashable, Equatable {
    /// Source `BuildKey`
    let a: BuildKey

    /// Destination `BuildKey`
    let b: BuildKey

    /// Flag if the edge is on critical build path.
    var isCritical: Bool = false

    static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.a == rhs.a && lhs.b == rhs.b
    }

    func hash(into hasher: inout Hasher) {
        a.hash(into: &hasher)
        b.hash(into: &hasher)
    }

    /// Style attributes for the edge.
    private var style: String {
        if isCritical {
            return "[style=bold]"
        }
        return ""
    }

    /// GraphViz representation of the Edge.
    var graphVizString: String {
        guard let a = a as? GraphVizNode, let b = b as? GraphVizNode else {
            fatalError("Both edges need to conform to GraphvizNode")
        }
        return "\t\"\(a.graphVizName)\" -> \"\(b.graphVizName)\"\(style)\n"
    }

}

private protocol CommandLineArgumentChoices: CaseIterable, RawRepresentable where RawValue == String {
    static var helpDescription: String { get }
}

private extension CommandLineArgumentChoices {
    static var helpDescription: String {
        let strings = Self.allCases.map { "\"\($0.rawValue)\"" }
        if #available(OSX 10.15, *) {
            let formatter = ListFormatter()
            return formatter.string(for: strings)!
        } else {
            return strings.joined(separator: ",")
        }
    }
}
