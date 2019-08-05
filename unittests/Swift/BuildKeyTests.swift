//
//  BuildKeyTests.swift
//  llbuildSwiftTests
//
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

class BuildKeyTests: XCTestCase {

  func testCommand() {
    let command = BuildKey.Command(name: "foobar")
    XCTAssertEqual(command.kind, .command)
    XCTAssertEqual(command.name, "foobar")
    XCTAssertFalse(command.keyData.isEmpty)
    XCTAssertEqual(command.key, "foobar")
    XCTAssertEqual(command, command)
    XCTAssertEqual(command.description, "<BuildKey.Command name=foobar>")
    XCTAssertNotEqual(command, BuildKey.Command(name: "foobar2"))
  }
  
  func testCustomTask() {
    let customTask = BuildKey.CustomTask(name: "foo", taskData: "bar")
    XCTAssertEqual(customTask.kind, .customTask)
    XCTAssertEqual(customTask.name, "foo")
    XCTAssertEqual(customTask.taskData, "bar")
    XCTAssertFalse(customTask.keyData.isEmpty)
    XCTAssertEqual(customTask, customTask)
    XCTAssertNotEqual(customTask, BuildKey.CustomTask(name: "foo2", taskData: "bar2"))
  }

  func testDirectoryContents() {
    let directoryContents = BuildKey.DirectoryContents(path: "/foo/bar")
    XCTAssertEqual(directoryContents.kind, .directoryContents)
    XCTAssertEqual(directoryContents.path, "/foo/bar")
    XCTAssertFalse(directoryContents.keyData.isEmpty)
    XCTAssertEqual(directoryContents.key, "/foo/bar")
    XCTAssertEqual(directoryContents, directoryContents)
    XCTAssertNotEqual(directoryContents, BuildKey.DirectoryContents(path: "/foo/bar2"))
  }

  func testFilteredDirectoryContents() {
    let filteredDirectoryContents = BuildKey.FilteredDirectoryContents(path: "/foo/bar", filters: ["jpg", "png"])
    XCTAssertEqual(filteredDirectoryContents.kind, .filteredDirectoryContents)
    XCTAssertEqual(filteredDirectoryContents.path, "/foo/bar")
    XCTAssertEqual(filteredDirectoryContents.filters, ["jpg", "png"])
    XCTAssertFalse(filteredDirectoryContents.keyData.isEmpty)
    XCTAssertEqual(filteredDirectoryContents, filteredDirectoryContents)
    XCTAssertNotEqual(filteredDirectoryContents, BuildKey.FilteredDirectoryContents(path: "/foo/bar2", filters: ["jpg"]))
  }

  func testDirectoryTreeSignature() {
    let directoryTreeSignature = BuildKey.DirectoryTreeSignature(path: "/foo/bar", filters: ["jpg", "png"])
    XCTAssertEqual(directoryTreeSignature.kind, .directoryTreeSignature)
    XCTAssertEqual(directoryTreeSignature.path, "/foo/bar")
    XCTAssertEqual(directoryTreeSignature.filters, ["jpg", "png"])
    XCTAssertFalse(directoryTreeSignature.keyData.isEmpty)
    XCTAssertEqual(directoryTreeSignature, directoryTreeSignature)
    XCTAssertNotEqual(directoryTreeSignature, BuildKey.DirectoryTreeSignature(path: "/foo/bar2", filters: ["jpg"]))
  }

  func testDirectoryTreeStructureSignature() {
    let directoryTreeStructureSignature = BuildKey.DirectoryTreeStructureSignature(path: "/foo/bar")
    XCTAssertEqual(directoryTreeStructureSignature.kind, .directoryTreeStructureSignature)
    XCTAssertEqual(directoryTreeStructureSignature.path, "/foo/bar")
    XCTAssertFalse(directoryTreeStructureSignature.keyData.isEmpty)
    XCTAssertEqual(directoryTreeStructureSignature.key, "/foo/bar")
    XCTAssertEqual(directoryTreeStructureSignature, directoryTreeStructureSignature)
    XCTAssertNotEqual(directoryTreeStructureSignature, BuildKey.DirectoryTreeStructureSignature(path: "/foo/bar2"))
  }

  func testNode() {
    let node = BuildKey.Node(path: "/foo/bar")
    XCTAssertEqual(node.kind, .node)
    XCTAssertEqual(node.path, "/foo/bar")
    XCTAssertFalse(node.keyData.isEmpty)
    XCTAssertEqual(node.key, "/foo/bar")
    XCTAssertEqual(node, node)
    XCTAssertNotEqual(node, BuildKey.Node(path: "/foo/bar2"))
  }

  func testStat() {
    let stat = BuildKey.Stat(path: "/foo/bar")
    XCTAssertEqual(stat.kind, .stat)
    XCTAssertEqual(stat.path, "/foo/bar")
    XCTAssertFalse(stat.keyData.isEmpty)
    XCTAssertEqual(stat.key, "/foo/bar")
    XCTAssertEqual(stat, stat)
    XCTAssertNotEqual(stat, BuildKey.Stat(path: "/foo/bar2"))
  }

  func testTarget() {
    let target = BuildKey.Target(name: "foobar")
    XCTAssertEqual(target.kind, .target)
    XCTAssertEqual(target.name, "foobar")
    XCTAssertFalse(target.keyData.isEmpty)
    XCTAssertEqual(target.key, "foobar")
    XCTAssertEqual(target, target)
    XCTAssertNotEqual(target, BuildKey.Target(name: "foobar2"))
  }

  func testConstruct() throws {
    // This test creates a raw llb_build_key_t and constructs a BuildKey from the given BuildKey instance
    func test<T: BuildKey>(_ instance: T, file: StaticString = #file, line: UInt = #line, _ expectation: (T) throws -> Void) throws {
      let keyData = instance.keyData
      try keyData.withUnsafeBufferPointer { ptr in
        let data = llb_data_t(length: UInt64(keyData.count), data: ptr.baseAddress)
        let constructedKey = BuildKey.construct(data: data)
        guard let typedKey = constructedKey as? T else {
          XCTFail("Expected to be able to construct a build key of type \(T.self) from \(data), but BuildKey.construct(data:) returned \(constructedKey) of type \(type(of: constructedKey)).", file: file, line: line)
          return
        }
        try expectation(typedKey)
        XCTAssertEqual(instance, typedKey, file: file, line: line)
      }
    }

    try test(BuildKey.Command(name: "foobar")) {
      XCTAssertEqual($0.kind, .command)
      XCTAssertEqual($0.name, "foobar")
    }

    try test(BuildKey.CustomTask(name: "foo", taskData: "bar")) {
      XCTAssertEqual($0.kind, .customTask)
      XCTAssertEqual($0.name, "foo")
      XCTAssertEqual($0.taskData, "bar")
    }
    
    try test(BuildKey.DirectoryContents(path: "/foo/bar")) {
      XCTAssertEqual($0.kind, .directoryContents)
      XCTAssertEqual($0.path, "/foo/bar")
    }
    
    try test(BuildKey.FilteredDirectoryContents(path: "/foo/bar", filters: ["jpg", "png"])) {
      XCTAssertEqual($0.kind, .filteredDirectoryContents)
      XCTAssertEqual($0.path, "/foo/bar")
      XCTAssertEqual($0.filters, ["jpg", "png"])
    }
    
    try test(BuildKey.DirectoryTreeSignature(path: "/foo/bar", filters: ["jpg", "png"])) {
      XCTAssertEqual($0.kind, .directoryTreeSignature)
      XCTAssertEqual($0.path, "/foo/bar")
      XCTAssertEqual($0.filters, ["jpg", "png"])
    }
    
    try test(BuildKey.DirectoryTreeStructureSignature(path: "/foo/bar")) {
      XCTAssertEqual($0.kind, .directoryTreeStructureSignature)
      XCTAssertEqual($0.path, "/foo/bar")
    }
    
    try test(BuildKey.Node(path: "/foo/bar")) {
      XCTAssertEqual($0.kind, .node)
      XCTAssertEqual($0.path, "/foo/bar")
    }
    
    try test(BuildKey.Stat(path: "/foo/bar")) {
      XCTAssertEqual($0.kind, .stat)
      XCTAssertEqual($0.path, "/foo/bar")
    }
    
    try test(BuildKey.Target(name: "foobar")) {
      XCTAssertEqual($0.kind, .target)
      XCTAssertEqual($0.name, "foobar")
    }
  }
  
}
