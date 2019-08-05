// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

// This file contains Swift bindings for the llbuild C API.

#if os(macOS)
import Darwin.C
#elseif os(Windows)
import MSVCRT
import WinSDK
#else
import Glibc
#endif

import Foundation

// We don't need this import if we're building
// this file as part of the llbuild framework.
#if !LLBUILD_FRAMEWORK
import llbuild
#endif

public typealias KeyID = UInt64
public typealias KeyType = [UInt8]
public typealias ValueType = [UInt8]

/// Defines the result of a call to fetch all keys from the database
/// Wraps calls to the llbuild database, but all results are fetched and available with this result
public class BuildDBKeysResult {
    /// Opaque pointer to the actual result object
    private let result: OpaquePointer
    
    fileprivate init(result: OpaquePointer) {
        self.result = result
    }
    
    private lazy var _count: Int = Int(llb_database_result_keys_get_count(result))
    
    deinit {
        llb_database_destroy_result_keys(result)
    }
}

extension BuildDBKeysResult: Collection {
    public typealias Index = Int
    public typealias Element = BuildKey
    
    public var startIndex: Index {
        return 0
    }
    
    public var endIndex: Index {
        return self._count + startIndex
    }
    
    public subscript(index: Index) -> Iterator.Element {
        guard (startIndex..<endIndex).contains(index) else {
            fatalError("Index \(index) is out of bounds (\(startIndex)..<\(endIndex))")
        }
        return BuildKey.construct(key: llb_database_result_keys_get_key_at_index(self.result, Int32(index)))
    }
    
    public func index(after i: Index) -> Index {
        return i + 1
    }
}

extension BuildDBKeysResult: CustomReflectable {
    public var customMirror: Mirror {
        let keys = (startIndex..<endIndex).map { self[$0] }
        return Mirror(BuildDBKeysResult.self, unlabeledChildren: keys, displayStyle: .collection)
    }
}

extension BuildDBKeysResult: CustomStringConvertible {
    public var description: String {
        let keyDescriptions = (startIndex..<endIndex).map { "\(self[$0]))" }
        return "[\(keyDescriptions.joined(separator: ", "))]"
    }
}

/// Defines the result of a built task
public class RuleResult {
    /// Reference to the C API result instance (for desctruction)
    private let result: BuildDBResult
    /// The value of the result
    public let value: BuildValue
    /// Signature of the node that generated the result
    public let signature: UInt64
    /// The build iteration this result was computed at
    public let computedAt: UInt64
    /// The build iteration this result was built at
    public let builtAt: UInt64
    /// The start of the command as a duration since a reference time
    public let start: Double
    /// The duration since a reference time of when the command finished computing
    public let end: Double
    /// The duration in seconds the result needed to finish
    public var duration: Double { end - start }
    /// A list of the dependencies of the computed task, use the database's allKeys to check for their key
    public let dependencies: [BuildKey]
    
    fileprivate init?(result: BuildDBResult) {
        self.result = result
        guard let value = BuildValue.construct(from: Value(ValueType(UnsafeBufferPointer(start: result.value.data, count: Int(result.value.length))))) else {
            return nil
        }
        self.value = value
        self.signature = result.signature
        self.computedAt = result.computed_at
        self.builtAt = result.built_at
        self.start = result.start
        self.end = result.end
        self.dependencies = UnsafeBufferPointer(start: result.dependencies, count: Int(result.dependencies_count))
                                .map(BuildKey.construct(key:))
    }
    
    deinit {
        withUnsafePointer(to: result) { ptr in
            llb_database_destroy_result(ptr)
        }
    }
}

/// Private class for easier handling of out-parameters
private class MutableStringPointer {
    var ptr = llb_data_t()
    init() { }
    
    deinit {
        ptr.data?.deallocate()
    }
    
    var msg: String? {
        guard ptr.data != nil else { return nil }
        return stringFromData(ptr)
    }
}

/// Database object that defines a connection to a llbuild database
public final class BuildDB {
    
    /// Errors that can happen when opening the database or performing operations on it
    public enum Error: Swift.Error {
        /// If the system can't open the database, this error is thrown at init
        case couldNotOpenDB(error: String)
        /// If an operation on the database fails, this error is thrown
        case operationDidFail(error: String)
        /// If the database didn't provide an error but the operation still failed, the unknownError is thrown
        case unknownError
    }
    
    /// The opaque pointer to the database object
    private var _database: OpaquePointer
    
    /// Initializes the build database at a given path
    /// If the database at this path doesn't exist, it will created
    /// If the clientSchemaVersion is different to the one in the database at this path, its content will be automatically erased!
    public init(path: String, clientSchemaVersion: UInt32) throws {
        // Safety check that we have linked against a compatibile llbuild framework version
        if llb_get_api_version() != LLBUILD_C_API_VERSION {
            throw Error.couldNotOpenDB(error: "llbuild C API version mismatch, found \(llb_get_api_version()), expect \(LLBUILD_C_API_VERSION)")
        }
        
        // check if the database file exists
        var directory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: path, isDirectory: &directory) else {
            throw Error.couldNotOpenDB(error: "Database at path '\(path)' does not exist.")
        }
        if directory.boolValue {
            throw Error.couldNotOpenDB(error: "Path '\(path)' exists, but is a directory.")
        }
        
        let errorPtr = MutableStringPointer()
        guard let database = llb_database_open(strdup(path), clientSchemaVersion, &errorPtr.ptr) else {
            throw Error.couldNotOpenDB(error: errorPtr.msg ?? "Unknown error.")
        }
        
        _database = database
    }
    
    deinit {
        llb_database_destroy(_database)
    }
    
    /// Fetches all keys from the database
    public func getKeys() throws -> BuildDBKeysResult {
        let errorPtr = MutableStringPointer()
        let keys = UnsafeMutablePointer<OpaquePointer?>.allocate(capacity: 1)
        let success = llb_database_get_keys(_database, keys, &errorPtr.ptr)
        
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error: error)
        }
        if !success {
            throw Error.unknownError
        }
        
        guard let resultKeys = keys.pointee else {
            throw Error.unknownError
        }
        
        return BuildDBKeysResult(result: resultKeys)
    }
    
    /// Get the result for a given keyID
    public func lookupRuleResult(buildKey: BuildKey) throws -> RuleResult? {
        let errorPtr = MutableStringPointer()
        var result = BuildDBResult()
        
        let stored = llb_database_lookup_rule_result(_database, buildKey.internalBuildKey, &result, &errorPtr.ptr)
        
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error: error)
        }
        
        if !stored {
            return nil
        }
        
        return RuleResult(result: result)
    }
}
