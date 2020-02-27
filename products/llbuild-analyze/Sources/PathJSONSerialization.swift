// This source file is part of the Swift.org open source project
//
// Copyright 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors


import llbuildAnalysis
import llbuildSwift

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
