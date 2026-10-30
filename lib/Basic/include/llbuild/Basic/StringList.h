//===- StringList.h ---------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_STRINGLIST_H
#define LLBUILD_BASIC_STRINGLIST_H

#include "llbuild/Basic/BinaryCoding.h"

#include <vector>

namespace llbuild {
namespace basic {

/// A list of strings particularly suited for use in binary coding
class StringList {
private:
  /// The values are packed as a sequence of C strings.
  char* contents = nullptr;

  /// The total length of the contents.
  uint64_t size = 0;

public:
  StringList() {}
  StringList(basic::BinaryDecoder& decoder) {
    decoder.read(size);
    StringRef contents;
    decoder.readBytes(size, contents);
    this->contents = new char[size];
    memcpy(this->contents, contents.data(), contents.size());
  }
  ~StringList() {
    if (contents != nullptr)
      delete [] contents;
  }

  explicit StringList(StringRef value) {
    size = value.size() + 1;
    contents = new char[size];
    assert(value.find('\0') == StringRef::npos);
    memcpy(contents, value.data(), value.size());
    contents[size - 1] = '\0';
  }

  template<class StringType>
  explicit StringList(const ArrayRef<StringType> values) {
    // Construct the concatenated data.
    for (auto value: values) {
      size += value.size() + 1;
    }
    // Make sure to allocate at least 1 byte.
    char* p = nullptr;
    contents = p = new char[size + 1];
    for (auto value: values) {
      assert(value.find('\0') == StringRef::npos);
      memcpy(p, value.data(), value.size());
      p += value.size();
      *p++ = '\0';
    }
    *p = '\0';
  }

  // StringList can only be moved, not copied
  StringList(StringList&& rhs) : contents(rhs.contents), size(rhs.size) {
    rhs.size = 0;
    rhs.contents = nullptr;
  }

  StringList& operator=(StringList&& rhs) {
    if (this != &rhs) {
      size = rhs.size;
      if (contents != nullptr)
        delete [] contents;
      contents = rhs.contents;
      rhs.contents = nullptr;
    }
    return *this;
  }

  std::vector<StringRef> getValues() const {
    std::vector<StringRef> result;
    for (uint64_t i = 0; i < size;) {
      auto value = StringRef(&contents[i]);
      assert(i + value.size() <= size);
      result.push_back(value);
      i += value.size() + 1;
    }
    return result;
  }

  bool isEmpty() const { return size == 0; }

  void encode(BinaryEncoder& coder) const {
    coder.write(size);
    coder.writeBytes(StringRef(contents, size));
  }
};

template<>
struct BinaryCodingTraits<StringList> {
  static inline void encode(const StringList& value, BinaryEncoder& coder) {
    value.encode(coder);
  }

  static inline void decode(StringList& value, BinaryDecoder& coder) {
    value = StringList(coder);
  }
};

}
}

#endif /* StringList_h */

