// This source file is part of the Swift.org open source project
//
// Copyright 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

// This file contains Swift bindings for the llbuild C API.

import Foundation

#if !LLBUILD_FRAMEWORK
import llbuild
#endif

public enum NinjaError: Error {
  case invalidManifest(errors: String)
}

public struct NinjaRule: Codable, Equatable, Sendable {
  public let name: String
  public let variables: [String: String]

  public init(name: String, variables: [String: String]) {
    self.name = name
    self.variables = variables
  }
}

public struct NinjaBuildStatement: Codable, Equatable, Sendable {
  public let rule: NinjaRule
  public let command: String
  public let description: String
  public var allInputs: [String] {
    return explicitInputs + implicitInputs + orderOnlyInputs
  }
  public let explicitInputs: [String]
  public let implicitInputs: [String]
  public let orderOnlyInputs: [String]
  public let outputs: [String]
  public let variables: [String: String]
  public let generator: Bool
  public let restat: Bool

  public init(rule: NinjaRule, command: String,
              description: String, explicitInputs: [String],
              implicitInputs: [String], orderOnlyInputs: [String],
              outputs: [String], variables: [String: String],
              generator: Bool, restat: Bool) {
    self.rule = rule
    self.command = command
    self.description = description
    self.explicitInputs = explicitInputs
    self.implicitInputs = implicitInputs
    self.orderOnlyInputs = orderOnlyInputs
    self.outputs = outputs
    self.variables = variables
    self.generator = generator
    self.restat = restat
  }
}

public struct NinjaManifest: Codable, Equatable, Sendable {
  public let rules: [String: NinjaRule]
  public let statements: [NinjaBuildStatement]
  public let defaultTargets: [String]

  public init(rules: [String: NinjaRule], statements: [NinjaBuildStatement],
              defaultTargets: [String]) {
    self.rules = rules
    self.statements = statements
    self.defaultTargets = defaultTargets
  }
}

extension NinjaManifest {
  public init(path: String, workingDirectory: String) throws {
    let (manifest, errors) = Self.createNonThrowing(
      path: path, workingDirectory: workingDirectory)

    if let errors = errors {
      throw NinjaError.invalidManifest(errors: errors)
    }

    self = manifest
  }

  public static func createNonThrowing(path: String, workingDirectory: String) -> (NinjaManifest, String?) {
    var cManifest = llb_manifest_fs_load(path, workingDirectory)
    defer {
      llb_manifest_destroy(&cManifest)
    }

    var rules = [String: NinjaRule]()
    let statements: [NinjaBuildStatement] = makeArray(
      cArray: cManifest.statements,
      count: cManifest.num_statements) { raw in
      let rule: NinjaRule

      let rawRule = raw.rule.pointee
      let ruleName = ownedString(rawRule.name)
      let foundRule = rules[ruleName]
      if let foundRule = foundRule {
        rule = foundRule
      } else {
        rule = NinjaRule(
          name: ruleName,
          variables: makeMap(
            cArray: rawRule.variables,
            count: rawRule.num_variables,
            transform: ownedVar))
        rules[ruleName] = rule
      }

      return NinjaBuildStatement(
        rule: rule,
        command: ownedString(raw.command),
        description: ownedString(raw.description),
        explicitInputs: makeArray(cArray: raw.explicit_inputs,
                                  count: raw.num_explicit_inputs,
                                  transform: ownedString),
        implicitInputs: makeArray(cArray: raw.implicit_inputs,
                                  count: raw.num_implicit_inputs,
                                  transform: ownedString),
        orderOnlyInputs: makeArray(cArray: raw.order_only_inputs,
                                   count: raw.num_order_only_inputs,
                                   transform: ownedString),
        outputs: makeArray(cArray: raw.outputs, count: raw.num_outputs,
                           transform: ownedString),
        variables: makeMap(cArray: raw.variables, count: raw.num_variables,
                           transform: ownedVar),
        generator: raw.generator,
        restat: raw.restat)
    }
    let defaultTargets = makeArray(cArray: cManifest.default_targets,
                                   count: cManifest.num_default_targets,
                                   transform: ownedString)

    let error: String?
    if cManifest.error.length > 0 {
      error = ownedString(cManifest.error)
    } else {
      error = nil
    }

    return (NinjaManifest(rules: rules, statements: statements,
                          defaultTargets: defaultTargets), error)
  }
}

private func ownedString(_ ref: CStringRef) -> String {
  return String(data: Data(bytes: ref.data, count: Int(ref.length)),
                encoding: .utf8)!
}

private func ownedVar(_ ref: CNinjaVariable) -> (String, String) {
  return (ownedString(ref.key), ownedString(ref.value))
}

private func makeArray<T, R>(cArray: UnsafePointer<T>, count: UInt64,
                             transform: (T) -> R) -> [R] {
  return UnsafeBufferPointer(start: cArray, count: Int(count)).map(transform)
}

private func makeMap<T, K, V>(cArray: UnsafePointer<T>, count: UInt64,
                              transform: @escaping (T) -> (K, V)) -> [K: V] {
  return Dictionary(
    UnsafeBufferPointer(start: cArray, count: Int(count))
      .lazy.map { transform($0) }, uniquingKeysWith: { first, _ in first })
}
