// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

/// IdentifierFactory creates unique identifiers for given elements.
/// It provides an API to map between identifiers and elements and can be used
/// for fast lookups.
public final class IdentifierFactory<T: Hashable> {
  public typealias Identifier = Array<T>.Index
  public let elements: [T]
  private var identifiers: [T: Identifier]
  
  /// Initializes a new factory using a collection of elements.
  /// - Parameter elements: Contains all elements which will become valid input for calling `identifier(element:)`.
  public init<C>(_ elements: C) where C: Collection, C.Element == T {
    self.elements = Array(elements)
    var identifierLookup = [T: Identifier](minimumCapacity: self.elements.count)
    for (index, element) in elements.enumerated() {
      identifierLookup[element] = index
    }
    self.identifiers = identifierLookup
  }
  
  public var count: Int {
    return elements.count
  }
  
  /// Returns the element for a provided identifier.
  /// - Parameter id: An identifier created using `identifier(element:)`
  public func element(id: Identifier) -> T {
    return elements[id]
  }
  
  /// Returns a unique identifier for a given element.
  /// - Parameter element: The element must have been part of the collection used in the initialization.
  public func identifier(element: T) -> Identifier {
    guard let identifier = self.identifiers[element] else {
      preconditionFailure("Could not get identifier for \(element) because it was not initially added to the IdentifierFactory.")
    }
    return identifier
  }
}
