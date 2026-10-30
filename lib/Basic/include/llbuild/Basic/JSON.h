//===- JSON.h ---------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_JSON_H
#define LLBUILD_BASIC_JSON_H

#include <iomanip>
#include <string>
#include <sstream>

namespace llbuild {
namespace basic {

  template <typename String>
  std::string escapeForJSON(String const& in) {
    std::ostringstream ss;
    for (auto i = in.begin(); i != in.end(); i++) {
      switch (*i) {
      case '\\': ss << "\\\\"; break;
      case '"': ss << "\\\""; break;
      case '\b': ss << "\\b"; break;
      case '\n': ss << "\\n"; break;
      case '\f': ss << "\\f"; break;
      case '\r': ss << "\\r"; break;
      case '\t': ss << "\\t"; break;
      default:
        if ('\x00' <= *i && *i <= '\x1f') {
          ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
             << (int)*i;
        } else {
          ss << *i;
        }
      }
    }
    return ss.str();
  }

}
}

#endif
