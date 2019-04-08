//
//  BuildDBBindings.swift
//  llbuild-framework
//
//  Created by Benjamin Herzog on 4/5/19.
//  Copyright © 2019 Apple Inc. All rights reserved.
//

#if os(Linux)
import Glibc
#elseif os(Windows)
import MSVCRT
import WinSDK
#else
import Darwin.C
#endif

import Foundation

// We don't need this import if we're building
// this file as part of the llbuild framework.
#if !LLBUILD_FRAMEWORK
import llbuild
#endif

public typealias KeyID = UInt64
public typealias KeyType = String

public class BuildDBFetchResult: Sequence {
    private var result: OpaquePointer? = nil
    
    public typealias Element = (id: KeyID, key: KeyType)
    
    fileprivate init(result: OpaquePointer) {
        self.result = result
    }
    
    public var count: Int {
        return Int(llb_database_result_keys_get_count(result))
    }
    
    public func id(key: KeyType) -> KeyID {
        return llb_database_result_keys_get_id_for_key(result, key.cString(using: .utf8))
    }
    
    public func key(id: KeyID) -> KeyType {
        return String(cString: llb_database_result_keys_get_key_for_id(result, id))
    }
    
    deinit {
        llb_database_destroy_result_keys(result)
    }
    
    public __consuming func makeIterator() -> AnyIterator<Element> {
        var index: UInt64 = 0
        let count = self.count
        return AnyIterator {
            if index >= count { return nil }
            defer { index += 1 }
            return (index, self.key(id: index))
        }
    }
}

public struct RuleResult {
    let value: Data
    let signature: UInt64
    let computedAt: UInt64
    let builtAt: UInt64
    let dependencies: [KeyID]
    
    fileprivate init(result: llb_database_result_t) {
        
        value = Data(bytes: UnsafeRawPointer(result.value.data), count: Int(result.value.length))
        signature = result.signature
        computedAt = result.computed_at
        builtAt = result.built_at
        
        dependencies = UnsafeBufferPointer(start: result.dependencies, count: Int(result.dependencies_count)).map { KeyID($0) }
    }
}

public protocol BuildDBDelegate {
    func getKeyID(key: KeyType) -> KeyID
    func getKey(id: KeyID) -> KeyType
}

fileprivate class StringPointer {
    let ptr: UnsafeMutablePointer<UnsafeMutablePointer<Int8>?>
    init() {
        ptr = .allocate(capacity: 1)
    }
    
    deinit {
        ptr.deallocate()
    }
    
    var msg: String? {
        if let errorPointee = ptr.pointee {
            defer { errorPointee.deallocate() }
            return String(cString: errorPointee)
        }
        return nil
    }
}

public class BuildDB {
    
    public enum Error: Swift.Error {
        case couldNotOpenDB(String)
        case operationDidFail(String)
        case unknownError
    }
    
    public let delegate: BuildDBDelegate
    private var _database: OpaquePointer? = nil
    
    public init(path: String, clientSchemaVersion: UInt32, delegate: BuildDBDelegate) throws {
        // Safety check that we have linked against a compatibile llbuild framework version
        if llb_get_api_version() != LLBUILD_C_API_VERSION {
            fatalError("llbuild C API version mismatch, found \(llb_get_api_version()), expect \(LLBUILD_C_API_VERSION)")
        }
        
        self.delegate = delegate
        
        var _delegate = llb_database_delegate_t()
        _delegate.context = Unmanaged.passUnretained(self).toOpaque()
        _delegate.get_key_for_id = { BuildDB.toDB($0!).get_key_for_id($1) }
        _delegate.get_key_id = { BuildDB.toDB($0!).get_key_id($1!) }
        
        let errorPtr = StringPointer()
        _database = llb_database_create(strdup(path), clientSchemaVersion, _delegate, errorPtr.ptr)
        if let error = errorPtr.msg {
            throw Error.couldNotOpenDB(error)
        }
        
    }
    
    deinit {
        llb_database_destroy(_database)
    }
    
    public func getCurrentIteration() throws -> UInt64 {
        var success = false
        
        let errorPtr = StringPointer()
        let iteration = llb_database_get_current_iteration(_database, &success, errorPtr.ptr)
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error)
        }
        if !success {
            throw Error.unknownError
        }
        return iteration
    }
    
    public func setCurrentIteration(_ iteration: UInt64) throws {
        let errorPtr = StringPointer()
        llb_database_set_current_iteration(_database, iteration, errorPtr.ptr)
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error)
        }
    }
    
    public func lookupRuleResult(keyID: KeyID, ruleKey: KeyType) throws -> RuleResult? {
        let errorPtr = StringPointer()
        var result = llb_database_result_t()
        
        let stored = ruleKey.withCString { ruleKey in
            return llb_database_lookup_rule_result(_database, keyID, ruleKey, &result, errorPtr.ptr)
        }
        
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error)
        }
        
        if !stored {
            return nil
        }
        
        return RuleResult(result: result)
    }
    
    public func buildStarted() throws {
        let errorPtr = StringPointer()
        let success = llb_database_build_started(_database, errorPtr.ptr)
        
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error)
        }
        if !success {
            throw Error.unknownError
        }
    }
    
    public func buildComplete() {
        llb_database_build_complete(_database)
    }
    
    public func getKeys() throws -> BuildDBFetchResult {
        let errorPtr = StringPointer()
        
        let keys = UnsafeMutablePointer<OpaquePointer?>.allocate(capacity: 1)
        
        let success = llb_database_get_keys(_database, keys, errorPtr.ptr)
        
        if let error = errorPtr.msg {
            throw Error.operationDidFail(error)
        }
        if !success {
            throw Error.unknownError
        }
        
        guard let resultKeys = keys.pointee else {
            throw Error.unknownError
        }
        
        return BuildDBFetchResult(result: resultKeys)
    }
    
    public func dump() {
        llb_database_dump(_database)
    }
    
    // MARK: - Private functions for wrapping C++ API
    
    static private func toDB(_ context: UnsafeMutableRawPointer) -> BuildDB {
        return Unmanaged<BuildDB>.fromOpaque(UnsafeRawPointer(context)).takeUnretainedValue()
    }
    
    private func get_key_for_id(_ id: llb_database_key_id) -> llb_database_key_type? {
        return UnsafePointer(strdup(self.delegate.getKey(id: id)))
    }
    
    private func get_key_id(_ key: llb_database_key_type) -> llb_database_key_id {
        return self.delegate.getKeyID(key: KeyType(cString: key))
    }
}
