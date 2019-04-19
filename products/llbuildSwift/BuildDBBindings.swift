//
//  BuildDBBindings.swift
//  llbuild-framework
//
//  Copyright © 2019 Apple Inc. All rights reserved.
//

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
public typealias KeyType = String

/// Defines the result of a call to fetch all keys from the database
/// Wraps calls to the llbuild database, but all results are fetched and available with this result
public class BuildDBKeysResult {
    /// Opaque pointer to the actual result object
    private let result: OpaquePointer?
    
    fileprivate init() {
        self.result = nil
    }
    
    fileprivate init(result: OpaquePointer) {
        self.result = result
    }
    
    private lazy var _count: Int = result.map { Int(llb_database_result_keys_get_count($0)) } ?? 0
    
    /// Get the key for a given key identifier (might be unique)
    public func getKey(at index: KeyID) -> KeyType {
        assert(result != nil, "Can't get key without fetching the data first.")
        
        var data = llb_data_t()
        withUnsafeMutablePointer(to: &data) { ptr in
            llb_database_result_keys_get_key_at_index(self.result, index, ptr)
        }
        defer { data.data?.deallocate() }
        return stringFromData(data)
    }
    
    deinit {
        llb_database_destroy_result_keys(result)
    }
}

extension BuildDBKeysResult: Collection {
    public typealias Index = KeyID
    public typealias Element = KeyType

    public var startIndex: Index {
        return 1 as UInt64
    }
    
    public var endIndex: Index {
        return UInt64(self._count) + startIndex
    }
    
    public subscript(index: Index) -> Iterator.Element {
        guard (startIndex..<endIndex).contains(index) else {
            fatalError("Index \(index) is out of bounds (\(startIndex)..<\(endIndex))")
        }
        return getKey(at: index)
    }
    
    public func index(after i: KeyID) -> KeyID {
        return i + 1
    }
}

/// Defines the result of a built task
public class RuleResult {
    /// Reference to the C API result instance (for desctruction)
    private var result: llb_database_result_t
    /// The value of the result
    public let value: Data
    /// Signature of the node that generated the result
    public let signature: UInt64
    /// The build iteration this result was computed at
    public let computedAt: UInt64
    /// The build iteration this result was built at
    public let builtAt: UInt64
    /// A list of the dependencies of the computed task, use the database's allKeys to check for their key
    public let dependencies: [KeyID]
    
    fileprivate init(result: llb_database_result_t) {
        self.result = result
        value = Data(bytes: result.value.data, count: Int(result.value.length))
        signature = result.signature
        computedAt = result.computed_at
        builtAt = result.built_at
        
        dependencies = UnsafeBufferPointer(start: result.dependencies, count: Int(result.dependencies_count)).map { KeyID($0) }
    }
    
    deinit {
        withUnsafeMutablePointer(to: &result) { ptr in
            llb_database_destroy_result(ptr)
        }
    }
    
    /// Fetch result information about all dependencies via the given database
    public func fetchDependencies(database: BuildDB) throws -> [RuleResult] {
        var result = [RuleResult]()
        
        for dep in dependencies {
            guard let res = try database.lookupRuleResult(keyID: dep) else {
                continue
            }
            result.append(res)
        }
        
        return result
    }
}

extension RuleResult: CustomReflectable {
    public var customMirror: Mirror {
        // We exclude the result pointer from the reflection
        let properties: DictionaryLiteral<String, Any> = [
            "signature": signature,
            "computedAt": computedAt,
            "builtAt": builtAt,
            "dependencies": dependencies,
            "value": value
        ]
        
        return Mirror(RuleResult.self, children: properties, displayStyle: .class, ancestorRepresentation: .generated)
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
        case couldNotOpenDB(String)
        /// If an operation on the database fails, this error is thrown
        case operationDidFail(String)
        /// If the database didn't provide an error but the operation still failed, the unknownError is thrown
        case unknownError
    }
    
    /// The opaque pointer to the database object
    private var _database: OpaquePointer? = nil
    
    /// Initializes the build database at a given path
    /// If the database at this path doesn't exist, it will created
    /// If the clientSchemaVersion is different to the one in the database at this path, its content will be automatically erased!
    public init(path: String, clientSchemaVersion: UInt32) throws {
        // Safety check that we have linked against a compatibile llbuild framework version
        if llb_get_api_version() != LLBUILD_C_API_VERSION {
            fatalError("llbuild C API version mismatch, found \(llb_get_api_version()), expect \(LLBUILD_C_API_VERSION)")
        }
        
        let errorPtr = MutableStringPointer()
        _database = llb_database_create(strdup(path), clientSchemaVersion, &errorPtr.ptr)
        
        if let error = errorPtr.msg {
            throw Error.couldNotOpenDB(error)
        }
        
        try buildStarted()
    }
    
    deinit {
        buildComplete()
        llb_database_destroy(_database)
    }
    
    /// Get the result for a given keyID
    public func lookupRuleResult(keyID: KeyID) throws -> RuleResult? {
        let errorPtr = MutableStringPointer()
        var result = llb_database_result_t()
        
        let stored = "ruleKey".withCString { ruleKey in
            return llb_database_lookup_rule_result(_database, keyID, &result, &errorPtr.ptr)
        }
        
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error)
        }
        
        if !stored {
            return nil
        }
        
        return RuleResult(result: result)
    }
    
    /// Fetches all keys from the database, returns it and saves if as \see allKeys on the database object
    public func getKeys() throws -> BuildDBKeysResult {
        let errorPtr = MutableStringPointer()
        
        let keys = UnsafeMutablePointer<OpaquePointer?>.allocate(capacity: 1)
        
        let success = llb_database_get_keys(_database, keys, &errorPtr.ptr)
        
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error)
        }
        if !success {
            throw Error.unknownError
        }
        
        guard let resultKeys = keys.pointee else {
            throw Error.unknownError
        }
        
        return BuildDBKeysResult(result: resultKeys)
    }
    
    /// Starts an exclusive build session
    private func buildStarted() throws {
        let errorPtr = MutableStringPointer()
        let success = llb_database_build_started(_database, &errorPtr.ptr)
        
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error)
        }
        if !success {
            throw Error.unknownError
        }
    }
    
    /// Ends a previously started exclusive build session
    private func buildComplete() {
        llb_database_build_complete(_database)
    }
    
    // MARK: - Private functions for wrapping C++ API
    
    static private func toDB(_ context: UnsafeMutableRawPointer) -> BuildDB {
        return Unmanaged<BuildDB>.fromOpaque(UnsafeRawPointer(context)).takeUnretainedValue()
    }
}
