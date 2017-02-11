//===- BinaryCoding.h -------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_BINARYCODING_H
#define LLBUILD_BASIC_BINARYCODING_H

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <vector>

namespace llbuild {
namespace basic {

template<typename T>
struct BinaryCodingTraits {
  // static inline void encode(const T&, BinaryEncoder&);
  // static inline void decode(T&, BinaryDecoder&);
};

/// A basic binary encoding utility.
///
/// This encoder is design for small, relatively efficient, in-memory coding of
/// objects. It is endian-neutral, and should be paired with \see BinaryDecoder
/// for decoding. The specific encoding is not intended to be stable.
///
/// The utility supports coding of user-defined types via specialization of the
/// BinaryCodeableTraits type.
class BinaryEncoder {
private:
   // Copying is disabled.
   BinaryEncoder(const BinaryEncoder&) LLBUILD_DELETED_FUNCTION;
   void operator=(const BinaryEncoder&) LLBUILD_DELETED_FUNCTION;

  /// The encoded data.
  //
  // FIXME: Parameterize this size?
  llvm::SmallVector<uint8_t, 256> data;

public:
  /// Construct a new binary encoder.
  BinaryEncoder() {}

  /// Encode a value to the stream.
  void write(uint8_t value) {
    data.push_back(value);
  }

  /// Encode a value to the stream.
  void write(uint16_t value) {
    write(uint8_t(value >> 0));
    write(uint8_t(value >> 8));
  }

  /// Encode a value to the stream.
  void write(uint32_t value) {
    write(uint16_t(value >> 0));
    write(uint16_t(value >> 16));
  }

  /// Encode a value to the stream.
  void write(uint64_t value) {
    write(uint32_t(value >> 0));
    write(uint32_t(value >> 32));
  }

  /// Encode a sequence of bytes to the stream.
  void writeBytes(StringRef bytes) {
    data.insert(data.end(), bytes.begin(), bytes.end());
  }

  /// Encode a value to the stream.
  template<typename T>
  void write(T value) {
    BinaryCodingTraits<T>::encode(value, *this);
  }

  /// Get the encoded binary data.
  std::vector<uint8_t> contents() {
    return std::vector<uint8_t>(data.begin(), data.end());
  }
};
  
/// A basic binary decoding utility.
///
/// \see BinaryEncoder.
class BinaryDecoder {
private:
   // Copying is disabled.
   BinaryDecoder(const BinaryDecoder&) LLBUILD_DELETED_FUNCTION;
   void operator=(const BinaryDecoder&) LLBUILD_DELETED_FUNCTION;
  
  /// The data being decoded.
  StringRef data;

  /// The current position in the stream.
  uint64_t pos = 0;

  uint8_t read8() { return data[pos++]; }
  uint16_t read16() {
    uint16_t result = read8();
    result |= uint16_t(read8()) << 8;
    return result;
  }
  uint32_t read32() {
    uint32_t result = read16();
    result |= uint32_t(read16()) << 16;
    return result;
  }
  uint64_t read64() {
    uint64_t result = read32();
    result |= uint64_t(read32()) << 32;
    return result;
  }
  
public:
  /// Construct a binary decoder. 
  BinaryDecoder(StringRef data) : data(data) {}
  
  /// Construct a binary decoder.
  ///
  /// NOTE: The input data is supplied by reference, and its lifetime must
  /// exceed that of the decoder.
  BinaryDecoder(const std::vector<uint8_t>& data) : BinaryDecoder(
      StringRef(reinterpret_cast<const char*>(data.data()), data.size())) {}

  /// Check if the decoder is at the end of the stream.
  bool isEmpty() const {
    return pos == data.size();
  }
  
  /// Decode a value from the stream.
  void read(uint8_t& value) { value = read8(); }
  
  /// Decode a value from the stream.
  void read(uint16_t& value) { value = read16(); }

  /// Decode a value from the stream.
  void read(uint32_t& value) { value = read32(); }

  /// Decode a value from the stream.
  void read(uint64_t& value) { value = read64(); }

  /// Decode a byte string from the stream.
  ///
  /// NOTE: The return value points into the decode stream, and must be copied
  /// by clients if it is to last longer than the lifetime of the decoder.
  void readBytes(size_t count, StringRef& value) {
    assert(pos + count <= data.size());
    value = StringRef(data.begin() + pos, count);
    pos += count;
  }

  /// Decode a value from the stream.
  template<typename T>
  void read(T& value) {
    BinaryCodingTraits<T>::decode(value, *this);
  }

  /// Finish decoding and clean up.
  void finish() {
    assert(isEmpty());
  }
};

}
}

#endif
