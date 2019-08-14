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
        llb_build_key_get_kind(internalBuildKey)
    }
    
    /// Returns the key data without the identifier for the kind
    /// This can't be removed for legacy reasons.
    public var key: String {
        String(cString: Array(self.keyData.dropFirst() + [0]))
    }
    
    /// This raw data is used for internal representation and is encoded differently per subclass
    public var keyData: KeyType {
        var result = KeyType()
        withUnsafeMutablePointer(to: &result) { ptr in
            llb_build_key_get_key_data(internalBuildKey, ptr) { context, data in
                context?.assumingMemoryBound(to: KeyType.self).pointee.append(data)
            }
        }
        
        return result
    }
    
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
        let kind = llb_build_key_get_kind(key)
        switch kind {
        case .command: return Command(key, weakPointer: true)
        case .customTask: return CustomTask(key, weakPointer: true)
        case .directoryContents: return DirectoryContents(key, weakPointer: true)
        case .filteredDirectoryContents: return FilteredDirectoryContents(key, weakPointer: true)
        case .directoryTreeSignature: return DirectoryTreeSignature(key, weakPointer: true)
        case .directoryTreeStructureSignature: return DirectoryTreeStructureSignature(key, weakPointer: true)
        case .node: return Node(key, weakPointer: true)
        case .stat: return Stat(key, weakPointer: true)
        case .target: return Target(key, weakPointer: true)
        default:
            assertionFailure("Unknown key kind: \(kind) - couldn't create concrete BuildKey class.")
            return BuildKey(key, weakPointer: true)
        }
    }
    
    public var description: String {
        "<\(type(of: self))>"
    }
    
    public static func ==(lhs: BuildKey, rhs: BuildKey) -> Bool {
        return lhs.equal(to: rhs)
    }
    
    /// This needs to be overriden in subclass
    fileprivate func equal(to other: BuildKey) -> Bool {
        preconditionFailure("equal(to:) needs to be overridden in \(type(of: self))")
    }
    
    public static func construct(data: llb_data_t) -> BuildKey {
        construct(key: withUnsafePointer(to: data, llb_build_key_make))
    }
    
    public func hash(into hasher: inout Hasher) {
        hasher.combine(keyData)
    }
    
    deinit {
        llb_build_key_destroy(internalBuildKey)
    }
    
    /// A key used to identify a command.
    public final class Command: BuildKey {
        public convenience init(name: String) {
            self.init(llb_build_key_make_command(name))
        }
        
        /// The name of the command
        public var name: String {
            formString { llb_build_key_get_command_name(internalBuildKey, &$0) }
        }
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return name == rhs.name
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) name=\(name)>"
        }
    }

    /// A key used to identify a custom task.
    public final class CustomTask: BuildKey {
        public convenience init(name: String, taskData: String) {
            self.init(llb_build_key_make_custom_task(name, taskData))
        }
        
        /// The name of the task
        public var name: String {
            formString {
                llb_build_key_get_custom_task_name(internalBuildKey, &$0)
            }
        }
        
        /// Custom data attached to the task
        public var taskData: String {
            formString {
                llb_build_key_get_custom_task_data(internalBuildKey, &$0)
            }
        }
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return name == rhs.name && taskData == rhs.taskData
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) name=\(name) taskData=\(taskData)>"
        }
    }

    /// A key used to identify directory contents.
    public final class DirectoryContents: BuildKey {
        public convenience init(path: String) {
            self.init(llb_build_key_make_directory_contents(path))
        }
        
        /// The path of the directory
        public var path: String {
            formString {
                llb_build_key_get_directory_path(internalBuildKey, &$0)
            }
        }
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return path == rhs.path
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) path=\(path)>"
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
            formString {
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
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return path == rhs.path && filters == rhs.filters
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) path=\(path) filters=[\(filters.joined(separator: ", "))]>"
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
            formString {
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
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return path == rhs.path && filters == rhs.filters
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) path=\(path) filters=[\(filters.joined(separator: ", "))]>"
        }
    }

    /// A key used to identify the signature of a complete directory tree.
    public final class DirectoryTreeStructureSignature: BuildKey {
        public convenience init(path: String) {
            self.init(llb_build_key_make_directory_tree_structure_signature(path))
        }
        
        /// The path of the directory
        public var path: String {
            formString {
                llb_build_key_get_directory_tree_structure_signature_path(internalBuildKey, &$0)
            }
        }
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return path == rhs.path
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) path=\(path)>"
        }
    }

    /// A key used to identify a node.
    public final class Node: BuildKey {
        public convenience init(path: String) {
            self.init(llb_build_key_make_node(path))
        }
        
        /// The path of the node
        public var path: String {
            formString {
                llb_build_key_get_node_path(internalBuildKey, &$0)
            }
        }
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return path == rhs.path
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) path=\(path)>"
        }
    }

    /// A key used to identify a node.
    public final class Stat: BuildKey {
        public convenience init(path: String) {
            self.init(llb_build_key_make_stat(path))
        }
        
        /// The path of the node
        public var path: String {
            formString {
                llb_build_key_get_stat_path(internalBuildKey, &$0)
            }
        }
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return path == rhs.path
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) path=\(path)>"
        }
    }

    /// A key used to identify a target.
    public final class Target: BuildKey {
        public convenience init(name: String) {
            self.init(llb_build_key_make_target(name))
        }
        
        /// The name of the target
        public var name: String {
            formString {
                llb_build_key_get_target_name(internalBuildKey, &$0)
            }
        }
        
        override func equal(to other: BuildKey) -> Bool {
            guard let rhs = other as? Self else { return false }
            return name == rhs.name
        }
        
        public override var description: String {
            "<BuildKey.\(type(of: self)) name=\(name)>"
        }
    }

}
