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

#include "llbuild/Ninja/ManifestLoader.h"
#include "llbuild/Ninja/Parser.h"

#include <iostream>
#include <sstream>

using namespace llbuild;
using namespace llbuild::commands;

static char hexdigit(unsigned Input) {
  return (Input < 10) ? '0' + Input : 'A' + Input - 10;
}

std::string util::EscapedString(const char *Start, unsigned Length) {
  std::stringstream Result;
  for (unsigned i = 0; i != Length; ++i) {
    char c = Start[i];
    if (c == '"') {
      Result << "\\\"";
    } else if (isprint(c)) {
      Result << c;
    } else if (c == '\n') {
      Result << "\\n";
    } else {
      Result << "\\x"
             << hexdigit(((unsigned char) c >> 4) & 0xF)
             << hexdigit((unsigned char) c & 0xF);
    }
  }
  return Result.str();
}
std::string util::EscapedString(const std::string& String) {
  return EscapedString(String.data(), String.size());
}

void util::EmitError(const std::string& Filename, const std::string& Message,
                     const ninja::Token& At, const ninja::Parser* Parser) {
  std::cerr << Filename << ":" << At.Line << ":" << At.Column
            << ": error: " << Message << "\n";

  // Skip carat diagnostics on EOF token.
  if (At.TokenKind == ninja::Token::Kind::EndOfFile)
    return;

  // Simple caret style diagnostics.
  const char *LineBegin = At.Start, *LineEnd = At.Start,
    *BufferBegin = Parser->getLexer().getBufferStart(),
    *BufferEnd = Parser->getLexer().getBufferEnd();

  // Run line pointers forward and back.
  while (LineBegin > BufferBegin &&
         LineBegin[-1] != '\r' && LineBegin[-1] != '\n')
    --LineBegin;
  while (LineEnd < BufferEnd &&
         LineEnd[0] != '\r' && LineEnd[0] != '\n')
    ++LineEnd;

  // Show the line, indented by 2.
  std::cerr << "  " << std::string(LineBegin, LineEnd) << "\n";

  // Show the caret or squiggly, making sure to print back spaces the
  // same.
  std::cerr << "  ";
  for (const char* S = LineBegin; S != At.Start; ++S)
    std::cerr << (isspace(*S) ? *S : ' ');
  if (At.Length > 1) {
    for (unsigned i = 0; i != At.Length; ++i)
      std::cerr << '~';
  } else {
    std::cerr << '^';
  }
  std::cerr << '\n';
}

bool util::ReadFileContents(std::string Path,
                            std::unique_ptr<char[]> *Data_Out,
                            uint64_t* Size_Out,
                            std::string* Error_Out) {
  // Open the input buffer and compute its size.
  FILE* fp = ::fopen(Path.c_str(), "rb");
  if (!fp) {
    int ErrorNumber = errno;
    *Error_Out = std::string("unable to open input: \"") +
      util::EscapedString(Path) + "\" (" + ::strerror(ErrorNumber) + ")";
    return false;
  }

  fseek(fp, 0, SEEK_END);
  uint64_t Size = *Size_Out = ::ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // Read the file contents.
  std::unique_ptr<char[]> Data(new char[Size]);
  uint64_t Pos = 0;
  while (Pos < Size) {
    // Read data into the buffer.
    size_t Result = ::fread(Data.get() + Pos, 1, Size - Pos, fp);
    if (Result <= 0) {
      *Error_Out = std::string("unable to read input: ") + Path;
      return false;
    }

    Pos += Result;
  }

  // Close the file.
  ::fclose(fp);

  *Data_Out = std::move(Data);
  return true;
}
