//===- BinaryCodingTests.cpp ----------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/BinaryCoding.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::basic;

struct CustomType {
  friend struct BinaryCodingTraits<CustomType>;
  
  uint32_t a;
  uint32_t b;
};

inline bool operator==(CustomType lhs, CustomType rhs) {
  return lhs.a == rhs.a && lhs.b == rhs.b;
}

template<>
struct BinaryCodingTraits<CustomType> {
  static inline void encode(const CustomType& value,
                            BinaryEncoder& coder) {
    coder.write(value.a);
    coder.write(value.b);
  }
  static inline void decode(CustomType& value, BinaryDecoder& coder) {
    coder.read(value.a);
    coder.read(value.b);
  }
};

namespace {

template<typename T>
static std::vector<uint8_t> encode(T value) {
  BinaryEncoder encoder;
  encoder.write(value);
  return encoder.contents();
}

template<typename T>
static T decode(std::vector<uint8_t> data) {
  BinaryDecoder decoder(data);
  // FIXME: Don't do default initialization.
  T result;
  decoder.read(result);
  return result;
}

template<typename T>
void checkRoundtrip(T value) {
  // Equal types should encode equal.
  auto data = encode(value);
  auto dataCopy = encode(T(value));
  EXPECT_EQ(data, dataCopy);

  // Check decoding.
  auto decodedValue = decode<T>(data);
  auto decodedValueCopy = decode<T>(dataCopy);
  EXPECT_EQ(decodedValue, decodedValueCopy);

  // Check roundtrip.
  EXPECT_EQ(decodedValue, value);
}

TEST(BinaryCodingTests, basic) {
  // Check the coding of basic types.
  checkRoundtrip(uint8_t(0xAB));
  checkRoundtrip(uint16_t(0xABCD));
  checkRoundtrip(uint32_t(0xABCD0123));
  checkRoundtrip(uint64_t(0xABCD01234567DCBAULL));
}

TEST(BinaryCodingTests, bytes) {
  BinaryEncoder encoder;
  encoder.writeBytes(StringRef("hello"));
  encoder.writeBytes(StringRef("world"));
  auto result = encoder.contents();

  // Check the raw encoding.
  EXPECT_EQ(StringRef((char*)result.data(), result.size()),
            StringRef("helloworld"));

  // Check the decode.
  BinaryDecoder decoder(result);
  StringRef s1, s2;
  decoder.readBytes(5, s1);
  decoder.readBytes(5, s2);
  EXPECT_EQ(s1, StringRef("hello"));
  EXPECT_EQ(s2, StringRef("world"));
}

TEST(BinaryCodingTests, customType) {
  // Check the coding of basic types.
  checkRoundtrip(CustomType{ 0xABCD, 0x1234 });
}

}
