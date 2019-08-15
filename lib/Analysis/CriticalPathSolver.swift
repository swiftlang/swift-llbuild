// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

// The Swift package has llbuildSwift as module
#if SWIFT_PACKAGE
import llbuild
import llbuildSwift
#else
import llbuild
#endif

/// CriticalPathSolver offers an implementation of getting critical paths
/// from a build database.
public final class CriticalPathSolver {
    public enum Error: Swift.Error, Equatable, CustomStringConvertible {
        case noResultGiven(key: BuildKey)
        
        public var description: String {
            switch self {
            case .noResultGiven(let key):
                return "Failed to look up result for key \(key). Can't find critical path."
            }
        }
    }
    
    /// All keys that were part of the build
    public let keys: [BuildKey]
    /// The lookup's results will be cached and should return a result for all
    /// given keys
    private let ruleResultLookup: (BuildKey) throws -> RuleResult?
    /// Caches results from the rule result lookup
    private var resultCache: [BuildKey: RuleResult]
    /// Caches calculated critical paths per build key for fast lookup
    private var pathCache: [BuildKey: CriticalPath]
    
    /// Initializes the critical path solver with a database instance which is
    /// used to fetch keys and lookup rule results.
    public convenience init(database: BuildDB) throws {
        let keys = try database.getKeys()
        self.init(keys: Array(keys),
                  ruleResultLookup: database.lookupRuleResult(buildKey:))
    }
    
    /// Initializes the solver with keys and a result lookup. Ensure that the
    /// lookup returns a rule result for every given key.
    public init(keys: [BuildKey],
                ruleResultLookup: @escaping (BuildKey) throws -> RuleResult?) {
        self.keys = keys
        self.ruleResultLookup = ruleResultLookup
        self.resultCache = .init(minimumCapacity: keys.count)
        self.pathCache = .init(minimumCapacity: keys.count)
    }
    
    private func result(for key: BuildKey) throws -> RuleResult {
        if let cached = self.resultCache[key] { return cached }
        guard let lookup = try self.ruleResultLookup(key) else {
            throw Error.noResultGiven(key: key)
        }
        self.resultCache[key] = lookup
        return lookup
    }
    
    /// Returns the critical path for the given key.
    /// This method returns fast cached results if available and starts the
    /// calculation otherwise. This lookup is not thread safe.
    public func criticalPath(for key: BuildKey) throws -> CriticalPath {
        if let cached = self.pathCache[key] {
            return cached
        }
        
        func calculatePath(key: BuildKey, end: CriticalPath? = nil, cost: Double = 0) throws -> CriticalPath {
            if let cached = self.pathCache[key] {
                return try cached.appending(end: end)
            }
            let result = try self.result(for: key)
            let deps = Set(result.dependencies)
            let path = CriticalPath(key: key, result: result, next: end)
            if deps.isEmpty {
                self.pathCache[key] = path
                return path
            }
            
            let maxPath = try deps
                .map { try calculatePath(key: $0, end: path) }
                .max(by: { $0.cost < $1.cost })!
            self.pathCache[key] = maxPath
            return maxPath
        }
        
        return try calculatePath(key: key)
    }
    
    /// Calculates the critical paths for all given keys.
    public func calculateAllCriticalPaths() throws -> [BuildKey: CriticalPath] {
        let paths = try keys.map(self.criticalPath)
        return Dictionary(uniqueKeysWithValues: zip(keys, paths))
    }
    
    /// Calculates the most expensive critical path.
    /// If no keys are given, `nil` will be returned.
    public func calculateCriticalPath() throws -> CriticalPath? {
        let paths = try keys.map(self.criticalPath)
        return paths.max(by: { $0.cost < $1.cost })
    }
}
