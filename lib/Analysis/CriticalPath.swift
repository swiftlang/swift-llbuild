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

/// Critical Path is a single linked list containing the first task built and pointers to dependents
/// where the current element was the most expensive (long running) dependency of `next`.
/// It conforms to Sequence providing access to all `BuildKey`s and their results in order.
public final class CriticalPath: CustomStringConvertible {
    public enum Error: Swift.Error, Equatable, CustomStringConvertible {
        case missingDependency(self: CriticalPath, end: CriticalPath)
        
        public var description: String {
            switch self {
            case let .missingDependency(self, end):
                return "Can't append \(end) to \(self) since \(self.key) is not a dependency of \(end.key). Found dependencies: \(end.result.dependencies)."
            }
        }
    }
    
    /// The key that was built
    public let key: BuildKey
    /// The result containing the value, timing and dependency information
    public let result: RuleResult
    /// The cost of the whole path (sum up durations of all keys downstream)
    public let cost: Double
    /// The realistic cost of a critical path includes the waiting times between
    /// the individual tasks.
    public let realisticCost: Double
    /// Pointer to the next element where `key` was the most expensive dependency
    public let next: CriticalPath?
    /// We keep track of that to cheap calculate the realistic cost
    private let end: Double
    
    public init(key: BuildKey, result: RuleResult, next: CriticalPath?) {
        self.key = key
        self.result = result
        self.cost = result.duration + (next?.cost ?? 0)
        self.end = next?.end ?? result.end
        self.realisticCost = self.end - result.start
        self.next = next
    }
    
    /// Creates a new critical path by appending the provided path at the end.
    /// - Parameter end: the element in the path following `self`
    public func appending(end: CriticalPath?) throws -> CriticalPath {
        guard let end = end else { return self }
        if next == nil && !end.result.dependencies.contains(key) {
            throw Error.missingDependency(self: self, end: end)
        }
        return CriticalPath(key: key, result: result, next: try next?.appending(end: end) ?? end)
    }
    
    public var description: String {
        "Path (cost: \(String(format: "%.3f", cost))sec):\n" + map({ "\t\($0.key)" }).joined(separator: " → \n")
    }
}

extension CriticalPath {
    /// Constructs a critical path from a sequence of keys and results by chaining them in order.
    ///  **NOTE**: Throws an error if the following element have no dependency on the previous element.
    /// - Parameter keysAndResults: The built keys and their results in order of their dependency chain.
    public static func construct(_ keysAndResults: [(BuildKey, RuleResult)]) throws -> CriticalPath? {
        guard !keysAndResults.isEmpty else { return nil }
        var current: CriticalPath? = nil
        for (key, result) in keysAndResults {
            let new = CriticalPath(key: key, result: result, next: nil)
            current = try current?.appending(end: new) ?? new
        }
        return current
    }
}

extension CriticalPath: Equatable {
    public static func == (lhs: CriticalPath, rhs: CriticalPath) -> Bool {
        lhs.key == rhs.key && lhs.result == rhs.result && lhs.cost == rhs.cost && lhs.realisticCost == rhs.realisticCost && lhs.next == rhs.next
    }
}

/// CriticalPath conforms to Sequence, iterating the element from start to end.
/// Every following element has a dependency on the previous element.
extension CriticalPath: Sequence {
    public func makeIterator() -> AnyIterator<(key: BuildKey, result: RuleResult)> {
        var current: CriticalPath? = self
        return AnyIterator {
            defer { current = current?.next }
            return current.map { ($0.key, $0.result) }
        }
    }
}
