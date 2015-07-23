//===-- BuildFile.cpp -----------------------------------------------------===//
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

#include "llbuild/BuildSystem/BuildFile.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLParser.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

BuildFileDelegate::~BuildFileDelegate() {}

#pragma mark - BuildEngine implementation

namespace {

static void dumpNode(llvm::yaml::Node *node, unsigned indent=0) {
  switch (node->getType()) {
  default: {
    fprintf(stderr, "%*s<node: %p, unknown>\n", indent*2, "", node);
    break;
  }

  case llvm::yaml::Node::NK_Null: {
    fprintf(stderr, "%*s(null)\n", indent*2, "");
    break;
  }

  case llvm::yaml::Node::NK_Scalar: {
    llvm::yaml::ScalarNode *scalar = llvm::cast<llvm::yaml::ScalarNode>(node);
    llvm::SmallString<256> storage;
    fprintf(stderr, "%*s(scalar: '%s')\n", indent*2, "",
            scalar->getValue(storage).str().c_str());
    break;
  }

  case llvm::yaml::Node::NK_KeyValue: {
    assert(0 && "unexpected keyvalue node");
    break;
  }

  case llvm::yaml::Node::NK_Mapping: {
    llvm::yaml::MappingNode *map = llvm::cast<llvm::yaml::MappingNode>(node);
    fprintf(stderr, "%*smap:\n", indent*2, "");
    for (auto& it: *map) {
      fprintf(stderr, "%*skey:\n", (indent+1)*2, "");
      dumpNode(it.getKey(), indent+2);
      fprintf(stderr, "%*svalue:\n", (indent+1)*2, "");
      dumpNode(it.getValue(), indent+2);
    }
    break;
  }

  case llvm::yaml::Node::NK_Sequence: {
    llvm::yaml::SequenceNode *sequence =
      llvm::cast<llvm::yaml::SequenceNode>(node);
    fprintf(stderr, "%*ssequence:\n", indent*2, "");
    for (auto& it: *sequence) {
      dumpNode(&it, indent+1);
    }
    break;
  }

  case llvm::yaml::Node::NK_Alias: {
    fprintf(stderr, "%*s(alias)\n", indent*2, "");
    break;
  }
  }
}

class BuildFileImpl {
  /// The name of the main input file.
  std::string mainFilename;

  /// The delegate the BuildFile was configured with.
  BuildFileDelegate& delegate;

public:
  BuildFileImpl(class BuildFile& buildFile,
                const std::string& mainFilename,
                BuildFileDelegate& delegate)
    : mainFilename(mainFilename), delegate(delegate) {}

  ~BuildFileImpl() {}

  BuildFileDelegate* getDelegate() {
    return &delegate;
  }

  /// @name Parse Actions
  /// @{

  void load() {
    // Create a memory buffer for the input.
    llvm::SourceMgr sourceMgr;
    auto res = llvm::MemoryBuffer::getFile(
        mainFilename);
    if (auto ec = res.getError()) {
      // FIXME: Propagate error here.
      fprintf(stderr, "error: unable to open '%s' (%s)\n",
              mainFilename.c_str(), ec.message().c_str());
      return;
    }

    std::unique_ptr<llvm::MemoryBuffer> input(res->release());

    // Create a YAML parser.
    llvm::yaml::Stream stream(input->getMemBufferRef(), sourceMgr);

    // Read all of the documents.
    for (auto& document: stream) {
      llvm::yaml::Node *node = document.getRoot();
      dumpNode(node);
    }
  }

  /// @}
};

}

#pragma mark - BuildFile

BuildFile::BuildFile(const std::string& mainFilename,
                     BuildFileDelegate& delegate)
  : impl(new BuildFileImpl(*this, mainFilename, delegate)) 
{
}

BuildFile::~BuildFile() {
  delete static_cast<BuildFileImpl*>(impl);
}

BuildFileDelegate* BuildFile::getDelegate() {
  return static_cast<BuildFileImpl*>(impl)->getDelegate();
}

void BuildFile::load() {
  return static_cast<BuildFileImpl*>(impl)->load();
}
