//===- Label.swift --------------------------------------------*- Swift -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020-2024 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

import Foundation

public enum TLabelError: Error {
  /// Error for finding invalid characters in the label.
  case invalidCharacters(String)

  /// Error for finding an unexpected character in label parsing.
  case unexpectedCharacter(Character)

  /// Error for an invalid prefix for the label.
  case unexpectedPrefix(String)

  /// Error for a label that has an invalid suffix.
  case unexpectedSuffix(String)

  /// Error for when a label is invalid.
  case invalidLabel(String)
}

extension TLabel {
  // Character set used in label scanning.
  public static let colonCharacterSet = CharacterSet(charactersIn: ":")
  public static let slashCharacterSet = CharacterSet(charactersIn: "/")

  /// Characters disallowed in label components.
  public static let invalidCharacters = CharacterSet(
    charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890:/! \\\"#$%&'()*+,-.;<=>?@[]^_`{|}"
  ).inverted

  public init(_ string: String) throws {
    let stringScanner = Scanner(string: string)

    // If there are any invalid characters error out.
    if let range = string.rangeOfCharacter(from: TLabel.invalidCharacters) {
      throw TLabelError.invalidCharacters("Invalid characters in range: \(String(describing: range))")
    }

    // Read exactly 2 `/` characters from the beginning.
    guard let separator = stringScanner.scanCharacters(from: TLabel.slashCharacterSet), separator.count == 2 else {
      throw TLabelError.unexpectedPrefix(string)
    }

    let unionCharacterSet = TLabel.colonCharacterSet.union(TLabel.slashCharacterSet)
    var pathComponents = [String]()

    // Read components until we find either `:` or `/`.
    while true {
      if let pathComponent = stringScanner.scanUpToCharacters(from: unionCharacterSet) {
        pathComponents.append(pathComponent)
      }

      if let nextCharacter = stringScanner.scanCharacter() {
        if nextCharacter == ":" {
          break
        } else if nextCharacter == "/" {
          continue
        } else {
          throw TLabelError.unexpectedCharacter(nextCharacter)
        }
      } else {
        break
      }
    }

    self.components = pathComponents

    // Read until we find another separator character. If there is more, it's the target name, if not, use the
    // last path component as the target name as a short-cut feature.
    if let targetName = stringScanner.scanUpToCharacters(from: unionCharacterSet) {
      self.name = targetName
    } else {
      guard let lastPathComponent = pathComponents.last else {
        throw TLabelError.invalidLabel(string)
      }
      self.name = lastPathComponent
    }

    // Make sure we've read all of the input, if not, the label was invalid
    if !stringScanner.isAtEnd {
      throw TLabelError.unexpectedSuffix(string)
    }
  }

  public func startsWith(_ str: String) -> Bool {
    guard let lbl = try? TLabel(str) else {
      return false
    }
    return startsWith(lbl)
  }

  public func startsWith(_ lbl: TLabel) -> Bool {
    guard self.components.count >= lbl.components.count else {
      return false
    }

    for (c1, c2) in zip(self.components, lbl.components) {
      if (c1 != c2) {
        return false
      }
    }

    return true
  }

  public var canonical: String {
    "//\(components.joined(separator: "/")):\(name)"
  }

}
