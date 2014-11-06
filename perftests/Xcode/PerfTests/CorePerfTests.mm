//===-- BuildEngine.mm ---------------------------------------------------===//
//
// Copyright (c) 2014 Apple Inc. All rights reserved.
//
//===----------------------------------------------------------------------===//

#import "llbuild/Commands/Commands.h"

#import <XCTest/XCTest.h>

@interface CorePerfTests : XCTestCase

@end

@implementation CorePerfTests

- (void)testBuildEngineBasicPerf {
  // Test the timing of 'buildengine ack 3 14'.
  //
  // This test uses ~300k rules, and is a good stress test for the core engine
  // operation.
  
  [self measureBlock:^{
      llbuild::commands::ExecuteBuildEngineCommand({
          "ack", "3", "14" });
    }];
}

- (void)testBuildEngineDependencyScanningCorePerf {
  // Test the timing of 'buildengine ack 3 11', with a high recompute count.
  //
  // This test uses ~40k rules, but then recomputes the results multiple times,
  // which is a stress test of the dependency scanning performance.
    
  [self measureBlock:^{
      llbuild::commands::ExecuteBuildEngineCommand({
          "ack", "--recompute", "100", "3", "11" });
    }];
}

@end
