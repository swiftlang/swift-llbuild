// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

// This file contains Swift bindings for the llbuild C API.

#if os(Linux)
import Glibc
#elseif os(Windows)
import MSVCRT
import WinSDK
#else
import Darwin.C
#endif

// We don't need this import if we're building
// this file as part of the llbuild framework.
#if !LLBUILD_FRAMEWORK
import llbuild
#endif

/// Create a new `llb_data_t` instance containing an allocated copy of the given `bytes`.
internal func copiedDataFromBytes(_ bytes: [UInt8]) -> llb_data_t {
    // Create the data.
    let buf = UnsafeMutableBufferPointer(start: UnsafeMutablePointer<UInt8>.allocate(capacity: bytes.count), count: bytes.count)

    // Copy the data.
    memcpy(buf.baseAddress!, UnsafePointer<UInt8>(bytes), buf.count)

    // Fill in the result structure.
    return llb_data_t(length: UInt64(buf.count), data: unsafeBitCast(buf.baseAddress, to: UnsafePointer<UInt8>.self))
}

// FIXME: We should eventually eliminate the need for this.
internal func stringFromData(_ data: llb_data_t) -> String {
    return String(decoding: UnsafeBufferPointer(start: data.data, count: Int(data.length)), as: Unicode.UTF8.self)
}

extension Array where Element == String {
    internal func withCArrayOfStrings<T>(_ body: @escaping (UnsafePointer<UnsafePointer<CChar>>) -> T) -> T {
        func appendPointer(_ index: Array.Index, to target: inout Array<UnsafePointer<CChar>>) -> T {
            if index == self.endIndex {
                return body(&target)
            } else {
                return self[index].withCString { cStringPtr in
                    target.append(cStringPtr)
                    return appendPointer(self.index(after: index), to: &target)
                }
            }
        }

        var elements = Array<UnsafePointer<CChar>>()
        elements.reserveCapacity(self.count)

        return appendPointer(self.startIndex, to: &elements)
    }
}
