// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

// This file contains Swift bindings for the llbuild C API.

#if canImport(Darwin)
import Darwin.C
#elseif os(Windows)
import ucrt
import WinSDK
#elseif canImport(Glibc)
import Glibc
#elseif canImport(Musl)
import Musl
#elseif canImport(Android)
import Android
#else
#error("Missing libc or equivalent")
#endif

// We don't need this import if we're building
// this file as part of the llbuild framework.
#if !LLBUILD_FRAMEWORK
import llbuild
#endif

/// Abstract class of a build key, use subclasses
public class BuildKey: CustomStringConvertible, Equatable, Hashable {
    public typealias Kind = BuildKeyKind
    
    /// Returns the kind of the key
    public var kind: Kind {
        return llb_build_key_get_kind(internalBuildKey)
    }
    
    /// Returns the key data without the identifier for the kind
    /// This can't be removed for legacy reasons.
    public var key: String {
        return String(cString: Array(self.keyData.dropFirst() + [0]))
    }
    
    /// This raw data is used for internal representation and is encoded differently per subclass
    public lazy var keyData: KeyType = {
        var data: KeyType?
        withUnsafeMutablePointer(to: &data) { ptr in
            llb_build_key_get_key_data(internalBuildKey, ptr) { context, ptr, size in
                context.assumingMemoryBound(to: KeyType?.self).pointee = KeyType(UnsafeBufferPointer(start: ptr, count: Int(size)))
            }
        }
        return data!
    }()
    
    /// The opaque pointer to the BuildKey object
    internal let internalBuildKey: OpaquePointer
    
    /// Will only destroy the internalBuildKey on deinit if this is `false`.
    private let weakPointer: Bool
    
    fileprivate init(_ buildKey: OpaquePointer, weakPointer: Bool = false) {
        self.internalBuildKey = buildKey
        self.weakPointer = weakPointer
    }
    
    /// Makes use of APIs easier that use out parameter of type `llb_data_t`.
    /// - Parameter block: Use this block to call the API with the provided `llb_data_t` instance.
    fileprivate func formString(_ block: (inout llb_data_t) -> Void) -> String {
        var data = llb_data_t()
        block(&data)
        defer {
            llb_data_destroy(&data)
        }
        return stringFromData(data)
    }
    
    /// Constructs a build key from internal representation.
    /// It returns a specific subclass, depending on the kind of the given key.
    /// - Parameter key: The internal representation of the build key.
    public static func construct(key: OpaquePointer) -> BuildKey {
        return construct(key: key, weakPointer: true)
    }

    /// Constructs a build key from internal representation.
    /// It returns a specific subclass, depending on the kind of the given key.
    /// - Parameter key: The internal representation of the build key.
    /// - Parameter weakPointer: Returned `BuildKey` will only destroy the provided pointer on deinit if this is `false`.
    public static func construct(key: OpaquePointer, weakPointer: Bool) -> BuildKey {
        let kind = llb_build_key_get_kind(key)
        switch kind {
        case .command: return Command(key, weakPointer: weakPointer)
        case .customTask: return CustomTask(key, weakPointer: weakPointer)
        case .directoryContents: return DirectoryContents(key, weakPointer: weakPointer)
        case .filteredDirectoryContents: return FilteredDirectoryContents(key, weakPointer: weakPointer)
        case .directoryTreeSignature: return DirectoryTreeSignature(key, weakPointer: weakPointer)
        case .directoryTreeStructureSignature: return DirectoryTreeStructureSignature(key, weakPointer: weakPointer)
        case .node: return Node(key, weakPointer: weakPointer)
        case .stat: return Stat(key, weakPointer: weakPointer)
        case .target: return Target(key, weakPointer: weakPointer)
        default:
            assertionFailure("Unknown key kind: \(kind) - couldn't create concrete BuildKey class.")
            return BuildKey(key, weakPointer: weakPointer)
        }
    }
    
    public var description: String {
        return "<\(type(of: self))>"
    }
    
    public static func ==(lhs: BuildKey, rhs: BuildKey) -> Bool {
        return llb_build_key_equal(lhs.internalBuildKey, rhs.internalBuildKey)
    }
    
    public static func construct(data: llb_data_t) -> BuildKey {
        // `llb_build_key_make` allocates a new `CAPIBuildKey` object, so we set `weakPointer` to false to deallocate on deinit.
        return construct(key: withUnsafePointer(to: data, llb_build_key_make), weakPointer: false)
    }
    
    public func hash(into hasher: inout Hasher) {
        hasher.combine(llb_build_key_hash(internalBuildKey))
    }
    
    deinit {
        if weakPointer { return }
        llb_build_key_destroy(internalBuildKey)
    }
    
    /// A key used to identify a command.
    public final class Command: BuildKey {
        public convenience init(name: String) {
            self.init(llb_build_key_make_command(name))
        }
        
        /// The name of the command
        public var name: String {
            return formString { llb_build_key_get_command_name(internalBuildKey, &$0) }
        }
        
        public override var description: String {
            return "<BuildKey.\(type(of: self)) name=\(name)>"
        }
    }

    /// A key used to identify a custom task.
    public final class CustomTask: BuildKey {
        public convenience init(name: String, taskData: String) {
            self.init(llb_build_key_make_custom_task(name, taskData))
        }
        
        public convenience init(name: String, taskDataBytes: [UInt8]) {
            self.init(taskDataBytes.withUnsafeBufferPointer { buf in
                llb_build_key_make_custom_task_with_data(name, llb_data_t(length: UInt64(buf.count), data: buf.baseAddress))
            })
        }
        
        /// The name of the task
        public var name: String {
            return formString {
                llb_build_key_get_custom_task_name(internalBuildKey, &$0)
            }
        }
        
        /// Custom data attached to the task
        public var taskData: String {
            return formString {
                llb_build_key_get_custom_task_data(internalBuildKey, &$0)
            }
        }
        
        public var taskDataBytes: [UInt8] {
            var data = llb_data_t()
            llb_build_key_get_custom_task_data_no_copy(internalBuildKey, &data)
            return Array(UnsafeBufferPointer(start: data.data, count: Int(data.length)))
        }
        
        public override var description: String {
            return "<BuildKey.\(type(of: self)) name=\(name) taskData=\(taskData)>"
        }
    }

    /// A key used to identify directory contents.
    public final class DirectoryContents: BuildKey {
        public convenience init(path: String) {
            self.init(llb_build_key_make_directory_contents(path))
        }
        
        /// The path of the directory
        public var path: String {
            return formString {
                llb_build_key_get_directory_path(internalBuildKey, &$0)
            }
        }
        
        public override var description: String {
            return "<BuildKey.\(type(of: self)) path=\(path)>"
        }
    }

    /// A key used to identify filtered directory contents.
    public final class FilteredDirectoryContents: BuildKey {
        public convenience init(path: String, filters: [String]) {
            let ptr = filters.withCArrayOfStrings { ptr in
                llb_build_key_make_filtered_directory_contents(path, ptr, Int32(filters.count))
            }
            
            self.init(ptr)
        }
        
        /// The path of the directory
        public var path: String {
            return formString {
                llb_build_key_get_filtered_directory_path(internalBuildKey, &$0)
            }
        }
        
        /// The filters to apply to the content
        public var filters: [String] {
            var result = [String]()
            withUnsafeMutablePointer(to: &result) { ptr in
                llb_build_key_get_filtered_directory_filters(internalBuildKey, ptr) { ctx, data in
                    ctx!.assumingMemoryBound(to: [String].self).pointee.append(stringFromData(data))
                }
            }
            
            return result
        }
        
        public override var description: String {
            return "<BuildKey.\(type(of: self)) path=\(path) filters=[\(filters.joined(separator: ", "))]>"
        }
    }

    /// A key used to identify the signature of a complete directory tree.
    public final class DirectoryTreeSignature: BuildKey {
        public convenience init(path: String, filters: [String]) {
            let ptr = filters.withCArrayOfStrings { ptr in
                llb_build_key_make_directory_tree_signature(path, ptr, Int32(filters.count))
            }
            
            self.init(ptr)
        }
        
        /// The path of the directory
        public var path: String {
            return formString {
                llb_build_key_get_directory_tree_signature_path(internalBuildKey, &$0)
            }
        }
        
        /// The filters to apply to the content
        public var filters: [String] {
            var result = [String]()
            withUnsafeMutablePointer(to: &result) { ptr in
                llb_build_key_get_directory_tree_signature_filters(internalBuildKey, ptr) { ctx, data in
                    ctx!.assumingMemoryBound(to: [String].self).pointee.append(stringFromData(data))
                }
            }
            
            return result
        }
        
        public override var description: String {
            return "<BuildKey.\(type(of: self)) path=\(path) filters=[\(filters.joined(separator: ", "))]>"
        }
    }

    /// A key used to identify the signature of a complete directory tree.
    public final class DirectoryTreeStructureSignature: BuildKey {
        public convenience init(path: String, filters: [String] = []) {
            let ptr = filters.withCArrayOfStrings { ptr in
                llb_build_key_make_directory_tree_structure_signature(path, ptr, Int32(filters.count))
            }

            self.init(ptr)
        }
        
        /// The path of the directory
        public var path: String {
            return formString {
                llb_build_key_get_directory_tree_structure_signature_path(internalBuildKey, &$0)
            }
        }

        /// The filters to apply to the content
        public var filters: [String] {
            var result = [String]()
            withUnsafeMutablePointer(to: &result) { ptr in
                llb_build_key_get_directory_tree_structure_signature_filters(internalBuildKey, ptr) { ctx, data in
                    ctx!.assumingMemoryBound(to: [String].self).pointee.append(stringFromData(data))
                }
            }

            return result
        }

        public override var description: String {
            return "<BuildKey.\(type(of: self)) path=\(path) filters=[\(filters.joined(separator: ", "))]>"
        }
    }

    /// A key used to identify a node.
    public final class Node: BuildKey {
        public convenience init(path: String) {
            self.init(llb_build_key_make_node(path))
        }
        
        /// The path of the node
        public var path: String {
            return formString {
                llb_build_key_get_node_path(internalBuildKey, &$0)
            }
        }
        
        public override var description: String {
            return "<BuildKey.\(type(of: self)) path=\(path)>"
        }
    }

    /// A key used to identify a node.
    public final class Stat: BuildKey {
        public convenience init(path: String) {
            self.init(llb_build_key_make_stat(path))
        }
        
        /// The path of the node
        public var path: String {
            return formString {
                llb_build_key_get_stat_path(internalBuildKey, &$0)
            }
        }
        
        public override var description: String {
            return "<BuildKey.\(type(of: self)) path=\(path)>"
        }
    }

    /// A key used to identify a target.
    public final class Target: BuildKey {
        public convenience init(name: String) {
            self.init(llb_build_key_make_target(name))
        }
        
        /// The name of the target
        public var name: String {
            return formString {
                llb_build_key_get_target_name(internalBuildKey, &$0)
            }
        }
        
        public override var description: String {
            return "<BuildKey.\(type(of: self)) name=\(name)>"
        }
    }

}
