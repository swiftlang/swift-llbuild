//===-- CommandUtil.cpp ---------------------------------------------------===//
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

#include "CommandUtil.h"
#include "llbuild/Commands/Commands.h"

#include "llbuild/Basic/LLVM.h"
#include "llbuild/Ninja/ManifestLoader.h"
#include "llbuild/Ninja/Parser.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>

using namespace llbuild;
using namespace llbuild::commands;

static std::string programName;

void commands::setProgramName(StringRef name) {
  assert(programName.empty());
  programName = name;
}

const char* commands::getProgramName() {
  if (programName.empty())
    return nullptr;
  
  return programName.c_str();
}

static char hexdigit(unsigned input) {
  return (input < 10) ? '0' + input : 'A' + input - 10;
}

std::string util::escapedString(StringRef str) {
  std::string result;
  llvm::raw_string_ostream resultStream(result);
  for (unsigned i = 0; i < str.size(); ++i) {
    char c = str[i];
    if (c == '"') {
      resultStream << "\\\"";
    } else if (isprint(c)) {
      resultStream << c;
    } else if (c == '\n') {
      resultStream << "\\n";
    } else {
      resultStream << "\\x"
             << hexdigit(((unsigned char) c >> 4) & 0xF)
             << hexdigit((unsigned char) c & 0xF);
    }
  }
  resultStream.flush();
  return result;
}

static void emitError(StringRef filename, StringRef message,
                      const char* position, unsigned length,
                      int line, int column,
                      StringRef buffer) {
  assert(position >= buffer.begin() && position <= buffer.end() &&
         "invalid position");
  assert(position + length <= buffer.end() && "invalid length");

  // Compute the line and column, if not provided.
  //
  // FIXME: This is not very efficient, if there are a lot of diagnostics.
  if (line == -1) {
    line = 1;
    column = 0;
    for (const char *c = buffer.begin(); c != position; ++c) {
      if (*c == '\n') {
        ++line;
        column = 0;
      } else {
        ++column;
      }
    }
  }

  std::cerr.write(filename.data(), filename.size());
  std::cerr << ":" << line << ":" << column << ": error: ";
  std::cerr.write(message.data(), message.size());
  std::cerr << "\n";

  // Skip carat diagnostics on EOF token.
  if (position == buffer.end())
    return;

  // Simple caret style diagnostics.
  const char *lineBegin = position, *lineEnd = position;
  const char *bufferBegin = buffer.begin(), *bufferEnd = buffer.end();

  // Run line pointers forward and back.
  while (lineBegin > bufferBegin &&
         lineBegin[-1] != '\r' && lineBegin[-1] != '\n')
    --lineBegin;
  while (lineEnd < bufferEnd &&
         lineEnd[0] != '\r' && lineEnd[0] != '\n')
    ++lineEnd;

  // Show the line, indented by 2.
  std::cerr << "  " << std::string(lineBegin, lineEnd) << "\n";

  // Show the caret or squiggly, making sure to print back spaces the same.
  std::cerr << "  ";
  for (const char* s = lineBegin; s != position; ++s)
    std::cerr << (isspace(*s) ? *s : ' ');
  if (length > 1) {
    for (unsigned i = 0; i != length; ++i)
      std::cerr << '~';
  } else {
    std::cerr << '^';
  }
  std::cerr << '\n';
}

void util::emitError(StringRef filename, StringRef message,
                     const ninja::Token& at, const ninja::Parser* parser) {
  ::emitError(filename, message, at.start, at.length, at.line, at.column,
            parser->getLexer().getBuffer());
}

void util::emitError(StringRef filename, StringRef message,
                     const char* position, unsigned length,
                     StringRef buffer) {
  ::emitError(filename, message, position, length, -1, -1, buffer);
}

llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>> util::readFileContents(
    StringRef filename) {
  SmallString<256> path(filename);
  llvm::sys::fs::make_absolute(path);

  auto bufferOrError = llvm::MemoryBuffer::getFile(path);
  if (!bufferOrError) {
    auto ec = bufferOrError.getError();
    return llvm::make_error<llvm::StringError>(
        Twine("unable to read input \"") + util::escapedString(path) +
        "\" (" + ec.message() + ")", ec);
  }
  return std::move(*bufferOrError);
}
