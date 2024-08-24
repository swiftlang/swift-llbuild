// This source file is part of the Swift.org open source project
//
// Copyright 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest

@available(macOS 10.15, *)
public extension XCTestCase {
    /// Create a temporary file with the given contents and returns the path to the file.
    //
    // FIXME: Move to a shared location.
    func makeTemporaryFile(_ contents: String? = nil) -> String {
        var directory = NSTemporaryDirectory()
        #if os(Windows)
        // Workaround: https://github.com/apple/swift-corelibs-foundation/issues/5066
        if directory.hasPrefix("/") {
            directory.removeFirst()
        }
        #endif
        let filename = UUID().uuidString
        let fileURL = URL(fileURLWithPath: directory).appendingPathComponent(filename)

        if let contents = contents {
            do {
                try contents.write(to: fileURL, atomically: false, encoding: .utf8)
            } catch {
                XCTFail("Error while writing to file: \(error)")
            }
        }

        let filePath = fileURL.withUnsafeFileSystemRepresentation {
            if let path = $0.map(String.init(cString:)) {
                return path
            } else {
                XCTFail("Could not convert file URL to path")
                return String()
            }
        }

        addTeardownBlock {
            do {
                let fileManager = FileManager.default
                if fileManager.fileExists(atPath: filePath) {
                    try fileManager.removeItem(at: fileURL)
                    XCTAssertFalse(fileManager.fileExists(atPath: filePath))
                }
            } catch {
                XCTFail("Error while deleting temporary file: \(error)")
            }
        }

        return filePath
    }
}
