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
#else
#error("Missing libc or equivalent")
#endif

// We don't need this import if we're building
// this file as part of the llbuild framework.
#if !LLBUILD_FRAMEWORK
import llbuild
#endif

/// Create a new `llb_data_t` instance containing an allocated copy of the given `bytes`.
internal func copiedDataFromBytes(_ bytes: [UInt8]) -> llb_data_t {
    // Create the data.
    let allocation: UnsafeMutableRawPointer? = malloc(bytes.count)
    let buf = UnsafeMutableBufferPointer(start: allocation?.assumingMemoryBound(to: UInt8.self), count: bytes.count)

    // Copy the data.
    _ = bytes.withUnsafeBufferPointer { bytesPtr in
        memcpy(buf.baseAddress!, bytesPtr.baseAddress!, buf.count)
    }

    // Fill in the result structure.
    return llb_data_t(length: UInt64(buf.count), data: unsafeBitCast(buf.baseAddress, to: UnsafePointer<UInt8>.self))
}

// FIXME: We should eventually eliminate the need for this.
internal func stringFromData(_ data: llb_data_t) -> String {
    return String(decoding: UnsafeBufferPointer(start: data.data, count: Int(data.length)), as: Unicode.UTF8.self)
}

extension Array where Element == String {
    private func withTemporaryBuffer<T>(_ body: @escaping (UnsafePointer<UnsafePointer<CChar>>) -> T) -> T {
        // The buffer is in the form "a1\0a2\0a3\0…"
        let totalLength = reduce(0) { $0 + $1.utf8.count + 1 }
        let pointer = UnsafeMutablePointer<CChar>.allocate(capacity: totalLength)
        let buffer = UnsafeMutableBufferPointer(start: pointer, count: totalLength)
        let elementPointers = UnsafeMutablePointer<UnsafePointer<CChar>>.allocate(capacity: count)
        
        defer {
            pointer.deallocate()
            elementPointers.deallocate()
        }
        
        var bufferCursor = buffer.startIndex
        for (index, element) in enumerated() {
            let elementPtr = element.withCString { ptr -> UnsafePointer<CChar> in
                let wordLength = element.utf8.count
                let result = pointer.advanced(by: bufferCursor)
                
                memcpy(result, ptr, wordLength)
                bufferCursor += wordLength
                buffer[bufferCursor] = 0
                bufferCursor += 1
                return UnsafePointer(result)
            }
            elementPointers[index] = elementPtr
        }
        return body(UnsafePointer(elementPointers))
    }
    
    internal func withCArrayOfStrings<T>(_ body: @escaping (UnsafePointer<UnsafePointer<CChar>>) -> T) -> T {
        withTemporaryBuffer {
            body($0)
        }
    }
    
    internal func withCArrayOfOptionalStrings<T>(_ body: @escaping (UnsafePointer<UnsafePointer<CChar>?>) -> T) -> T {
        withTemporaryBuffer {
            $0.withMemoryRebound(to: UnsafePointer<CChar>?.self, capacity: count) { ptr in
                body(ptr)
            }
        }
    }
}
