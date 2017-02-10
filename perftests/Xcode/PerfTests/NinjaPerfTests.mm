//===-- NinjaPerfTests.mm -------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2015 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
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

- (void)testChromiumFakeManifestLoading {
    // Test the Ninja parsing/loading time for the Chromium fake manifest.
    
    // Create a sandbox to run the test in.
    NSString *inputsDir = [@(SRCROOT)
                           stringByAppendingPathComponent:@"perftests/Inputs"];
    NSString *sandboxDir = [@(TEST_TEMPS_PATH)
                            stringByAppendingPathComponent:@"ChromiumFakeManifestLoading"];
    NSLog(@"executing test using inputs: %@", inputsDir);
    NSLog(@"executing test using sandbox: %@", sandboxDir);
    ExecuteShellCommand([NSString stringWithFormat:@"rm -rf \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString stringWithFormat:@"mkdir -p \"%@\"",
                         sandboxDir].UTF8String);
    NSString *ninjaPath = [sandboxDir stringByAppendingPathComponent:@"build.ninja"];
    ExecuteShellCommand([NSString
                         stringWithFormat:@"cp \"%@\"/chromium-fake-manifest.ninja.gz \"%@.gz\"",
                         inputsDir, ninjaPath].UTF8String);
    ExecuteShellCommand([NSString
                         stringWithFormat:@"gzip -d \"%@.gz\"", ninjaPath].UTF8String);
    
    printf("performing load-manifest-only test...\n");
    [self measureBlock:^{
        llbuild::commands::executeNinjaCommand({
            "load-manifest-only", ninjaPath.UTF8String });
    }];
}

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
                            stringByAppendingPathComponent:@"LLVMOnlyNoExecuteInitialBuild"];
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
        
        llbuild::commands::executeNinjaCommand({
            "build", "--quiet", "--jobs", "1", "--simulate",
            "--db", dbPath.UTF8String, "-f", ninjaPath.UTF8String, TargetName });
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
                            stringByAppendingPathComponent:@"LLVMOnlyNoExecuteNullBuild"];
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
    llbuild::commands::executeNinjaCommand({
        "build", "--quiet", "--jobs", "1", "--simulate",
        "--db", dbPath.UTF8String, "-f", ninjaPath.UTF8String, TargetName });
    
    // Test the null build performance, each run of which will reuse the initial
    // database, but should not modify it other than to bump the iteration count.
    printf("performing null builds (performance test)...\n");
    [self measureBlock:^{
        for (int i = 0; i != 10; ++i) {
            llbuild::commands::executeNinjaCommand({
                "build", "--quiet", "--jobs", "1", "--simulate",
                "--db", dbPath.UTF8String, "-f", ninjaPath.UTF8String, TargetName });
        }
    }];
}

- (void)testPseudoLLVMParallelFullBuild {
    // Test the pseudo LLVM build, which includes the time to do the actual
    // stat'ing and dependency checking of files, and in particular includes all
    // the overhead of the database.
    
    // Create a sandbox to run the test in.
    NSString *inputsDir = [@(SRCROOT)
                           stringByAppendingPathComponent:@"perftests/Inputs"];
    NSString *sandboxDir = [@(TEST_TEMPS_PATH)
                            stringByAppendingPathComponent:@"PseudoLLVMParallelFullBuild"];
    NSLog(@"executing test using inputs: %@", inputsDir);
    NSLog(@"executing test using sandbox: %@", sandboxDir);
    ExecuteShellCommand([NSString stringWithFormat:@"rm -rf \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString stringWithFormat:@"mkdir -p \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString
                         stringWithFormat:@"tar -C \"%@\" -xf \"%@\"/pseudo-llvm.tgz",
                         sandboxDir, inputsDir].UTF8String);
    
    // Build once to prime the tree.
    printf("performing initial build...\n");
    NSString *pseudoLLVMPath = [sandboxDir stringByAppendingPathComponent:@"pseudo-llvm"];
    NSString *dbPath = [pseudoLLVMPath stringByAppendingPathComponent:@"build.db"];
    llbuild::commands::executeNinjaCommand({
        "build", "--quiet", "-C", pseudoLLVMPath.UTF8String, "all" });
    
    // Test the null build performance, each run of which will reuse the initial
    // database, but should not modify it other than to bump the iteration count.
    printf("performing full builds (performance test)...\n");
    [self measureBlock:^{
        // For each iteration, remove the database file.
        ExecuteShellCommand([NSString stringWithFormat:@"rm -f \"%@\"",
                             dbPath].UTF8String);
        
        llbuild::commands::executeNinjaCommand({
            "build", "--quiet", "-C", pseudoLLVMPath.UTF8String, "all" });
    }];
}

- (void)testPseudoLLVMParallelFullBuildWithNinja {
    // Test the pseudo LLVM build using the actual Ninja tool, so we can easily
    // compare the performance.
    
    // Create a sandbox to run the test in.
    NSString *inputsDir = [@(SRCROOT)
                           stringByAppendingPathComponent:@"perftests/Inputs"];
    NSString *sandboxDir = [@(TEST_TEMPS_PATH)
                            stringByAppendingPathComponent:@"PseudoLLVMParallelFullBuildWithNinja"];
    NSLog(@"executing test using inputs: %@", inputsDir);
    NSLog(@"executing test using sandbox: %@", sandboxDir);
    ExecuteShellCommand([NSString stringWithFormat:@"rm -rf \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString stringWithFormat:@"mkdir -p \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString
                         stringWithFormat:@"tar -C \"%@\" -xf \"%@\"/pseudo-llvm.tgz",
                         sandboxDir, inputsDir].UTF8String);
    
    // Build once to prime the tree.
    printf("performing initial build...\n");
    NSString *pseudoLLVMPath = [sandboxDir stringByAppendingPathComponent:@"pseudo-llvm"];
    NSString *dbPath = [pseudoLLVMPath stringByAppendingPathComponent:@".ninja_log"];
    NSString *ninjaPath = [@(SRCROOT) stringByAppendingPathComponent:@"llbuild-test-tools/utils/Xcode/ninja"];
    // NOTE: We have to pipe to /dev/null because Ninja has no -q (Ninja #480).
    ExecuteShellCommand([NSString stringWithFormat:@"%@  -C \"%@\" all > /dev/null",
                         ninjaPath, pseudoLLVMPath].UTF8String);
    
    // Test the null build performance.
    printf("performing full builds (performance test)...\n");
    [self measureBlock:^{
        // For each iteration, make clean and remove the database file.
        ExecuteShellCommand([NSString stringWithFormat:@"%@ -C \"%@\" -t clean",
                             ninjaPath, pseudoLLVMPath].UTF8String);
        ExecuteShellCommand([NSString stringWithFormat:@"rm -f \"%@\"",
                             dbPath].UTF8String);
        
        ExecuteShellCommand([NSString stringWithFormat:@"%@ -C \"%@\" all > /dev/null",
                             ninjaPath, pseudoLLVMPath].UTF8String);
    }];
}

- (void)testPseudoLLVMNullBuild {
    // Test the pseudo LLVM build, which includes the time to do the actual
    // stat'ing and dependency checking of files, and in particular includes all
    // the overhead of the database.
    
    // Create a sandbox to run the test in.
    NSString *inputsDir = [@(SRCROOT)
                           stringByAppendingPathComponent:@"perftests/Inputs"];
    NSString *sandboxDir = [@(TEST_TEMPS_PATH)
                            stringByAppendingPathComponent:@"PseudoLLVMNullBuild"];
    NSLog(@"executing test using inputs: %@", inputsDir);
    NSLog(@"executing test using sandbox: %@", sandboxDir);
    ExecuteShellCommand([NSString stringWithFormat:@"rm -rf \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString stringWithFormat:@"mkdir -p \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString
                         stringWithFormat:@"tar -C \"%@\" -xf \"%@\"/pseudo-llvm.tgz",
                         sandboxDir, inputsDir].UTF8String);
    
    // Build once to create a fresh initial database.
    printf("performing initial build...\n");
    NSString *pseudoLLVMPath = [sandboxDir stringByAppendingPathComponent:@"pseudo-llvm"];
    llbuild::commands::executeNinjaCommand({
        "build", "--quiet", "-C", pseudoLLVMPath.UTF8String, "all" });
    
    // Test the null build performance, each run of which will reuse the initial
    // database, but should not modify it other than to bump the iteration count.
    printf("performing null builds (performance test)...\n");
    [self measureBlock:^{
        llbuild::commands::executeNinjaCommand({
            "build", "--quiet", "-C", pseudoLLVMPath.UTF8String, "--jobs", "1", "all" });
    }];
}

- (void)testPseudoLLVMNullBuildWithNinja {
    // Test the pseudo LLVM build using the actual Ninja tool, so we can easily
    // compare the performance.
    
    // Create a sandbox to run the test in.
    NSString *inputsDir = [@(SRCROOT)
                           stringByAppendingPathComponent:@"perftests/Inputs"];
    NSString *sandboxDir = [@(TEST_TEMPS_PATH)
                            stringByAppendingPathComponent:@"PseudoLLVMNullBuildWithNinja"];
    NSLog(@"executing test using inputs: %@", inputsDir);
    NSLog(@"executing test using sandbox: %@", sandboxDir);
    ExecuteShellCommand([NSString stringWithFormat:@"rm -rf \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString stringWithFormat:@"mkdir -p \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString
                         stringWithFormat:@"tar -C \"%@\" -xf \"%@\"/pseudo-llvm.tgz",
                         sandboxDir, inputsDir].UTF8String);
    
    // Build once to initialize the database.
    printf("performing initial build...\n");
    NSString *pseudoLLVMPath = [sandboxDir stringByAppendingPathComponent:@"pseudo-llvm"];
    NSString *ninjaPath = [@(SRCROOT) stringByAppendingPathComponent:@"llbuild-test-tools/utils/Xcode/ninja"];
    // NOTE: We have to pipe to /dev/null because Ninja has no -q (Ninja #480).
    ExecuteShellCommand([NSString stringWithFormat:@"%@ -C \"%@\" > /dev/null",
                            ninjaPath, pseudoLLVMPath].UTF8String);
    
    // Test the null build performance.
    printf("performing null builds (performance test)...\n");
    [self measureBlock:^{
        ExecuteShellCommand([NSString stringWithFormat:@"%@ -j1 -C \"%@\" > /dev/null",
                             ninjaPath, pseudoLLVMPath].UTF8String);
        
    }];
}

@end
