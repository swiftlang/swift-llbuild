import XCTest

import CommandsTests
import CoreTests

var tests = [XCTestCaseEntry]()
tests += CommandsTests.__allTests()
tests += CoreTests.__allTests()

XCTMain(tests)
