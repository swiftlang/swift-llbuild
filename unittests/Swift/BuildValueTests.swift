//
//  BuildValueTests.swift
//  llbuildSwiftTests
//
//  Created by Benjamin Herzog on 30.07.19.
//  Copyright © 2019 Apple Inc. All rights reserved.
//

import XCTest

// The Swift package has llbuildSwift as module
#if SWIFT_PACKAGE
import llbuild
import llbuildSwift
#else
import llbuild
#endif

class BuildValueTests: XCTestCase {

  func testInvalid() {
    let buildValue = BuildValue.Invalid()
    XCTAssertEqual(buildValue.kind, .invalid)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.Invalid>")
    XCTAssertEqual(buildValue, BuildValue.Invalid())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testVirtualInput() {
    let buildValue = BuildValue.VirtualInput()
    XCTAssertEqual(buildValue.kind, .virtualInput)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.VirtualInput>")
    XCTAssertEqual(buildValue, BuildValue.VirtualInput())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testExistingInput() {
    let fileInfo = BuildValue.FileInfo(device: 1, inode: 2, mode: 3, size: 4, modTime: BuildValue.FileTimestamp(seconds: 5, nanoseconds: 6))
    let buildValue = BuildValue.ExistingInput(fileInfo: fileInfo)
    XCTAssertEqual(buildValue.kind, .existingInput)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.fileInfo, fileInfo)
    XCTAssertEqual(buildValue.description, "<BuildValue.ExistingInput fileInfo=<FileInfo device=1 inode=2 mode=3 size=4 modTime=<FileTimestamp seconds=5 nanoseconds=6>>>")
    XCTAssertEqual(buildValue, BuildValue.ExistingInput(fileInfo: fileInfo))
    XCTAssertNotEqual(buildValue, BuildValue.ExistingInput(fileInfo: BuildValue.FileInfo(device: 1, inode: 2, mode: 3, size: 4, modTime: BuildValue.FileTimestamp(seconds: 5, nanoseconds: 7))))
  }
  
  func testMissingInput() {
    let buildValue = BuildValue.MissingInput()
    XCTAssertEqual(buildValue.kind, .missingInput)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.MissingInput>")
    XCTAssertEqual(buildValue, BuildValue.MissingInput())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testDirectoryContents() {
    let fileInfo = BuildValue.FileInfo(device: 1, inode: 2, mode: 3, size: 4, modTime: BuildValue.FileTimestamp(seconds: 5, nanoseconds: 6))
    let buildValue = BuildValue.DirectoryContents(directoryInfo: fileInfo, contents: ["Sources", "Tests", "Package.swift"])
    XCTAssertEqual(buildValue.fileInfo, fileInfo)
    XCTAssertEqual(buildValue.contents, ["Sources", "Tests", "Package.swift"])
    XCTAssertEqual(buildValue.description, "<BuildValue.DirectoryContents fileInfo=<FileInfo device=1 inode=2 mode=3 size=4 modTime=<FileTimestamp seconds=5 nanoseconds=6>> contents=[Sources, Tests, Package.swift]>")
    XCTAssertEqual(buildValue, BuildValue.DirectoryContents(directoryInfo: fileInfo, contents: ["Sources", "Tests", "Package.swift"]))
    XCTAssertNotEqual(buildValue, BuildValue.DirectoryContents(directoryInfo: fileInfo, contents: ["Sources", "Tests"]))
  }

  func testDirectoryTreeSignature() {
    let buildValue = BuildValue.DirectoryTreeSignature(signature: 42)
    XCTAssertEqual(buildValue.kind, .directoryTreeSignature)
    XCTAssertEqual(buildValue.signature, 42)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.DirectoryTreeSignature signature=42>")
    XCTAssertEqual(buildValue, BuildValue.DirectoryTreeSignature(signature: 42))
    XCTAssertNotEqual(buildValue, BuildValue.DirectoryTreeSignature(signature: 2))
  }
  
  func testDirectoryTreeStructureSignature() {
    let buildValue = BuildValue.DirectoryTreeStructureSignature(signature: 42)
    XCTAssertEqual(buildValue.kind, .directoryTreeStructureSignature)
    XCTAssertEqual(buildValue.signature, 42)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.DirectoryTreeStructureSignature signature=42>")
    XCTAssertEqual(buildValue, BuildValue.DirectoryTreeStructureSignature(signature: 42))
    XCTAssertNotEqual(buildValue, BuildValue.DirectoryTreeStructureSignature(signature: 2))
  }
  
  func testMissingOutput() {
    let buildValue = BuildValue.MissingOutput()
    XCTAssertEqual(buildValue.kind, .missingOutput)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.MissingOutput>")
    XCTAssertEqual(buildValue, BuildValue.MissingOutput())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testFailedInput() {
    let buildValue = BuildValue.FailedInput()
    XCTAssertEqual(buildValue.kind, .failedInput)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.FailedInput>")
    XCTAssertEqual(buildValue, BuildValue.FailedInput())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testSuccessfulCommand() {
    let fileInfos = [1, 2, 3].map {
      BuildValue.FileInfo(device: 1 * $0, inode: 2 * $0, mode: 3 * $0, size: 4 * $0, modTime: BuildValue.FileTimestamp(seconds: 5 * $0, nanoseconds: 6 * $0))
    }
    let buildValue = BuildValue.SuccessfulCommand(outputInfos: fileInfos)
    XCTAssertEqual(buildValue.kind, .successfulCommand)
    XCTAssertEqual(buildValue.outputInfos, fileInfos)
    XCTAssertEqual(buildValue.description, "<BuildValue.SuccessfulCommand outputInfos=[<FileInfo device=1 inode=2 mode=3 size=4 modTime=<FileTimestamp seconds=5 nanoseconds=6>>, <FileInfo device=2 inode=4 mode=6 size=8 modTime=<FileTimestamp seconds=10 nanoseconds=12>>, <FileInfo device=3 inode=6 mode=9 size=12 modTime=<FileTimestamp seconds=15 nanoseconds=18>>]>")
    XCTAssertEqual(buildValue, BuildValue.SuccessfulCommand(outputInfos: fileInfos))
    XCTAssertNotEqual(buildValue, BuildValue.SuccessfulCommand(outputInfos: Array(fileInfos.dropFirst())))
  }
  
  func testFailedCommand() {
    let buildValue = BuildValue.FailedCommand()
    XCTAssertEqual(buildValue.kind, .failedCommand)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.FailedCommand>")
    XCTAssertEqual(buildValue, BuildValue.FailedCommand())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testPropagatedFailedCommand() {
    let buildValue = BuildValue.PropagatedFailureCommand()
    XCTAssertEqual(buildValue.kind, .propagatedFailureCommand)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.PropagatedFailureCommand>")
    XCTAssertEqual(buildValue, BuildValue.PropagatedFailureCommand())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testCancelledCommand() {
    let buildValue = BuildValue.CancelledCommand()
    XCTAssertEqual(buildValue.kind, .cancelledCommand)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.CancelledCommand>")
    XCTAssertEqual(buildValue, BuildValue.CancelledCommand())
    XCTAssertNotEqual(buildValue, BuildValue.SkippedCommand())
  }
  
  func testSkippedCommand() {
    let buildValue = BuildValue.SkippedCommand()
    XCTAssertEqual(buildValue.kind, .skippedCommand)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.SkippedCommand>")
    XCTAssertEqual(buildValue, BuildValue.SkippedCommand())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testTarget() {
    let buildValue = BuildValue.Target()
    XCTAssertEqual(buildValue.kind, .target)
    XCTAssertFalse(buildValue.valueData.isEmpty)
    XCTAssertEqual(buildValue.description, "<BuildValue.Target>")
    XCTAssertEqual(buildValue, BuildValue.Target())
    XCTAssertNotEqual(buildValue, BuildValue.CancelledCommand())
  }
  
  func testStaleFileRemoval() {
    let buildValue = BuildValue.StaleFileRemoval(fileList: ["/foo/bar.txt", "/bar/foo.swift"])
    XCTAssertEqual(buildValue.kind, .staleFileRemoval)
    XCTAssertEqual(buildValue.fileList, ["/foo/bar.txt", "/bar/foo.swift"])
    XCTAssertEqual(buildValue.description, "<BuildValue.StaleFileRemoval fileList=[/foo/bar.txt, /bar/foo.swift]>")
    XCTAssertEqual(buildValue, BuildValue.StaleFileRemoval(fileList: ["/foo/bar.txt", "/bar/foo.swift"]))
    XCTAssertNotEqual(buildValue, BuildValue.StaleFileRemoval(fileList: ["/foo/bar.txt"]))
  }
  
  func testFilteredDirectoryContents() {
    let buildValue = BuildValue.FilteredDirectoryContents(contents: ["/foo/bar.txt", "/bar/foo.swift"])
    XCTAssertEqual(buildValue.kind, .filteredDirectoryContents)
    XCTAssertEqual(buildValue.contents, ["/foo/bar.txt", "/bar/foo.swift"])
    XCTAssertEqual(buildValue.description, "<BuildValue.FilteredDirectoryContents contents=[/foo/bar.txt, /bar/foo.swift]>")
    XCTAssertEqual(buildValue, BuildValue.FilteredDirectoryContents(contents: ["/foo/bar.txt", "/bar/foo.swift"]))
    XCTAssertNotEqual(buildValue, BuildValue.FilteredDirectoryContents(contents: ["/foo/bar.txt", "/bar/foo.txt"]))
  }
  
  func testSuccessfulCommandWithOutputSignature() {
    let fileInfos = [1, 2, 3].map {
      BuildValue.FileInfo(device: 1 * $0, inode: 2 * $0, mode: 3 * $0, size: 4 * $0, modTime: BuildValue.FileTimestamp(seconds: 5 * $0, nanoseconds: 6 * $0))
    }
    let buildValue = BuildValue.SuccessfulCommandWithOutputSignature(outputInfos: fileInfos, signature: 42)
    XCTAssertEqual(buildValue.kind, .successfulCommandWithOutputSignature)
    XCTAssertEqual(buildValue.outputInfos, fileInfos)
    XCTAssertEqual(buildValue.signature, 42)
    XCTAssertEqual(buildValue, BuildValue.SuccessfulCommandWithOutputSignature(outputInfos: fileInfos, signature: 42))
    XCTAssertNotEqual(buildValue, BuildValue.SuccessfulCommandWithOutputSignature(outputInfos: Array(fileInfos.dropFirst()), signature: 3))
  }
  
  func testConstruct() {
    func test<T: BuildValue>(_ instance: T, test: (T) -> Void) {
      let data = instance.valueData
      guard let constructed = BuildValue.construct(from: Value(data)) else {
        XCTFail("Expected to be able to construct a build value of type \(T.self) from \(data).")
        return
      }
      guard let typedConstructed = constructed as? T else {
        XCTFail("Expected \(constructed) to be of type \(T.self) after deconstructing + constructing.")
        return
      }
      test(typedConstructed)
      XCTAssertEqual(instance, typedConstructed)
    }
    
    let fileInfo = BuildValue.FileInfo(device: 1, inode: 2, mode: 3, size: 4, modTime: BuildValue.FileTimestamp(seconds: 5, nanoseconds: 6))
    let signature: BuildValue.CommandSignature = 42
    let stringList = ["foo", "bar"]
    
    test(BuildValue.Invalid()) {
      XCTAssertEqual($0.kind, .invalid)
    }
    
    test(BuildValue.VirtualInput()) {
      XCTAssertEqual($0.kind, .virtualInput)
    }
    
    test(BuildValue.ExistingInput(fileInfo: fileInfo)) {
      XCTAssertEqual($0.kind, .existingInput)
      XCTAssertEqual($0.fileInfo, fileInfo)
    }
    
    test(BuildValue.MissingInput()) {
      XCTAssertEqual($0.kind, .missingInput)
    }
    
    test(BuildValue.DirectoryContents(directoryInfo: fileInfo, contents: stringList)) {
      XCTAssertEqual($0.kind, .directoryContents)
      XCTAssertEqual($0.fileInfo, fileInfo)
      XCTAssertEqual($0.contents, stringList)
    }
    
    test(BuildValue.DirectoryTreeSignature(signature: signature)) {
      XCTAssertEqual($0.kind, .directoryTreeSignature)
      XCTAssertEqual($0.signature, signature)
    }
    
    test(BuildValue.DirectoryTreeStructureSignature(signature: signature)) {
      XCTAssertEqual($0.kind, .directoryTreeStructureSignature)
      XCTAssertEqual($0.signature, signature)
    }
    
    test(BuildValue.MissingOutput()) {
      XCTAssertEqual($0.kind, .missingOutput)
    }
    
    test(BuildValue.FailedInput()) {
      XCTAssertEqual($0.kind, .failedInput)
    }
    
    test(BuildValue.SuccessfulCommand(outputInfos: [fileInfo])) {
      XCTAssertEqual($0.kind, .successfulCommand)
      XCTAssertEqual($0.outputInfos, [fileInfo])
    }
    
    test(BuildValue.FailedCommand()) {
      XCTAssertEqual($0.kind, .failedCommand)
    }
    
    test(BuildValue.PropagatedFailureCommand()) {
      XCTAssertEqual($0.kind, .propagatedFailureCommand)
    }
    
    test(BuildValue.CancelledCommand()) {
      XCTAssertEqual($0.kind, .cancelledCommand)
    }
    
    test(BuildValue.SkippedCommand()) {
      XCTAssertEqual($0.kind, .skippedCommand)
    }
    
    test(BuildValue.Target()) {
      XCTAssertEqual($0.kind, .target)
    }
    
    test(BuildValue.StaleFileRemoval(fileList: stringList)) {
      XCTAssertEqual($0.kind, .staleFileRemoval)
      XCTAssertEqual($0.fileList, stringList)
    }
    
    test(BuildValue.FilteredDirectoryContents(contents: stringList)) {
      XCTAssertEqual($0.kind, .filteredDirectoryContents)
      XCTAssertEqual($0.contents, stringList)
    }
    
    test(BuildValue.SuccessfulCommandWithOutputSignature(outputInfos: [fileInfo], signature: signature)) {
      XCTAssertEqual($0.kind, .successfulCommandWithOutputSignature)
      XCTAssertEqual($0.outputInfos, [fileInfo])
      XCTAssertEqual($0.signature, signature)
    }
  }
}
