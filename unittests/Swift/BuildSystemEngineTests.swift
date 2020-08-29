// This source file is part of the Swift.org open source project
//
// Copyright 2019-2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest

// The Swift package has llbuildSwift as module
#if SWIFT_PACKAGE
import llbuild
import llbuildSwift
#else
import llbuild
#endif

import llbuildTestSupport

// Command that always fails.
class FailureCommand: ExternalCommand {
    func getSignature(_ command: Command) -> [UInt8] {
        return []
    }

    func execute(_ command: Command, _ commandInterface: BuildSystemCommandInterface) -> Bool {
        return false
    }
}

protocol ExpectationCommand: class {
    func isFulfilled() -> Bool
}

// Command that expects to be executed.
class BasicCommand: ExternalCommand, ExpectationCommand {
    private var executed = false

    func getSignature(_ command: Command) -> [UInt8] {
        return []
    }

    func start(_ command: Command, _ commandInterface: BuildSystemCommandInterface) {}

    func provideValue(_ command: Command, _ commandInterface: BuildSystemCommandInterface, _ buildValue: BuildValue, _ inputID: UInt) {}

    func execute(_ command: Command, _ commandInterface: BuildSystemCommandInterface) -> Bool {
        executed = true
        return true
    }

    func isFulfilled() -> Bool {
        return executed
    }
}

// Command that expects to be executed after dependencies have executed.
class DependentCommand: BasicCommand {
    private var expectedValues: Set<UInt> = []
    private let dependencyNames: [String]
    private let discoveredDependencyNames: [String]

    init(dependencyNames: [String] = [], discoveredDependencyNames: [String] = []) {
        self.dependencyNames = dependencyNames
        self.discoveredDependencyNames = discoveredDependencyNames
    }

    override func start(_ command: Command, _ commandInterface: BuildSystemCommandInterface) {
        super.start(command, commandInterface)
        for (index, name) in dependencyNames.enumerated() {
            let key = BuildKey.CustomTask(name: name, taskData: "")
            let inputID = UInt(index)
            expectedValues.insert(inputID)
            commandInterface.commandNeedsInput(key: key, inputID: inputID)
        }
    }

    override func provideValue(_ command: Command, _ commandInterface: BuildSystemCommandInterface, _ buildValue: BuildValue, _ inputID: UInt) {
        super.provideValue(command, commandInterface, buildValue, inputID)
        expectedValues.remove(inputID)
    }

    override func execute(_ command: Command, _ commandInterface: BuildSystemCommandInterface) -> Bool {
        let result = super.execute(command, commandInterface)
        discoveredDependencyNames.forEach { name in
            let key = BuildKey.CustomTask(name: name, taskData: "")
            commandInterface.commandDiscoveredDependency(key: key)
        }
        return result
    }

    override func isFulfilled() -> Bool {
        super.isFulfilled() && expectedValues.isEmpty
    }
}

// Enhanced command that returns a custom build value
class EnhancedCommand: ExternalCommand, ProducesCustomBuildValue {
    private let fileInfo: BuildValueFileInfo
    private var executed = false

    init(_ fileInfo: BuildValueFileInfo) {
        self.fileInfo = fileInfo
    }

    func getSignature(_ command: Command) -> [UInt8] {
        return []
    }

    func start(_ command: Command, _ commandInterface: BuildSystemCommandInterface) {}

    func provideValue(_ command: Command, _ commandInterface: BuildSystemCommandInterface, _ buildValue: BuildValue, _ inputID: UInt) {}

    func execute(_ command: Command, _ commandInterface: BuildSystemCommandInterface) -> BuildValue {
        executed = true
        return BuildValue.SuccessfulCommand(outputInfos: [fileInfo])
    }

    func isResultValid(_ command: Command, _ buildValue: BuildValue) -> Bool {
        guard let value = buildValue as? BuildValue.SuccessfulCommand else {
            return false
        }

        return value.outputInfos.count == 1 && value.outputInfos[0] == BuildValueFileInfo(device: 1, inode: 2, mode: 3, size: 4, modTime: BuildValueFileTimestamp())
    }

    func wasExecuted() -> Bool {
        return executed
    }

    func reset() {
        executed = false
    }
}

final class TestTool: Tool {
    let expectedCommands: [String: ExternalCommand]

    init(expectedCommands: [String: ExternalCommand]) {
        self.expectedCommands = expectedCommands
    }

    func createCommand(_ name: String) -> ExternalCommand {
        guard let command = expectedCommands[name] else {
            XCTFail("Command \(name) not expected.")
            return FailureCommand()
        }
        return command
    }

    func createCustomCommand(_ key: BuildKey.CustomTask) -> ExternalCommand? {
        guard let command = expectedCommands[key.name] else {
            XCTFail("Command \(key.name) not expected.")
            return nil
        }
        return command
    }
}

final class TestBuildSystemDelegate: BuildSystemDelegate {
    let tool: Tool
    init(tool: Tool) {
        self.tool = tool
    }

    var fs: FileSystem?

    func lookupTool(_ name: String) -> Tool? {
        return tool
    }

    func hadCommandFailure() {}

    func handleDiagnostic(_ diagnostic: Diagnostic) {}

    func commandStatusChanged(_ command: Command, kind: CommandStatusKind) {}

    func commandPreparing(_ command: Command) {}

    func commandStarted(_ command: Command) {}

    func shouldCommandStart(_ command: Command) -> Bool {
        return true
    }

    func commandFinished(_ command: Command, result: CommandResult) {}

    func commandHadError(_ command: Command, message: String) {}

    func commandHadNote(_ command: Command, message: String) {}

    func commandHadWarning(_ command: Command, message: String) {}

    func commandCannotBuildOutputDueToMissingInputs(_ command: Command, output: BuildKey, inputs: [BuildKey]) {}

    func cannotBuildNodeDueToMultipleProducers(output: BuildKey, commands: [Command]) {}

    func commandProcessStarted(_ command: Command, process: ProcessHandle) {}

    func commandProcessHadError(_ command: Command, process: ProcessHandle, message: String) {}

    func commandProcessHadOutput(_ command: Command, process: ProcessHandle, data: [UInt8]) {}

    func commandProcessFinished(_ command: Command, process: ProcessHandle, result: CommandExtendedResult) {}

    func cycleDetected(rules: [BuildKey]) {}

    func shouldResolveCycle(rules: [BuildKey], candidate: BuildKey, action: CycleAction) -> Bool {
      return false
  }
}

class TestBuildSystem {
    let delegate: BuildSystemDelegate
    let buildSystem: BuildSystem

    init(buildFile: String, databaseFile: String, expectedCommands: [String: ExternalCommand]) {
        let tool = TestTool(expectedCommands: expectedCommands)
        delegate = TestBuildSystemDelegate(tool: tool)
        buildSystem = BuildSystem(buildFile: buildFile, databaseFile: databaseFile, delegate: delegate)
    }

    func run(target: String) {
        XCTAssertTrue(buildSystem.build(target: target))
    }

    func build(node: String, walker: RuleResultsWalker) {
        XCTAssertTrue(buildSystem.build(node: node, resultsWalker: walker))
    }
}

class BuildSystemEngineTests: XCTestCase {

    let basicBuildManifest = """
client:
  name: basic
  version: 0
  file-system: default

tools:
  testtool: {}

targets:
  all: ["<all>"]

commands:
  maincommand:
    tool: testtool
    inputs: []
    outputs: ["<all>"]

"""

    func testCommand() {
        let buildFile = makeTemporaryFile(basicBuildManifest)
        let databaseFile = makeTemporaryFile()

        let expectedCommands = [
            "maincommand": BasicCommand()
        ]

        let buildSystem = TestBuildSystem(
            buildFile: buildFile,
            databaseFile: databaseFile,
            expectedCommands: expectedCommands
        )
        buildSystem.run(target: "all")

        for (name, command) in expectedCommands {
            XCTAssert(command.isFulfilled(), "\(name) did not execute")
        }
    }

    func testDynamicCommand() {
        let buildFile = makeTemporaryFile(basicBuildManifest)
        let databaseFile = makeTemporaryFile()

        let expectedCommands = [
            "maincommand": DependentCommand(dependencyNames: ["dependency1"]),
            "dependency1": BasicCommand()
        ]


        let buildSystem = TestBuildSystem(
            buildFile: buildFile,
            databaseFile: databaseFile,
            expectedCommands: expectedCommands
        )
        buildSystem.run(target: "all")

        for (name, command) in expectedCommands {
            XCTAssert(command.isFulfilled(), "\(name) is not fulfilled")
        }
    }

    func testSerialTransitiveDynamicCommand() {
        let buildFile = makeTemporaryFile(basicBuildManifest)
        let databaseFile = makeTemporaryFile()

        let expectedCommands = [
            "maincommand": DependentCommand(dependencyNames: ["dependency1"]),
            "dependency1": DependentCommand(dependencyNames: ["dependency2"]),
            "dependency2": DependentCommand(dependencyNames: ["dependency3"]),
            "dependency3": BasicCommand(),
        ]


        let buildSystem = TestBuildSystem(
            buildFile: buildFile,
            databaseFile: databaseFile,
            expectedCommands: expectedCommands
        )
        buildSystem.run(target: "all")

        for (name, command) in expectedCommands {
            XCTAssert(command.isFulfilled(), "\(name) is not fulfilled")
        }
    }

    func testParallelTransitiveDynamicCommand() {
        let buildFile = makeTemporaryFile(basicBuildManifest)
        let databaseFile = makeTemporaryFile()

        let expectedCommands = [
            "maincommand": DependentCommand(dependencyNames: ["dependency1", "dependency2", "dependency3"]),
            "dependency1": BasicCommand(),
            "dependency2": BasicCommand(),
            "dependency3": BasicCommand(),
        ]

        let buildSystem = TestBuildSystem(
            buildFile: buildFile,
            databaseFile: databaseFile,
            expectedCommands: expectedCommands
        )
        buildSystem.run(target: "all")

        for (name, command) in expectedCommands {
            XCTAssert(command.isFulfilled(), "\(name) is not fulfilled")
        }
    }

    func testDiscoveredDependenciesCommand() {
        let buildFile = makeTemporaryFile(basicBuildManifest)
        let databaseFile = makeTemporaryFile()

        let expectedCommands = [
            "maincommand": DependentCommand(discoveredDependencyNames: ["discoveredDependency1"]),
            "discoveredDependency1": BasicCommand(),
        ]

        let buildSystem = TestBuildSystem(
            buildFile: buildFile,
            databaseFile: databaseFile,
            expectedCommands: expectedCommands
        )
        buildSystem.run(target: "all")

        for (name, command) in expectedCommands {
            XCTAssert(command.isFulfilled(), "\(name) is not fulfilled")
        }
    }

    func testEnhancedCommand() throws {
        let buildFile = makeTemporaryFile(basicBuildManifest)
        let databaseFile = makeTemporaryFile()

        let expectedCommands = [
            "maincommand": EnhancedCommand(BuildValueFileInfo(device: 1, inode: 2, mode: 3, size: 4, modTime: BuildValueFileTimestamp()))
        ]

        let buildSystem = TestBuildSystem(
            buildFile: buildFile,
            databaseFile: databaseFile,
            expectedCommands: expectedCommands
        )
        buildSystem.run(target: "all")

        for (name, command) in expectedCommands {
            XCTAssert(command.wasExecuted(), "\(name) did not execute")
        }

        // reset commands
        for (_, command) in expectedCommands {
            command.reset()
        }

        // run subsequent build
        buildSystem.run(target: "all")

        // check that the commands weren't executed
        for (name, command) in expectedCommands {
            XCTAssert(!command.wasExecuted(), "\(name) executed on incremental build")
        }

        // Validate that the custom build value was collected by checking the
        // database contents.
        let db = try BuildDB(path: databaseFile, clientSchemaVersion: 9)
        guard let maincommandResult = try db.lookupRuleResult(buildKey: BuildKey.Command(name: "maincommand")) else {
            return XCTFail("Unable to load command value from db")
        }

        let fileInfo = BuildValueFileInfo(device: 1, inode: 2, mode: 3, size: 4, modTime: BuildValueFileTimestamp())
        XCTAssertEqual(maincommandResult.value, BuildValue.SuccessfulCommand(outputInfos: [fileInfo]))
    }

    func testBuildAndWalkNodes() {
        let buildManifest = """
client:
  name: basic
  version: 0
  file-system: default

tools:
  testtool: {}

targets:
  all: ["<all>"]

commands:
  cmdAll:
    tool: testtool
    inputs: ["a"]
    outputs: ["<all>"]
  cmdA:
    tool: testtool
    inputs: ["b", "c"]
    outputs: ["a"]
  cmdB:
    tool: testtool
    inputs: ["d"]
    outputs: ["b"]
  cmdC:
    tool: testtool
    outputs: ["c"]
  cmdD:
    tool: testtool
    outputs: ["d"]

"""
        let buildFile = makeTemporaryFile(buildManifest)
        let databaseFile = makeTemporaryFile()

        let fileInfoA = BuildValueFileInfo(device: 1, inode: 2, mode: 3, size: 4, modTime: BuildValueFileTimestamp(seconds: 1, nanoseconds: 1))
        let fileInfoB = BuildValueFileInfo(device: 11, inode: 12, mode: 13, size: 14, modTime: BuildValueFileTimestamp(seconds: 11, nanoseconds: 1))
        let fileInfoC = BuildValueFileInfo(device: 21, inode: 22, mode: 23, size: 24, modTime: BuildValueFileTimestamp(seconds: 21, nanoseconds: 1))
        let fileInfoD = BuildValueFileInfo(device: 31, inode: 32, mode: 33, size: 34, modTime: BuildValueFileTimestamp(seconds: 31, nanoseconds: 1))

        let expectedCommands: [String: ExternalCommand] = [
            "cmdAll": BasicCommand(),
            "cmdA": EnhancedCommand(fileInfoA),
            "cmdB": EnhancedCommand(fileInfoB),
            "cmdC": EnhancedCommand(fileInfoC),
            "cmdD": EnhancedCommand(fileInfoD),
        ]

        struct NodeResult: Equatable {
            let key: String
            let value: BuildValueFileInfo

            init(_ key: String, _ value: BuildValueFileInfo) {
                self.key = key
                self.value = value
            }
        }

        class TestResultsWalker: RuleResultsWalker {
            let topNode: String
            let action: RuleResultsWalkActionKind
            var results: [NodeResult] = []

            init(topNode: String, action: RuleResultsWalkActionKind) {
                self.topNode = topNode
                self.action = action
            }

            func visit(key: BuildKey, result: BuildSystemRuleResult) -> RuleResultsWalkActionKind {
                guard key.key != topNode else {
                    return .visitDependencies
                }
                guard let inputVal = result.value as? BuildValue.ExistingInput else {
                    return .visitDependencies
                }
                results.append(NodeResult(key.key, inputVal.fileInfo))
                return action
            }
        }

        func checkResults(_ action: RuleResultsWalkActionKind, block: ([NodeResult])->()) {
            let buildSystem = TestBuildSystem(
                buildFile: buildFile,
                databaseFile: databaseFile,
                expectedCommands: expectedCommands
            )
            let walker = TestResultsWalker(topNode: "a", action: action)
            buildSystem.build(node: "a", walker: walker)
            block(walker.results)
        }

        checkResults(.visitDependencies) { results in
            XCTAssertEqual(results, [
                NodeResult("c", fileInfoC),
                NodeResult("b", fileInfoB),
                NodeResult("d", fileInfoD),
            ])
        }
        checkResults(.skipDependencies) { results in
            XCTAssertEqual(results, [
                NodeResult("b", fileInfoB),
                NodeResult("c", fileInfoC),
            ])
        }
        checkResults(.stop) { results in
            XCTAssertEqual(results, [
                NodeResult("b", fileInfoB),
            ])
        }
    }
}
