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

- (void)testBuildengineAck3_14 {
  // Test the timing of 'buildengine ack 3 14'.
  
  [self measureBlock:^{
          llbuild::commands::ExecuteBuildEngineCommand({
                  "ack", "3", "14" });
      }];
}

@end
