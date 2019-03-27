//===-- BuildSystemPerfTests.mm -------------------------------------------===//
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

#import "llbuild/Basic/Subprocess.h"
#import "llbuild/Commands/Commands.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#import <XCTest/XCTest.h>

extern "C" {
    // Provided by System.framework's libsystem_kernel interface
    extern int __pthread_fchdir(int fd);
}

using namespace llbuild;
using namespace llbuild::basic;

@interface BuildSystemPerfTests : XCTestCase

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

@implementation BuildSystemPerfTests

- (void)testChromiumFakeBuildFileLoading {
    // Test the build file parsing/loading time for the Chromium fake build file.
    
    // Create a sandbox to run the test in.
    NSString *inputsDir = [@(SRCROOT)
                           stringByAppendingPathComponent:@"perftests/Inputs"];
    NSString *sandboxDir = [@(TEST_TEMPS_PATH)
                            stringByAppendingPathComponent:@"ChromiumFakeBuildFileLoading"];
    NSLog(@"executing test using inputs: %@", inputsDir);
    NSLog(@"executing test using sandbox: %@", sandboxDir);
    ExecuteShellCommand([NSString stringWithFormat:@"rm -rf \"%@\"",
                         sandboxDir].UTF8String);
    ExecuteShellCommand([NSString stringWithFormat:@"mkdir -p \"%@\"",
                         sandboxDir].UTF8String);
    NSString *buildFilePath = [sandboxDir
                               stringByAppendingPathComponent:@"chromium-fake-manifest.llbuild"];
    ExecuteShellCommand([NSString
                         stringWithFormat:@"cp \"%@\"/chromium-fake-manifest.llbuild.gz \"%@.gz\"",
                         inputsDir, buildFilePath].UTF8String);
    ExecuteShellCommand([NSString
                         stringWithFormat:@"gzip -d \"%@.gz\"", buildFilePath].UTF8String);
    
    printf("performing parse --no-output test...\n");
    [self measureBlock:^{
        llbuild::commands::executeBuildSystemCommand({
            "parse", "--no-output", buildFilePath.UTF8String });
    }];
}


class PerfTestProcessDelegate : public ProcessDelegate {
    void processStarted(ProcessContext*, ProcessHandle) {}
    void processHadError(ProcessContext*, ProcessHandle, const Twine&) {}
    void processHadOutput(ProcessContext*, ProcessHandle, StringRef) {}
    void processFinished(ProcessContext*, ProcessHandle, const ProcessResult&) {}
};


- (void)testSupprocessSpawn {

    [self measureBlock:^{
        PerfTestProcessDelegate delegate;
        ProcessAttributes attr{true};
        ProcessGroup pgrp;
        ProcessHandle handle{0};
        std::vector<StringRef> cmd({"/usr/bin/true"});
        POSIXEnvironment environment;

        for (int i = 0; i < 200; i++) {
            ProcessReleaseFn releaseFn = [](std::function<void()>&& pwait){ pwait(); };
            ProcessCompletionFn completionFn = [](ProcessResult){};
            spawnProcess(delegate, nullptr, pgrp, handle, cmd, environment, attr, std::move(releaseFn), std::move(completionFn));
        }
    }];
}

- (void)testSupprocessSpawnWorkingDirectory {

    [self measureBlock:^{
        PerfTestProcessDelegate delegate;
        ProcessAttributes attr{true, "/tmp"};
        ProcessGroup pgrp;
        ProcessHandle handle{0};
        std::vector<StringRef> cmd({"/usr/bin/true"});
        POSIXEnvironment environment;

        for (int i = 0; i < 200; i++) {
            ProcessReleaseFn releaseFn = [](std::function<void()>&& pwait){ pwait(); };
            ProcessCompletionFn completionFn = [](ProcessResult){};
            spawnProcess(delegate, nullptr, pgrp, handle, cmd, environment, attr, std::move(releaseFn), std::move(completionFn));
        }
    }];

    // Reset (remove) per-thread working directory in case it was set by the
    // above process spawning.
    __pthread_fchdir(-1);
}


@end
