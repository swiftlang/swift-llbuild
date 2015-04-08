//===- CommandLineStatusOutput.h --------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_COMMANDS_COMMANDLINESTATUSOUTPUT_H
#define LLBUILD_COMMANDS_COMMANDLINESTATUSOUTPUT_H

#include <string>

namespace llbuild {
namespace commands {

/// Utility class for writing out progress or status information to a terminal
/// which abstracts out support for ANSI compliant terminals.
class CommandLineStatusOutput {
  void *impl;

public:
  CommandLineStatusOutput();

  ~CommandLineStatusOutput();

  /// Open the output stream for writing.
  bool open(std::string* error_out);

  /// Close the output stream and clear any incomplete output.
  bool close(std::string* error_out);

  /// Check if the attached output device can update (rewrite) the current line.
  bool canUpdateCurrentLine() const;

  /// Clear the current output.
  ///
  /// This requires that \see canUpdateCurrentLine() is true.
  void clearOutput();

  /// Update the current line of output text.
  void setCurrentLine(const std::string& text);

  /// Update the current line of output text, if possible, otherwise simply
  /// write it out.
  ///
  /// The text should be a single line with no newlines or carriage returns.
  void setOrWriteLine(const std::string& text);

  /// Finish writing the current line, if necessary.
  void finishLine();

  /// Write a non-overwritable block of text to the output.
  ///
  /// Any text written by this method should always end with a newline.
  void writeText(const std::string& text);
};

}
}

#endif
