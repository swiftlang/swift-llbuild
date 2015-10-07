//===-- CommandLineStatusOutput.cpp ---------------------------------------===//
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

#include "CommandLineStatusOutput.h"

#include "llvm/Support/Process.h"

#include <cassert>
#include <cstdio>

namespace {

struct CommandLineStatusOutputImpl {
  /// The output stream.
  FILE* fp{nullptr};

  /// Whether the output stream honors '\r'.
  bool termHonorsCarriageReturn{false};

  /// Whether the stream has current output.
  bool hasOutput{false};

  /// Whether the stream has been closed.
  bool isClosed{false};

  /// The number of characters written to the current line.
  int numCurrentCharacters{0};

  CommandLineStatusOutputImpl() {}

  ~CommandLineStatusOutputImpl() {
    if (isOpen()) {
      std::string error;
      close(&error);
    }
  }

  bool isOpen() const {
    return fp != nullptr;
  }

  bool open(std::string* error_out) {
    assert(!isOpen() && !isClosed);
    fp = stdout;

    // Detect the features of the output.
    int fd = fileno(fp);
    if (llvm::sys::Process::FileDescriptorIsDisplayed(fd)) {
      // If the terminal is a tty, check the TERM variable.
      const char* term = ::getenv("TERM");

      // We assume the terminal honors '\r' if it is either not-"dumb", or it is
      // "dumb" and we are inside Emacs (comint-mode reports as "dumb").
      if (term) {
        termHonorsCarriageReturn = (term != std::string("dumb") ||
                                    ::getenv("INSIDE_EMACS") != nullptr);
      }
    }

    return true;
  }

  bool close(std::string* error_out) {
    if (hasOutput) {
      fprintf(fp, "\n");
      fflush(fp);
    }

    fp = nullptr;
    isClosed = true; // Don't allow re-opening.
    return true;
  }

  bool canUpdateCurrentLine() const {
    assert(isOpen());
    return termHonorsCarriageReturn;
  }

  void clearOutput() {
    assert(isOpen());
    assert(canUpdateCurrentLine());
    
    if (hasOutput) {
      // Clear the line before writing, this tends to produce better results
      // than clearing the unwritten tail of the line written below.
      fprintf(fp, "\r%*s\r", numCurrentCharacters, "");
      fflush(fp);
      hasOutput = false;
    }
  }

  int getNumColumns() {
    auto result = llvm::sys::Process::StandardOutColumns();

    // If we were unable to query the terminal, just use a default.
    if (!result) {
      return 80;
    }
    
    return result;
  }

  void setCurrentLine(const std::string& text) {
    assert(isOpen());
    assert(text.find('\r') == std::string::npos);
    assert(text.find('\n') == std::string::npos);

    // Clear the line before writing, this tends to produce better results than
    // clearing the unwritten tail of the line written below.
    clearOutput();

    // Write the line, trimming it to fit in the current terminal.
    int columns = getNumColumns();
    if (columns > 0 && (int)text.size() > columns) {
      // Elide the middle of the text.
      int midpoint = columns / 2;
      std::string elided = text.substr(0, std::max(0, midpoint - 2)) + "..." +
        text.substr(text.size() - (columns - (midpoint + 1)));
      assert(columns < 3 || (int)elided.size() == columns);
      fprintf(fp, "%s", elided.c_str());
      numCurrentCharacters = elided.size();
    } else {
      fprintf(fp, "%s", text.c_str());
      numCurrentCharacters = text.size();
    }
    fflush(fp);

    hasOutput = numCurrentCharacters != 0;
  }

  void setOrWriteLine(const std::string& text) {
    if (canUpdateCurrentLine())
      return setCurrentLine(text);
    writeText(text + "\n");
  }

  void finishLine() {
    assert(isOpen());

    // Finish the current line, if necessary.
    if (canUpdateCurrentLine() && hasOutput) {
      fputc('\n', fp);
      fflush(fp);
      hasOutput = false;
    }
  }

  void writeText(const std::string& text) {
    assert(isOpen());
    assert(text.size() && text.back() == '\n');

    // Clear the current output, if present.
    if (hasOutput)
      clearOutput();

    fwrite(text.c_str(), text.size(), 1, fp);
    fflush(fp);
  }
};

}

namespace llbuild {
namespace commands {

CommandLineStatusOutput::CommandLineStatusOutput()
    : impl(new CommandLineStatusOutputImpl())
{
}

CommandLineStatusOutput::~CommandLineStatusOutput() {
  delete static_cast<CommandLineStatusOutputImpl*>(impl);
}

bool CommandLineStatusOutput::open(std::string* error_out) {
  return static_cast<CommandLineStatusOutputImpl*>(impl)->open(error_out);
}

bool CommandLineStatusOutput::close(std::string* error_out) {
  return static_cast<CommandLineStatusOutputImpl*>(impl)->close(error_out);
}

bool CommandLineStatusOutput::canUpdateCurrentLine() const {
  return
    static_cast<CommandLineStatusOutputImpl*>(impl)->canUpdateCurrentLine();
}

void CommandLineStatusOutput::clearOutput() {
  return static_cast<CommandLineStatusOutputImpl*>(impl)->clearOutput();
}

void CommandLineStatusOutput::setCurrentLine(const std::string& text) {
  return
    static_cast<CommandLineStatusOutputImpl*>(impl)->setCurrentLine(text);
}

void CommandLineStatusOutput::setOrWriteLine(const std::string& text) {
  return
    static_cast<CommandLineStatusOutputImpl*>(impl)->setOrWriteLine(text);
}

void CommandLineStatusOutput::finishLine() {
  return static_cast<CommandLineStatusOutputImpl*>(impl)->finishLine();
}

void CommandLineStatusOutput::writeText(const std::string& text) {
  return
    static_cast<CommandLineStatusOutputImpl*>(impl)->writeText(text);
}

}
}
