//===-- ShellUtility.cpp --------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/ShellUtility.h"
#include "llvm/ADT/SmallString.h"

namespace llbuild {
namespace basic {

void appendShellEscapedString(llvm::raw_ostream& os, StringRef string) {

  static const std::string whitelist = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-_/:@#%+=.,";
  auto pos = string.find_first_not_of(whitelist);

  // We don't need any escaping just append the string and return.
  if (pos == std::string::npos) {
    os << string;
    return;
  }

#if defined(_WIN32)
  std::string escQuote = "\"";
#else
  std::string escQuote = "'";
#endif
  // We only need to escape the single quote, if it isn't present we can
  // escape using single quotes.
  auto singleQuotePos = string.find_first_of(escQuote, pos);
  if (singleQuotePos == std::string::npos) {
    os << escQuote;
    os << string;
    os << escQuote;
    return;
  }

  // Otherwise iterate and escape all the single quotes.
  os << escQuote;
  os << string.slice(0, singleQuotePos);
  for (auto idx = singleQuotePos; idx < string.size(); idx++) {
    if (string[idx] == escQuote[0]) {
      os << escQuote << "\\" << escQuote << escQuote;
    } else {
      os << string[idx];
    }
  }
  os << escQuote;
}

std::string shellEscaped(StringRef string) {
  SmallString<16> out;
  llvm::raw_svector_ostream os(out);
  appendShellEscapedString(os, string);
  os.flush();
  return out.str();
}

}
}
