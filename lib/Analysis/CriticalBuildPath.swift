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

/// Defines the most expensive dependency chain of a build executed by llbuild
/// based on the build database.
public struct CriticalBuildPath {
    /// An element in the chain consisting of the key being built and its result
    public struct Element {
        /// The built key
        public let key: BuildKey
        /// The build result of the built key
        public let result: RuleResult
    }
    /// The overall duration of the path, calculated as the sum of all element's durations
    public let duration: Double
    /// The elements of the chain in order.
    /// Each subsequent element has a dependency on its predecessor.
    public let elements: [Element]
    
    /// The solver is used for the actual calculation and offers access to the caches it builds up
    public final class Solver {
        /// Fast lookup for all keys and their result
        public let keyResultsLookup: IdentifierFactory<BuildDBKeysWithResult.Element>
        /// Fast lookup for all keys without result
        public let keyLookup: IdentifierFactory<BuildKey>
        /// The elements of the underlying, generic implementation of the algorithm
        private let pathElements: [CriticalPath.Element]
        
        /// Initializes a new CriticalBuildPath.Solver with a collection of keys and their results.
        /// This can get very expensive for huge collections as the fast lookups are being built up in here.
        public init<C>(keys: C) where C: Collection, C.Element == BuildDBKeysWithResult.Element {
            // Create infrastructure for mapping between path pointer and keys/results
            let keyResultsLookup = IdentifierFactory(keys)
            self.keyResultsLookup = keyResultsLookup
            let keyLookup = IdentifierFactory(keys.lazy.map({ $0.key }))
            self.keyLookup = keyLookup
            // Create the pointer elements for the algorithm
            self.pathElements = keys.map { element -> CriticalPath.Element in
                let identifier = keyResultsLookup.identifier(element: element)
                let result = element.result
                return .init(identifier: identifier,
                             weight: result.duration,
                             dependencies: result.dependencies.map(keyLookup.identifier(element:)))
            }
        }
        
        /// Executes the actual path solving algorithm and returns the most expensive path.
        public func run() -> CriticalBuildPath {
            let criticalPath = calculateCriticalPath(pathElements)
            // Map back from the pointer elements to the actual elements
            let elements = criticalPath.elements.map { element -> Element in
                let element = keyResultsLookup.element(id: element.identifier)
                return Element(key: element.key, result: element.result)
            }
            return CriticalBuildPath(duration: criticalPath.weight, elements: elements)
        }
    }
}

/// CriticalBuildPath allows iterating over all elements
extension CriticalBuildPath: Collection {
    public typealias Index = Array<Element>.Index
    
    public var startIndex: Index {
        return elements.startIndex
    }
    
    public var endIndex: Index {
        return elements.endIndex
    }
    
    public subscript(position: Index) -> Element {
        return elements[position]
    }
    
    public func index(after i: Index) -> Index {
        return elements.index(after: i)
    }
}

extension CriticalBuildPath.Element: CustomStringConvertible {
    public var description: String {
        return "<Element key=\(key) result=\(result)>"
    }
}

extension CriticalBuildPath: CustomStringConvertible {
    public var description: String {
        return "<CriticalBuildPath duration=\(duration)sec elements=[\n\(elements.map({ "\t" + $0.description }).joined(separator: "\n"))\n])>"
    }
}
