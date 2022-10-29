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

using namespace llvm;
namespace llbuild {
namespace basic {


#if defined(_WIN32)
std::string formatWindowsCommandArg(StringRef string) {
  // Windows escaping, adapted from Daniel Colascione's "Everyone quotes
  // command line arguments the wrong way" - Microsoft Developer Blog
  const std::string needsQuote = " \t\n\v\"";
  if (string.find_first_of(needsQuote) == std::string::npos) {
    return string;
  }

  // To escape the command line, we surround the argument with quotes. However
  // the complication comes due to how the Windows command line parser treats
  // backslashes (\) and quotes (")
  //
  // - \ is normally treated as a literal backslash
  //     - e.g. foo\bar\baz => foo\bar\baz
  // - However, the sequence \" is treated as a literal "
  //     - e.g. foo\"bar => foo"bar
  //
  // But then what if we are given a path that ends with a \? Surrounding
  // foo\bar\ with " would be "foo\bar\" which would be an unterminated string
  // since it ends on a literal quote. To allow this case the parser treats:
  //
  // - \\" as \ followed by the " metachar
  // - \\\" as \ followed by a literal "
  // - In general:
  //     - 2n \ followed by " => n \ followed by the " metachar
  //     - 2n+1 \ followed by " => n \ followed by a literal "
  std::string escaped = "\"";
  for (auto i = std::begin(string); i != std::end(string); ++i) {
    int numBackslashes = 0;
    while (i != string.end() && *i == '\\') {
      ++i;
      ++numBackslashes;
    }

    if (i == string.end()) {
      // String ends with a backslash e.g. foo\bar\, escape all the backslashes
      // then add the metachar " below
      escaped.append(numBackslashes * 2, '\\');
      break;
    } else if (*i == '"') {
      // This is  a string of \ followed by a " e.g. foo\"bar. Escape the
      // backslashes and the quote
      escaped.append(numBackslashes * 2 + 1, '\\');
      escaped.push_back(*i);
    } else {
      // These are just literal backslashes
      escaped.append(numBackslashes, '\\');
      escaped.push_back(*i);
    }
  }
  escaped.push_back('"');

  return escaped;
}

std::string formatWindowsCommandString(std::vector<std::string> args) {
    std::string commandLine;
    for (auto& arg : args)
        commandLine += formatWindowsCommandArg(arg) + " ";
    if (commandLine.size())
      commandLine.pop_back();
    return commandLine;
}
#endif

void appendShellEscapedString(llvm::raw_ostream& os, StringRef string) {
#if defined(_WIN32)
  os << formatWindowsCommandArg(string);
  return;
#else
  static const std::string whitelist = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-_/:@#%+=.,";
  auto pos = string.find_first_not_of(whitelist);

  // We don't need any escaping just append the string and return.
  if (pos == std::string::npos) {
    os << string;
    return;
  }

  std::string escQuote = "'";
  // We only need to escape the single quote, if it isn't present we can
  // escape using single quotes.
  auto singleQuotePos = string.find_first_of("'" , pos);
  if (singleQuotePos == std::string::npos) {
    os << "'" << string << "'";
    return;
  }

  // Otherwise iterate and escape all the single quotes.
  os << "'";
  os << string.slice(0, singleQuotePos);
  for (auto idx = singleQuotePos; idx < string.size(); idx++) {
    if (string[idx] == '\'') {
      os << "'\\''";
    } else {
      os << string[idx];
    }
  }
  os << "'";
#endif
}

std::string shellEscaped(StringRef string) {
  SmallString<16> out;
  llvm::raw_svector_ostream os(out);
  appendShellEscapedString(os, string);
  return out.str().str();
}

}
}
