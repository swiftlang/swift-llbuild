//===- BinaryCodingPerfTests.cpp ------------------------------------------===//
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

#import "llbuild/Basic/BinaryCoding.h"

#import <XCTest/XCTest.h>

using namespace llbuild::basic;

@interface BinaryCodingPerfTests : XCTestCase

@end

@implementation BinaryCodingPerfTests

/// Check encoding 100MB of uint64_ts.
- (void)testEncoding_UInt64_100MB {
    [self measureBlock:^{
        // We do 100 iterations to sum to 100 MBs.
        for (int j = 0; j != 100; ++j) {
            BinaryEncoder coder;
            // Encode 8 64-bit values.
            for (auto i = 0; i != (1 << 20) / 8; ++i) {
                coder.write(uint64_t(0xAABBCCDDAABBCCDDULL));
            }
            auto result = coder.contents();
            XCTAssertEqual(result.size(), 1 << 20);
        }
    }];
}

/// Check decoding 1000MB of uint64_ts.
- (void)testDecoding_UInt64_1000MB {
    // Write the data.
    BinaryEncoder coder;
    // Encode 8 64-bit values.
    for (auto i = 0; i != (1 << 20) / 8; ++i) {
        coder.write(uint64_t(0xAABBCCDDAABBCCDDULL ^ i));
    }
    auto data = coder.contents();
    XCTAssertEqual(data.size(), 1 << 20);
    
    [self measureBlock:^{
        // We do 1000 iterations to sum to 100 MBs.
        for (int j = 0; j != 1000; ++j) {
            BinaryDecoder decoder(data);
            for (auto i = 0; i != (1 << 20) / 8; ++i) {
                uint64_t value;
                decoder.read(value);
                if (value != (0xAABBCCDDAABBCCDDULL ^ i)) abort();
            }
            decoder.finish();
        }
    }];
}

@end
