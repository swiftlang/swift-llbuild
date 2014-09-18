//===-- llbuild.cpp - llbuild Frontend Utillity ---------------------------===//
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

#include "llbuild/Basic/Version.h"

#include "llbuild/Ninja/Lexer.h"

#include <cstdio>
#include <cstdlib>

using namespace llbuild;

int main(int argc, const char **argv) {
    // Print the version and exit.
    printf("%s\n", getLLBuildFullVersion().c_str());

    // Check the Ninja lexer, for now.
    if (argc != 2) {
      fprintf(stderr, "error: %s: invalid number of arguments\n",
              getprogname());
      return 1;
    }

    // Open the input buffer and compute its size.
    FILE *fp = fopen(argv[1], "rb");
    fseek(fp, 0, SEEK_END);
    uint64_t Size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Read the file contents.
    std::unique_ptr<char[]> Data(new char[Size]);
    uint64_t Pos = 0;
    while (Pos < Size) {
      // Read data into the buffer.
      size_t Result = fread(Data.get() + Pos, 1, Size - Pos, fp);
      if (Result <= 0) {
        fprintf(stderr, "error: %s: unable to read input\n", getprogname());
        return 1;
      }

      Pos += Result;
    }

    // Create a Ninja lexer.
    fprintf(stderr, "note: %s: reading tokens from %s\n", getprogname(),
            argv[1]);
    ninja::Lexer Lexer(Data.get(), Size);
    ninja::Token Tok;
    while (Lexer.lex(Tok).TokenKind != ninja::Token::Kind::EndOfFile) {
      Tok.dump();
    }
    Tok.dump();

    return 0;
}
