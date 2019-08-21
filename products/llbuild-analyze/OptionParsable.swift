// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

/// A type that conforms to `OptionParsable` is able to construct an instance from a raw String.
public protocol OptionParsable {
  init(raw: String) throws
}

extension Optional: OptionParsable where Wrapped: OptionParsable {
  public init(raw: String) throws {
    self = try? Wrapped(raw: raw)
  }
}

extension Int: OptionParsable {
  public init(raw: String) throws {
    guard let value = Int(raw) else { throw ParseError.cantParse(description: #"Can't construct integer value from "\#(raw)"."#) }
    self = value
  }
}

extension String: OptionParsable {
  public init(raw: String) throws {
    self = raw
  }
}

extension Bool: OptionParsable {
  public init(raw: String) throws {
    if raw.isEmpty { self = false; return }
    if let number = Int(raw) { self = number > 0; return }
    self = ["true", "yes"].contains(where: { $0 == raw.lowercased() })
  }
}

extension RawRepresentable where Self: CaseIterable, RawValue: OptionParsable {
  public init(raw: String) throws {
    let rawValue = try RawValue(raw: raw)
    guard let value = Self.init(rawValue: rawValue) else {
      throw ParseError.cantParse(description: "Unknown raw value '\(raw)' for \(Self.self). Possible values: [\(Self.allCases.map({ "\($0.rawValue)" }).joined(separator: ", "))].")
    }
    self = value
  }
}
