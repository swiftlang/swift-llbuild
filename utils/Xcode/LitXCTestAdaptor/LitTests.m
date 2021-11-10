//
//  llbuild_Tests.m
//  llbuild Tests
//
//  Copyright (c) 2014 Apple Inc. All rights reserved.
//

#import <XCTest/XCTest.h>

#import <TargetConditionals.h>
#if TARGET_OS_OSX
#import <Python/Python.h>
#endif

@interface LitTests : XCTestCase

@end

@implementation LitTests

+ (XCTestSuite*)defaultTestSuite {
    // Inject the BUILT_PRODUCTS_DIR we were built with into the environment.
    setenv("BUILT_PRODUCTS_DIR", BUILT_PRODUCTS_DIR, /*overwrite=*/1);

#if TARGET_OS_OSX
    // Initialize Python.
    Py_Initialize();

    // Extend the sys path to include the current directory.
    NSString *sourceDir = [@(__FILE__) stringByDeletingLastPathComponent];
    PyObject* sysPath = PySys_GetObject("path");
    PyObject* pySourceDir = PyString_FromString([sourceDir UTF8String]);
    PyList_Append(sysPath, pySourceDir);
    
    // Import our custom module, which will inject test methods.
    PyRun_SimpleString("import LitTests");
#endif
    
    // Invoke super class implementation, which will find all the methods we injected.
    return [super defaultTestSuite];
}

@end
