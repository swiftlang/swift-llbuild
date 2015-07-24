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

#ifndef NDEBUG
static void dumpNode(llvm::yaml::Node* node, unsigned indent=0)
    __attribute__((used));
static void dumpNode(llvm::yaml::Node* node, unsigned indent) {
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
    llvm::yaml::ScalarNode* scalar = llvm::cast<llvm::yaml::ScalarNode>(node);
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
    llvm::yaml::MappingNode* map = llvm::cast<llvm::yaml::MappingNode>(node);
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
    llvm::yaml::SequenceNode* sequence =
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
#endif

class BuildFileImpl {
  /// The name of the main input file.
  std::string mainFilename;

  /// The delegate the BuildFile was configured with.
  BuildFileDelegate& delegate;

  // FIXME: Factor out into a parser helper class.
  std::string stringFromScalarNode(llvm::yaml::ScalarNode* scalar) {
    llvm::SmallString<256> storage;
    return scalar->getValue(storage).str();
  }

  // FIXME: Factor out into a parser helper class.
  bool nodeIsScalarString(llvm::yaml::Node* node, const std::string& name) {
    if (node->getType() != llvm::yaml::Node::NK_Scalar)
      return false;

    return stringFromScalarNode(
        static_cast<llvm::yaml::ScalarNode*>(node)) == name;
  }

  bool parseRootNode(llvm::yaml::Node* node) {
    // The root must always be a mapping.
    if (node->getType() != llvm::yaml::Node::NK_Mapping) {
      delegate.error(mainFilename, "unexpected top-level node");
      return false;
    }
    auto mapping = static_cast<llvm::yaml::MappingNode*>(node);

    // Iterate over each of the sections in the mapping.
    auto it = mapping->begin();
    if (!nodeIsScalarString(it->getKey(), "client")) {
      delegate.error(mainFilename, "expected initial mapping key 'client'");
      return false;
    } else if (it->getValue()->getType() != llvm::yaml::Node::NK_Mapping) {
      delegate.error(mainFilename, "unexpected 'client' value (expected map)");
      return false;
    }

    // Parse the client mapping.
    if (!parseClientMapping(
            static_cast<llvm::yaml::MappingNode*>(it->getValue()))) {
      return false;
    }

    // Walk to the end of the map.
    for (; it != mapping->end(); ++it) ;

    return true;
  }

  bool parseClientMapping(llvm::yaml::MappingNode* map) {
    // Collect all of the keys.
    std::string name;
    uint32_t version = 0;
    property_list_type properties;

    for (auto& entry: *map) {
      // All keys and values must be scalar.
      if (entry.getKey()->getType() != llvm::yaml::Node::NK_Scalar) {
        delegate.error(mainFilename, "invalid key type in 'client' map");
        return false;
      }
      if (entry.getValue()->getType() != llvm::yaml::Node::NK_Scalar) {
        delegate.error(mainFilename, "invalid value type in 'client' map");
        return false;
      }

      std::string key = stringFromScalarNode(
          static_cast<llvm::yaml::ScalarNode*>(entry.getKey()));
      std::string value = stringFromScalarNode(
          static_cast<llvm::yaml::ScalarNode*>(entry.getValue()));
      if (key == "name") {
        name = value;
      } else if (key == "version") {
        if (llvm::StringRef(value).getAsInteger(10, version)) {
          delegate.error(
              mainFilename, "invalid version number in 'client' map");
          return false;
        }
      } else {
        properties.push_back({ key, value });
      }
    }

    // Pass to the delegate.
    if (!delegate.configureClient(name, version, properties)) {
      delegate.error(mainFilename, "unable to configure client");
      return false;
    }

    return true;
  }

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

  bool load() {
    // Create a memory buffer for the input.
    llvm::SourceMgr sourceMgr;
    auto res = llvm::MemoryBuffer::getFile(
        mainFilename);
    if (auto ec = res.getError()) {
      delegate.error(mainFilename,
                     ("unable to open '" + mainFilename +
                      "' (" + ec.message() + ")"));
      return false;
    }

    std::unique_ptr<llvm::MemoryBuffer> input(res->release());

    // Create a YAML parser.
    llvm::yaml::Stream stream(input->getMemBufferRef(), sourceMgr);

    // Read the stream, we only expect a single document.
    auto it = stream.begin();
    if (it == stream.end()) {
      delegate.error(mainFilename, "missing document in stream");
      return false;
    }

    auto& document = *it;
    if (!parseRootNode(document.getRoot())) {
      return false;
    }

    if (++it != stream.end()) {
      delegate.error(mainFilename, "unexpected additional document in stream");
      return false;
    }

    return true;
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

bool BuildFile::load() {
  return static_cast<BuildFileImpl*>(impl)->load();
}
