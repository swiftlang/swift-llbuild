//===-- BuildEngine.mm ---------------------------------------------------===//
//
// Copyright (c) 2014 Apple Inc. All rights reserved.
//
//===----------------------------------------------------------------------===//

#import "llbuild/Commands/Commands.h"

#import <XCTest/XCTest.h>

@interface NinjaPerfTests : XCTestCase

@end

static void ExecuteShellCommand(const char *String) {
  NSLog(@"running shell command: %s", String);
  int Result = system(String);
  if (Result != 0) {
    NSLog(@"... command returned error: %d", Result);
    // FIXME: Raise proper error.
    abort();
  }
}

@implementation NinjaPerfTests

- (void)testLLVMOnlyNoExecuteInitialBuild {
  // Test the build of the llvm-only test file, with --no-execute.
  //
  // What we are measuring here is the time to write the initial from scratch
  // database.
  //
  // We use the N478 target which executes 266 commands.
  const char *TargetName = "N478";

  // Create a sandbox to run the test in.
  NSString *inputsDir = [@(SRCROOT)
                            stringByAppendingPathComponent:@"perftests/Inputs"];
  NSString *sandboxDir = [@(TEST_TEMPS_PATH)
                             stringByAppendingPathComponent:@"initial-build"];
  NSLog(@"executing test using inputs: %@", inputsDir);
  NSLog(@"executing test using sandbox: %@", sandboxDir);
  ExecuteShellCommand([NSString stringWithFormat:@"rm -rf \"%@\"",
                                sandboxDir].UTF8String);
  ExecuteShellCommand([NSString stringWithFormat:@"mkdir -p \"%@\"",
                                sandboxDir].UTF8String);
  NSString *ninjaPath = [sandboxDir
                          stringByAppendingPathComponent:@"build.ninja"];
  ExecuteShellCommand([NSString
                        stringWithFormat:@"cp \"%@\"/llvm-only.ninja \"%@\"",
                        inputsDir, ninjaPath].UTF8String);

  NSString *dbPath = [sandboxDir stringByAppendingPathComponent:@"t.db"];

  [self measureBlock:^{
      // For each iteration, remove the database file.
      ExecuteShellCommand([NSString stringWithFormat:@"rm -f \"%@\"",
                                    dbPath].UTF8String);
      
      llbuild::commands::ExecuteNinjaCommand({
          "build", "--quiet", "--no-execute",
            "--db", dbPath.UTF8String, ninjaPath.UTF8String, TargetName });
    }];
}

- (void)testLLVMOnlyNoExecuteNullBuild {
  // Test the build of an llvm-only test file, with --no-execute.
  //
  // What we are measuring here is the time to perform a null build after the
  // initial database has been constructed.
  //
  // We use the N478 target which executes 266 commands.
  const char *TargetName = "N478";

  // Create a sandbox to run the test in.
  NSString *inputsDir = [@(SRCROOT)
                            stringByAppendingPathComponent:@"perftests/Inputs"];
  NSString *sandboxDir = [@(TEST_TEMPS_PATH)
                             stringByAppendingPathComponent:@"initial-build"];
  NSLog(@"executing test using inputs: %@", inputsDir);
  NSLog(@"executing test using sandbox: %@", sandboxDir);
  ExecuteShellCommand([NSString stringWithFormat:@"rm -rf \"%@\"",
                                sandboxDir].UTF8String);
  ExecuteShellCommand([NSString stringWithFormat:@"mkdir -p \"%@\"",
                                sandboxDir].UTF8String);
  NSString *ninjaPath = [sandboxDir
                          stringByAppendingPathComponent:@"build.ninja"];
  ExecuteShellCommand([NSString
                        stringWithFormat:@"cp \"%@\"/llvm-only.ninja \"%@\"",
                        inputsDir, ninjaPath].UTF8String);

  NSString *dbPath = [sandboxDir stringByAppendingPathComponent:@"t.db"];

  // Build once to create a fresh initial database.
  printf("performing initial build...\n");
  ExecuteShellCommand([NSString stringWithFormat:@"rm -f \"%@\"",
                                dbPath].UTF8String);
  llbuild::commands::ExecuteNinjaCommand({
      "build", "--quiet", "--no-execute",
      "--db", dbPath.UTF8String, ninjaPath.UTF8String, TargetName });
    
  // Test the null build performance, each run of which will reuse the initial
  // database, but should not modify it other than to bump the iteration count.
  printf("performing null builds (performance test)...\n");
  [self measureBlock:^{
      llbuild::commands::ExecuteNinjaCommand({
          "build", "--quiet", "--no-execute",
          "--db", dbPath.UTF8String, ninjaPath.UTF8String, TargetName });
    }];
}

@end
