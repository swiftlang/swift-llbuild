// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import NIO

import CevoCore
import Ninja

public typealias Command = Ninja.Command

public protocol NinjaValue: Value {}

public class NinjaBuild {
    let manifest: NinjaManifest
    let delegate: NinjaBuildDelegate

    public enum Error: Swift.Error {
        case internalTypeError
    }

    public init(manifest: String, delegate: NinjaBuildDelegate) throws {
        self.manifest = try NinjaManifest(path: manifest)
        self.delegate = delegate
    }

    public func build<V: NinjaValue>(target: String, as: V.Type) throws -> V {
        let engineDelegate = NinjaEngineDelegate(manifest: manifest, delegate: delegate)
        let engine = Engine(delegate: engineDelegate)
        return try engine.build(key: "T" + target, as: V.self).wait()
    }
}

public protocol NinjaBuildDelegate {
    /// Build the given Ninja input.
    ///
    /// This will only be called when all inputs are available.
    func build(group: EventLoopGroup, path: String) -> EventLoopFuture<NinjaValue>
    
    /// Build the given Ninja command.
    ///
    /// This will only be called when all inputs are available.
    func build(group: EventLoopGroup, command: Command, inputs: [NinjaValue]) -> EventLoopFuture<NinjaValue>
}

private extension EventLoopFuture where Value == CevoCore.Value {
    func asNinjaValue() -> EventLoopFuture<NinjaValue> {
        return self.flatMapThrowing { value in
            guard let ninjaValue = value as? NinjaValue else {
                throw NinjaBuild.Error.internalTypeError
            }

            return ninjaValue
        }
    }
}

enum NinjaEngineDelegateError: Error {
    case unexpectedKey(String)
    case unexpectedKeyType(String)
    case unexpectedCommandKey(String)
    case invalidKey(String)
    case commandNotFound(String)
}

private class NinjaEngineDelegate: EngineDelegate {
    let manifest: NinjaManifest
    let commandMap: [String: Int]
    let delegate: NinjaBuildDelegate

    init(manifest: NinjaManifest, delegate: NinjaBuildDelegate) {
        self.manifest = manifest
        self.delegate = delegate

        // Populate the command map.
        var commandMap = [String: Int]()
        for (i,command) in self.manifest.commands.enumerated() {
            for output in command.outputs {
                commandMap[output] = i
            }
        }
        self.commandMap = commandMap
    }
    
    func lookupFunction(forKey rawKey: Key, group: EventLoopGroup) -> EventLoopFuture<Function> {
        guard let key = rawKey as? String else {
            return group.next().makeFailedFuture(
                NinjaEngineDelegateError.unexpectedKeyType(String(describing: type(of: rawKey)))
            )
        }

        guard let code = key.first else {
            return group.next().makeFailedFuture(NinjaEngineDelegateError.invalidKey(key))
        }

        switch code {
            // A top-level target build request (expected to always be a valid target).
        case "T":
            // Must be a target.
            let target = String(key.dropFirst(1))
            guard let i = self.commandMap[target] else {
                return group.next().makeFailedFuture(NinjaEngineDelegateError.commandNotFound(target))
            }

            return group.next().makeSucceededFuture(
                SimpleFunction { (fi, key) in
                    return fi.request("C" + String(i))
                }
            )

            // A build node.
        case "N":
            // If this is a command output, build the command (note that there
            // must be a level of indirection here, because the same command may
            // produce multiple outputs).
            let path = String(key.dropFirst(1))
            if let i = self.commandMap[path] {
                return group.next().makeSucceededFuture(
                    SimpleFunction { (fi, key) in
                        return fi.request("C" + String(i))
                    }
                )
            }

            // Otherwise, it is an input file.
            return group.next().makeSucceededFuture(
                SimpleFunction { (fi, key) in
                    return self.delegate.build(group: fi.group, path: path).map { $0 as Value }
                }
            )

            // A build command.
        case "C":
            let commandIndexStr = String(key.dropFirst(1))
            guard let i = Int(commandIndexStr) else {
                return group.next().makeFailedFuture(NinjaEngineDelegateError.unexpectedCommandKey(key))
            }

            return group.next().makeSucceededFuture(
                SimpleFunction { (fi, key) in
                    // Get the command.
                    let command = self.manifest.commands[i]
                    // FIXME: For now, we just merge all the inputs. This isn't
                    // really in keeping with the Ninja semantics, but is strong.
                    var inputs = command.inputs.map{ fi.request("N" + $0).asNinjaValue() }
                    inputs += command.implicitInputs.map{ fi.request("N" + $0).asNinjaValue() }
                    inputs += command.orderOnlyInputs.map{ fi.request("N" + $0).asNinjaValue() }
                    return EventLoopFuture.whenAllSucceed(inputs, on: fi.group.next()).flatMap { inputs in
                        return self.delegate.build(group: fi.group.next(), command: command, inputs: inputs).map { $0 as Value }
                    }
                }
            )

        default:
            return group.next().makeFailedFuture(NinjaEngineDelegateError.unexpectedKey(key))
        }
    }
}
