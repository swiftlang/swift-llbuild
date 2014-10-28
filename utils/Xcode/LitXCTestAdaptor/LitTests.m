//
//  llbuild_Tests.m
//  llbuild Tests
//
//  Copyright (c) 2014 Apple Inc. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <XCTest/XCTest.h>

#import <Python/Python.h>

@interface LitTests : XCTestCase

@end

@implementation LitTests

+ (XCTestSuite*)defaultTestSuite {
    // Initialize Python.
    Py_Initialize();

    // Extend the sys path to include the current directory.
    NSString *sourceDir = [@(__FILE__) stringByDeletingLastPathComponent];
    PyObject* sysPath = PySys_GetObject("path");
    PyObject* pySourceDir = PyString_FromString([sourceDir UTF8String]);
    PyList_Append(sysPath, pySourceDir);
    
    // Import our custom module, which will inject test methods.
    PyRun_SimpleString("import LitTests");
    
    // Invoke super class implementation, which will find all the methods we injected.
    return [super defaultTestSuite];
}

@end
