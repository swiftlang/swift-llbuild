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

extension BuildValueKind: CustomStringConvertible {
    public var description: String {
        switch self {
        case .invalid: return "invalid"
        case .virtualInput: return "virtualInput"
        case .existingInput: return "existingInput"
        case .missingInput: return "missingInput"
        case .directoryContents: return "directoryContents"
        case .directoryTreeSignature: return "directoryTreeSignature"
        case .directoryTreeStructureSignature: return "directoryTreeStructureSignature"
        case .staleFileRemoval: return "staleFileRemoval"
        case .missingOutput: return "missingOutput"
        case .failedInput: return "failedInput"
        case .successfulCommand: return "successfulCommand"
        case .failedCommand: return "failedCommand"
        case .propagatedFailureCommand: return "propagatedFailureCommand"
        case .cancelledCommand: return "cancelledCommand"
        case .skippedCommand: return "skippedCommand"
        case .target: return "target"
        case .filteredDirectoryContents: return "filteredDirectoryContents"
        case .successfulCommandWithOutputSignature: return "successfulCommandWithOutputSignature"
        @unknown default:
            return "unknown"
        }
    }
}

extension BuildValueFileInfo: Equatable {
    public static func == (lhs: BuildValueFileInfo, rhs: BuildValueFileInfo) -> Bool {
        lhs.device == rhs.device && lhs.inode == rhs.inode && lhs.mode == rhs.mode && lhs.size == rhs.size && lhs.modTime == rhs.modTime
    }
}

extension BuildValueFileInfo: CustomStringConvertible {
    public var description: String {
        "<FileInfo device=\(device) inode=\(inode) mode=\(mode) size=\(size) modTime=\(modTime)>"
    }
}

extension BuildValueFileTimestamp: Equatable {
    public static func == (lhs: llb_build_value_file_timestamp_t_, rhs: BuildValueFileTimestamp) -> Bool {
        lhs.seconds == rhs.seconds && lhs.nanoseconds == rhs.nanoseconds
    }
}

extension BuildValueFileTimestamp: CustomStringConvertible {
    public var description: String {
        "<FileTimestamp seconds=\(seconds) nanoseconds=\(nanoseconds)>"
    }
}

/// Abstract class of a build value, use subclasses
public class BuildValue: CustomStringConvertible, Equatable, Hashable {
    /// Build values can be of different kind - each has its own subclass
    public typealias Kind = BuildValueKind
    /// Describes information about a file or directory
    public typealias FileInfo = BuildValueFileInfo
    /// Is part of the file info by providing a timestamp for the modifiation time
    public typealias FileTimestamp = BuildValueFileTimestamp
    /// Is a calculated signature of general purpose used by some of the subclasses
    public typealias CommandSignature = BuildValueCommandSignature
    
    /// The opaque pointer to the internal C++ class
    fileprivate let internalBuildValue: OpaquePointer
    
    fileprivate init(_ internalBuildValue: OpaquePointer) {
        self.internalBuildValue = internalBuildValue
    }
    
    /// Tries to construct a BuildValue from the given data
    ///
    /// NOTE: If the data is malformed this code might assert.
    public static func construct(from value: Value) -> BuildValue? {
        var llbData = copiedDataFromBytes(value.data)
        defer {
            llb_data_destroy(&llbData)
        }
        
        let buildValue = llb_build_value_make(&llbData)
        
        switch llb_build_value_get_kind(buildValue) {
        case .invalid: return Invalid(buildValue)
        case .virtualInput: return VirtualInput(buildValue)
        case .existingInput: return ExistingInput(buildValue)
        case .missingInput: return MissingInput(buildValue)
        case .directoryContents: return DirectoryContents(buildValue)
        case .directoryTreeSignature: return DirectoryTreeSignature(buildValue)
        case .directoryTreeStructureSignature: return DirectoryTreeStructureSignature(buildValue)
        case .staleFileRemoval: return StaleFileRemoval(buildValue)
        case .missingOutput: return MissingOutput(buildValue)
        case .failedInput: return FailedInput(buildValue)
        case .successfulCommand: return SuccessfulCommand(buildValue)
        case .failedCommand: return FailedCommand(buildValue)
        case .propagatedFailureCommand: return PropagatedFailureCommand(buildValue)
        case .cancelledCommand: return CancelledCommand(buildValue)
        case .skippedCommand: return SkippedCommand(buildValue)
        case .target: return Target(buildValue)
        case .filteredDirectoryContents: return FilteredDirectoryContents(buildValue)
        case .successfulCommandWithOutputSignature: return SuccessfulCommandWithOutputSignature(buildValue)
        @unknown default: return nil
        }
    }
    
    /// The kind of the build value.
    /// The kind also defines the subclass, so kind == .invalid means the instance should be of type Invalid
    public var kind: Kind {
        return llb_build_value_get_kind(internalBuildValue)
    }
    
    /// The raw value data
    public var valueData: ValueType {
        var result = ValueType()
        withUnsafeMutablePointer(to: &result) { ptr in
            llb_build_value_get_value_data(internalBuildValue, ptr) { context, data in
                context?.assumingMemoryBound(to: ValueType.self).pointee.append(data)
            }
        }
        return result
    }
    
    public var description: String {
        "<BuildValue.\(type(of: self))>"
    }
    
    public static func ==(lhs: BuildValue, rhs: BuildValue) -> Bool {
        return lhs.equal(to: rhs)
    }
    
    public func hash(into hasher: inout Hasher) {
        hasher.combine(valueData)
    }
    
    /// This needs to be overriden in subclass if properties need to be checked
    fileprivate func equal(to other: BuildValue) -> Bool {
        type(of: self) == type(of: other)
    }
    
    /// An invalid value, for sentinel purposes.
    public final class Invalid: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_invalid())
        }
    }

    /// A value produced by a virtual input.
    public final class VirtualInput: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_virtual_input())
        }
    }

    /// A value produced by an existing input file.
    public final class ExistingInput: BuildValue {
        public convenience init(fileInfo: BuildValueFileInfo) {
            self.init(llb_build_value_make_existing_input(fileInfo))
        }
        
        /// Information about the existing input file
        public var fileInfo: FileInfo {
            return llb_build_value_get_output_info(internalBuildValue)
        }
        
        override func equal(to other: BuildValue) -> Bool {
            (other as? Self)?.fileInfo == fileInfo
        }
        
        public override var description: String {
            "<BuildValue.\(type(of: self)) fileInfo=\(fileInfo)>"
        }
    }

    /// A value produced by a missing input file.
    public final class MissingInput: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_missing_input())
        }
    }

    /// The contents of a directory.
    public final class DirectoryContents: BuildValue {
        public convenience init(directoryInfo: FileInfo, contents: [String]) {
            let ptr = contents.withCArrayOfStrings { ptr in
                llb_build_value_make_directory_contents(directoryInfo, ptr, Int32(contents.count))
            }
            self.init(ptr)
        }
        
        /// The information about the directory
        public var fileInfo: FileInfo {
            return llb_build_value_get_output_info(internalBuildValue)
        }
        
        /// The contents of the directory
        public var contents: [String] {
            var result: [String] = []
            withUnsafeMutablePointer(to: &result) { ptr in
                llb_build_value_get_directory_contents(internalBuildValue, ptr) { ctx, data in
                    ctx!.assumingMemoryBound(to: [String].self).pointee.append(stringFromData(data))
                }
            }
            return result
        }
        
        override func equal(to other: BuildValue) -> Bool {
            guard let other = other as? Self else { return false }
            return fileInfo == other.fileInfo && contents == other.contents
        }
        
        public override var description: String {
            "<BuildValue.\(type(of: self)) fileInfo=\(fileInfo) contents=[\(contents.joined(separator: ", "))]>"
        }
    }

    /// The signature of a directories contents.
    public final class DirectoryTreeSignature: BuildValue {
        public convenience init(signature: CommandSignature) {
            self.init(llb_build_value_make_directory_tree_signature(signature))
        }
        
        /// The signature of the directory tree
        public var signature: CommandSignature {
            return llb_build_value_get_directory_tree_signature(internalBuildValue)
        }
        
        override func equal(to other: BuildValue) -> Bool {
            guard let other = other as? Self else { return false }
            return signature == other.signature
        }
        
        public override var description: String {
            "<BuildValue.\(type(of: self)) signature=\(signature)>"
        }
    }

    /// The signature of a directories structure.
    public final class DirectoryTreeStructureSignature: BuildValue {
        public convenience init(signature: CommandSignature) {
            self.init(llb_build_value_make_directory_tree_structure_signature(signature))
        }
        
        /// The signature of the directory's tree structure
        public var signature: CommandSignature {
            return llb_build_value_get_directory_tree_structure_signature(internalBuildValue)
        }
        
        override func equal(to other: BuildValue) -> Bool {
            guard let other = other as? Self else { return false }
            return signature == other.signature
        }
        
        public override var description: String {
            "<BuildValue.\(type(of: self)) signature=\(signature)>"
        }
    }

    /// A value produced by a command which succeeded, but whose output was
    /// missing.
    public final class MissingOutput: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_missing_output())
        }
    }

    /// A value for a produced output whose command failed or was cancelled.
    public final class FailedInput: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_failed_input())
        }
    }

    /// A value produced by a successful command.
    public final class SuccessfulCommand: BuildValue {
        public convenience init(outputInfos: [FileInfo]) {
            self.init(llb_build_value_make_successful_command(outputInfos, Int32(outputInfos.count)))
        }
        
        /// Information about the outputs of the command
        public var outputInfos: [FileInfo] {
            var result: [FileInfo] = []
            withUnsafeMutablePointer(to: &result) { ptr in
                llb_build_value_get_file_infos(internalBuildValue, ptr) { ctx, fileInfo in
                    ctx!.assumingMemoryBound(to: [FileInfo].self).pointee.append(fileInfo)
                }
            }
            return result
        }
        
        override func equal(to other: BuildValue) -> Bool {
            guard let other = other as? Self else { return false }
            return outputInfos == other.outputInfos
        }
        
        public override var description: String {
            "<BuildValue.\(type(of: self)) outputInfos=[\(outputInfos.map { $0.description }.joined(separator: ", "))]>"
        }
    }

    /// A value produced by a failing command.
    public final class FailedCommand: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_failed_command())
        }
    }

    /// A value produced by a command which was skipped because one of its
    /// dependencies failed.
    public final class PropagatedFailureCommand: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_propagated_failure_command())
        }
    }

    /// A value produced by a command which was cancelled.
    public final class CancelledCommand: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_cancelled_command())
        }
    }

    /// A value produced by a command which was skipped.
    public final class SkippedCommand: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_skipped_command())
        }
    }

    /// Sentinel value representing the result of "building" a top-level target.
    public final class Target: BuildValue {
        public convenience init() {
            self.init(llb_build_value_make_target())
        }
    }

    /// A value produced by stale file removal.
    public final class StaleFileRemoval: BuildValue {
        public convenience init(fileList: [String]) {
            let ptr = fileList.withCArrayOfStrings { ptr in
                llb_build_value_make_stale_file_removal(ptr, Int32(fileList.count))
            }
            self.init(ptr)
        }
        
        /// A list of the files that got removed
        public var fileList: [String] {
            var result: [String] = []
            withUnsafeMutablePointer(to: &result) { ptr in
                llb_build_value_get_stale_file_list(internalBuildValue, ptr) { ctx, data in
                    ctx!.assumingMemoryBound(to: [String].self).pointee.append(stringFromData(data))
                }
            }
            return result
        }
        
        override func equal(to other: BuildValue) -> Bool {
            guard let other = other as? Self else { return false }
            return fileList == other.fileList
        }
        
        public override var description: String {
            "<BuildValue.\(type(of: self)) fileList=[\(fileList.joined(separator: ", "))]>"
        }
    }

    /// The filtered contents of a directory.
    public final class FilteredDirectoryContents: BuildValue {
        public convenience init(contents: [String]) {
            let ptr = contents.withCArrayOfStrings { ptr in
                llb_build_value_make_filtered_directory_contents(ptr, Int32(contents.count))
            }
            self.init(ptr)
        }
        
        /// The contents of the directory with the filter applied
        public var contents: [String] {
            var result: [String] = []
            withUnsafeMutablePointer(to: &result) { ptr in
                llb_build_value_get_directory_contents(internalBuildValue, ptr) { ctx, data in
                    ctx!.assumingMemoryBound(to: [String].self).pointee.append(stringFromData(data))
                }
            }
            return result
        }
        
        override func equal(to other: BuildValue) -> Bool {
            guard let other = other as? Self else { return false }
            return contents == other.contents
        }
        
        public override var description: String {
            "<BuildValue.\(type(of: self)) contents=[\(contents.joined(separator: ", "))]>"
        }
    }

    /// A value produced by a successful command with an output signature.
    public final class SuccessfulCommandWithOutputSignature: BuildValue {
        public convenience init(outputInfos: [FileInfo], signature: CommandSignature) {
            self.init(llb_build_value_make_successful_command_with_output_signature(outputInfos, Int32(outputInfos.count), signature))
        }
        
        /// Information about the outputs of the command
        public var outputInfos: [FileInfo] {
            var result: [FileInfo] = []
            withUnsafeMutablePointer(to: &result) { ptr in
                llb_build_value_get_file_infos(internalBuildValue, ptr) { ctx, fileInfo in
                    ctx!.assumingMemoryBound(to: [FileInfo].self).pointee.append(fileInfo)
                }
            }
            return result
        }
        
        /// The output signature of the command
        public var signature: CommandSignature {
            return llb_build_value_get_output_signature(internalBuildValue)
        }
        
        override func equal(to other: BuildValue) -> Bool {
            guard let other = other as? Self else { return false }
            return outputInfos == other.outputInfos && signature == other.signature
        }
        
        public override var description: String {
            "<BuildValue.\(type(of: self)) outputInfos=[\(outputInfos.map { $0.description }.joined(separator: ", "))] signature=\(signature)>"
        }
    }

}
