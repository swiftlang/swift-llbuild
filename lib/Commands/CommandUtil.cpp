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

#include <iostream>
#include <sstream>

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

std::string util::escapedString(const char* start, unsigned length) {
  std::stringstream result;
  for (unsigned i = 0; i != length; ++i) {
    char c = start[i];
    if (c == '"') {
      result << "\\\"";
    } else if (isprint(c)) {
      result << c;
    } else if (c == '\n') {
      result << "\\n";
    } else {
      result << "\\x"
             << hexdigit(((unsigned char) c >> 4) & 0xF)
             << hexdigit((unsigned char) c & 0xF);
    }
  }
  return result.str();
}
std::string util::escapedString(const std::string& string) {
  return escapedString(string.data(), string.size());
}

static void emitError(const std::string& filename, const std::string& message,
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
  
  std::cerr << filename << ":" << line << ":" << column
            << ": error: " << message << "\n";

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

void util::emitError(const std::string& filename, const std::string& message,
                     const ninja::Token& at, const ninja::Parser* parser) {
  ::emitError(filename, message, at.start, at.length, at.line, at.column,
            parser->getLexer().getBuffer());
}

void util::emitError(const std::string& filename, const std::string& message,
                     const char* position, unsigned length,
                     StringRef buffer) {
  ::emitError(filename, message, position, length, -1, -1, buffer);
}

bool util::readFileContents(std::string path,
                            std::unique_ptr<char[]> *data_out,
                            uint64_t* size_out,
                            std::string* error_out) {
  // Open the input buffer and compute its size.
  FILE* fp = ::fopen(path.c_str(), "rb");
  if (!fp) {
    int errorNumber = errno;
    *error_out = std::string("unable to open input: \"") +
      util::escapedString(path) + "\" (" + ::strerror(errorNumber) + ")";
    return false;
  }

  fseek(fp, 0, SEEK_END);
  uint64_t size = *size_out = ::ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // Read the file contents.
  auto data = llvm::make_unique<char[]>(size);
  uint64_t pos = 0;
  while (pos < size) {
    // Read data into the buffer.
    size_t result = ::fread(data.get() + pos, 1, size - pos, fp);
    if (result <= 0) {
      *error_out = std::string("unable to read input: ") + path;
      return false;
    }

    pos += result;
  }

  // Close the file.
  ::fclose(fp);

  *data_out = std::move(data);
  return true;
}
