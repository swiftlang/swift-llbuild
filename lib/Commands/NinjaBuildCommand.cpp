//===-- NinjaBuildCommand.cpp ---------------------------------------------===//
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

#include "NinjaBuildCommand.h"

#include "llbuild/Ninja/ManifestLoader.h"

#include "CommandUtil.h"

#include <cerrno>
#include <cstdlib>

#include <unistd.h>

using namespace llbuild;
using namespace llbuild::commands;

static void usage() {
  fprintf(stderr, "Usage: %s ninja build [--help] <manifest> [<args>]\n",
          ::getprogname());
  ::exit(1);
}

namespace {

class BuildManifestActions : public ninja::ManifestLoaderActions {
  ninja::ManifestLoader *Loader = 0;
  unsigned NumErrors = 0;
  unsigned MaxErrors = 20;

private:
  virtual void initialize(ninja::ManifestLoader *Loader) {
    this->Loader = Loader;
  }

  virtual void error(std::string Filename, std::string Message,
                     const ninja::Token &At) override {
    if (NumErrors++ >= MaxErrors)
      return;

    util::EmitError(Filename, Message, At, Loader->getCurrentParser());
  }

  virtual bool readFileContents(const std::string& FromFilename,
                                const std::string& Filename,
                                const ninja::Token* ForToken,
                                std::unique_ptr<char[]> *Data_Out,
                                uint64_t *Length_Out) override {
    // Load the file contents and return if successful.
    std::string Error;
    if (util::ReadFileContents(Filename, Data_Out, Length_Out, &Error))
      return true;

    // Otherwise, emit the error.
    if (ForToken) {
      util::EmitError(FromFilename, Error, *ForToken,
                      Loader->getCurrentParser());
    } else {
      // We were unable to open the main file.
      fprintf(stderr, "error: %s: %s\n", getprogname(), Error.c_str());
      exit(1);
    }

    return false;
  };

public:
  unsigned getNumErrors() const { return NumErrors; }
};

}

int commands::ExecuteNinjaBuildCommand(const std::vector<std::string> &Args) {
  if (Args.empty() || Args[0] == "--help")
    usage();

  if (Args.size() != 1) {
    fprintf(stderr, "\error: %s: invalid number of arguments\n\n",
            ::getprogname());
    usage();
  }

  // Change to the directory containing the input file, so include references
  // can be relative.
  //
  // FIXME: Need llvm::sys::fs.
  std::string Filename = Args[0];
  size_t Pos = Filename.find_last_of('/');
  if (Pos != std::string::npos) {
    if (::chdir(std::string(Filename.substr(0, Pos)).c_str()) < 0) {
      fprintf(stderr, "error: %s: unable to chdir(): %s\n",
              getprogname(), strerror(errno));
      return 1;
    }
    Filename = Filename.substr(Pos+1);
  }

  // Load the manifest.
  BuildManifestActions Actions;
  ninja::ManifestLoader Loader(Filename, Actions);
  std::unique_ptr<ninja::Manifest> Manifest = Loader.load();

  // If there were errors loading, we are done.
  if (unsigned NumErrors = Actions.getNumErrors()) {
    fprintf(stderr, "%d errors generated.\n", NumErrors);
    return 1;
  }

  // Otherwise, run the build.

  return 0;
}
