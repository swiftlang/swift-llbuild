// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

/// CriticalPath is a structure that represents the dependency chain of elements which was the most expensive in a process (e.g. build).
public struct CriticalPath: Equatable {
    /// CriticalPath.Element defines a node in the weighted graph that should be used to calculate the critical path
    public struct Element: Equatable {
        public typealias Identifier = Array<Element>.Index
        /// The graph-wide unique index for the element
        let identifier: Identifier
        /// The weight of the node
        var weight: Double
        /// An Array of all dependencies of the node identified by their identifier
        var dependencies: [Identifier]
        
        /// Initializes a new instance of an Element
        /// - Parameter identifier: graph-wide unique identifier
        /// - Parameter weight: weight of the node
        /// - Parameter dependencies: dependencies identified by their unique identifiers
        public init(identifier: Identifier, weight: Double, dependencies: [Identifier]) {
            self.identifier = identifier
            self.weight = weight
            self.dependencies = dependencies
        }
        
        public static func == (lhs: Element, rhs: Element) -> Bool {
            return lhs.identifier == rhs.identifier
        }
    }
    
    public enum Error: Swift.Error, Equatable, CustomStringConvertible {
        case missingDependency(previous: Element, following: Element)
        
        public var description: String {
            switch self {
            case let .missingDependency(previous, following):
                return #"Can't create a connection in the critical build path between element "\#(previous.identifier)" and "\#(following.identifier)" because "\#(following.identifier)" doesn't have a dependency on "\#(previous.identifier)". Found dependencies are \#(following.dependencies)."#
            }
        }
    }
    
    /// The cost of the whole path (sum of the individual costs)
    public let weight: Double
    /// The elements in order where the following has a dependency on the previous
    public let elements: [Element]
    
    /// Creates a critical path instance by calculating the cost and checking for dependencies between the elements.
    /// If an element has a predecessor which isn't a dependency this will throw an error.
    /// - Parameter elements: All elements in correct order in the dependency chain.
    public init(_ elements: [Element]) throws {
        var cost: Double = 0
        var current: Element?
        for element in elements {
            cost += element.weight
            if let cur = current {
                guard element.dependencies.contains(cur.identifier) else {
                    throw Error.missingDependency(previous: cur, following: element)
                }
            }
            current = element
        }
        
        self.init(weight: cost, path: elements)
    }
    
    /// Initializes a path with one element.
    /// - Parameter element: The only element in the path.
    public init(_ element: Element) {
        self.init(weight: element.weight, path: [element])
    }
    
    /// This initializer doesn't check dependencies between the elements and is therefore private for fast use.
    /// - Parameter weight: The weight of all elements
    /// - Parameter path: The elements in order where the following element should have a dependency on the previous one.
    private init(weight: Double, path: [Element]) {
        self.weight = weight
        self.elements = path
    }
    
    /// The empty critical path doesn't have elements and cost nothing.
    public static func empty() -> CriticalPath {
        return CriticalPath(weight: 0, path: [])
    }
    
    /// Returns a path where `other` is appended to `self`.
    /// - Parameter other: The path that should be connected to `self`. It's first element's dependencies should contain the last element of self if none are empty.
    fileprivate func appending(_ other: CriticalPath) -> CriticalPath {
        return CriticalPath(weight: weight + other.weight, path: elements + other.elements)
    }
}

/// Calculates the critical path for a given collection of elements.
/// The size of the collection needs to be higher than all identifiers of the given elements.
/// - Parameter elements: All elements in the graph.
public func calculateCriticalPath<C: Collection>(_ elements: C) -> CriticalPath where C.Element == CriticalPath.Element, C.Index == CriticalPath.Element.Identifier {
    // An empty graph has the empty critical path as critical path
    if elements.isEmpty { return .empty() }
    // This cache uses the identifier as array index to cache calculated paths
    var cache = [CriticalPath?](repeating: nil, count: elements.count)
    
    func generatePath(for element: CriticalPath.Element, tail: CriticalPath = .empty()) -> CriticalPath {
        if let cached = cache[element.identifier] { return cached.appending(tail) }
        let newPath = CriticalPath(element)
        
        let maxPath = element.dependencies
            .map { generatePath(for: elements[$0], tail: newPath) }
            .max(by: { $0.weight < $1.weight }) ?? newPath
        cache[element.identifier] = maxPath
        return maxPath
    }
    
    return elements
        .map { generatePath(for: $0) }
        .max(by: { $0.weight < $1.weight }) ?? .empty()
}
