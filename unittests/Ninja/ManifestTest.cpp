//===- unittests/Ninja/ManifestTest.cpp -----------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include <random>

#include "llvm/Support/Path.h"
#include "llbuild/Ninja/Manifest.h"

#include "gtest/gtest.h"

using namespace llvm;
using namespace llbuild::ninja;

static std::string normalize(std::string workingDirectory, std::string path0) {
  SmallString<256> absPathTmp = StringRef(path0);
  if (!Manifest::normalize_path(workingDirectory, absPathTmp)) {
    return "<failed to normalize>";
  }
  assert(absPathTmp.size() < workingDirectory.size() + 1 + path0.size() + 1);
  return absPathTmp.str();
}

TEST(ManifestTest, normalize_path_bad_working_directory) {
  EXPECT_EQ(normalize("", ""), "<failed to normalize>");
  EXPECT_EQ(normalize("k", ""), "<failed to normalize>");
  EXPECT_EQ(normalize(".", ""), "<failed to normalize>");
  EXPECT_EQ(normalize("..", ""), "<failed to normalize>");
  EXPECT_EQ(normalize("/", ""), "/");

  EXPECT_EQ(normalize("", "k"), "<failed to normalize>");
  EXPECT_EQ(normalize("k", "k"), "<failed to normalize>");
  EXPECT_EQ(normalize("..", "k"), "<failed to normalize>");
  EXPECT_EQ(normalize("/", "k"), "/k");

  EXPECT_EQ(normalize("", "."), "<failed to normalize>");
  EXPECT_EQ(normalize("k", "."), "<failed to normalize>");
  EXPECT_EQ(normalize("..", "."), "<failed to normalize>");
  EXPECT_EQ(normalize("/", "."), "/");

  EXPECT_EQ(normalize("", ".."), "<failed to normalize>");
  EXPECT_EQ(normalize("k", ".."), "<failed to normalize>");
  EXPECT_EQ(normalize("..", ".."), "<failed to normalize>");
  EXPECT_EQ(normalize("/", ".."), "/");
}

TEST(ManifestTest, normalize_path_absolute) {
  // The working directory is ignore if the path component is absolute
  EXPECT_EQ(normalize("", "/"), "/");
  EXPECT_EQ(normalize("k", "/"), "/");
  EXPECT_EQ(normalize(".k", "/"), "/");
  EXPECT_EQ(normalize("..k", "/"), "/");
  EXPECT_EQ(normalize("..", "/"), "/");
  EXPECT_EQ(normalize("/", "/"), "/");
  EXPECT_EQ(normalize("/../", "/"), "/");
  EXPECT_EQ(normalize("/../", "/"), "/");
  EXPECT_EQ(normalize("/foo", "/"), "/");
  EXPECT_EQ(normalize("/foo", "/.k"), "/.k");
  EXPECT_EQ(normalize("/foo", "/..k"), "/..k");
  EXPECT_EQ(normalize("/foo", "/k"), "/k");
  EXPECT_EQ(normalize("/foo", "/k/"), "/k/");
  EXPECT_EQ(normalize("/foo", "/k/l"), "/k/l");
  EXPECT_EQ(normalize("/foo", "/k/m/"), "/k/m/");
}

TEST(ManifestTest, normalize_path_network) {
  EXPECT_EQ(normalize("/", "//k"), "//k/");
  EXPECT_EQ(normalize("/", "//k/"), "//k/");
  EXPECT_EQ(normalize("/", "//k/ref"), "//k/ref");
  EXPECT_EQ(normalize("/", "//k/../ref"), "//k/ref");
  EXPECT_EQ(normalize("/", "//.."), "//../");
  EXPECT_EQ(normalize("/", "//../"), "//../");
}

TEST(ManifestTest, normalize_path) {
  EXPECT_EQ(normalize("/", ""), "/");
  EXPECT_EQ(normalize("/", "k"), "/k");
  EXPECT_EQ(normalize("/", ".k"), "/.k");
  EXPECT_EQ(normalize("/", "..k"), "/..k");
  EXPECT_EQ(normalize("/", "."), "/");
  EXPECT_EQ(normalize("/", ".."), "/");
  EXPECT_EQ(normalize("/", "/"), "/");

  EXPECT_EQ(normalize("//", ""), "/");
  EXPECT_EQ(normalize("//", "k"), "/k");
  EXPECT_EQ(normalize("//", ".k"), "/.k");
  EXPECT_EQ(normalize("//", "..k"), "/..k");
  EXPECT_EQ(normalize("//", "."), "/");
  EXPECT_EQ(normalize("//", ".."), "/");
  EXPECT_EQ(normalize("//", "/"), "/");

  EXPECT_EQ(normalize("/", "//"), "/");
  EXPECT_EQ(normalize("/", "k//"), "/k/");
  EXPECT_EQ(normalize("/", ".k//"), "/.k/");
  EXPECT_EQ(normalize("/", "..k//"), "/..k/");
  EXPECT_EQ(normalize("/", ".//"), "/");
  EXPECT_EQ(normalize("/", "..//"), "/");

  EXPECT_EQ(normalize("/", ""), "/");
  EXPECT_EQ(normalize("/", "k"), "/k");
  EXPECT_EQ(normalize("/", ".k"), "/.k");
  EXPECT_EQ(normalize("/", "..k"), "/..k");
  EXPECT_EQ(normalize("/", "."), "/");
  EXPECT_EQ(normalize("/", ".."), "/");
  EXPECT_EQ(normalize("/", "/"), "/");

  EXPECT_EQ(normalize("/a", ""), "/a/");
  EXPECT_EQ(normalize("/a", "k"), "/a/k");
  EXPECT_EQ(normalize("/a", ".k"), "/a/.k");
  EXPECT_EQ(normalize("/a", "..k"), "/a/..k");
  EXPECT_EQ(normalize("/a", "."), "/a/");
  EXPECT_EQ(normalize("/a", ".."), "/");
  EXPECT_EQ(normalize("/a", "/"), "/");

  EXPECT_EQ(normalize("/a", "k/./b"), "/a/k/b");
  EXPECT_EQ(normalize("/a", ".k/./b"), "/a/.k/b");
  EXPECT_EQ(normalize("/a", "..k/./b"), "/a/..k/b");
  EXPECT_EQ(normalize("/a", "././b"), "/a/b");
  EXPECT_EQ(normalize("/a", ".././b"), "/b");
  EXPECT_EQ(normalize("/a", "/./b"), "/b");

  EXPECT_EQ(normalize("/a", "k/../b"), "/a/b");
  EXPECT_EQ(normalize("/a", ".k/../b"), "/a/b");
  EXPECT_EQ(normalize("/a", "..k/../b"), "/a/b");
  EXPECT_EQ(normalize("/a", "./../b"), "/b");
  EXPECT_EQ(normalize("/a", "../../b"), "/b");
  EXPECT_EQ(normalize("/a", "/../b"), "/b");
}

/// A model of a normalize_path which just removes the components ad hoc.
/// Should be clear, dumb and correct, not fast.
static std::string normalize_path_model(std::vector<std::string> components) {

  bool endsWithSlash = components.size() > 0 && components.back() != "a" && components.back() != "b";

  // Iterate until fixpoint.
  for (bool shrunk = true; shrunk; ) {

    shrunk = false;

    // Find something to delete and delete.
    for (auto it = components.begin(); it != components.end(); it++) {
      const auto& component = *it;

      if (component == "") {
        components.erase(it);
      } else if (component == ".") {
        components.erase(it);
      } else if(component == "..") {
        if (it == components.begin()) {
          components.erase(it);
        } else {
          auto previous = it - 1;
          components.erase(it);
          components.erase(previous);
        }
      } else {
        continue;
      }

      shrunk = true;
      break;
    }

  }

  std::string path = "/";
  for (auto &component : components) {
      if (path.back() != '/') {
        path += "/";
      }
      path += component;
  }
  if (path.back() != '/' && endsWithSlash) {
    path += "/";
  }
  return path;
}

TEST(ManifestTest, normalize_path_randomized) {
  const std::vector<std::string> componentVariants = { "", ".", "..", "a", "b" };

  std::default_random_engine generator;
  std::uniform_int_distribution<int> lengthDistribution(1, 5);
  std::uniform_int_distribution<int> componentDistribution(0, componentVariants.size() - 1);

  auto lengthDice = std::bind(lengthDistribution, generator);
  auto componentIndexDice = std::bind(componentDistribution, generator);

  bool atLeastOneTested = true;

  for (int n = 1; n < 1000; n++) {
    std::vector<std::string> pathComponents;
    const int length = lengthDice();

    for (int i = 0; i < length; i++) {
      pathComponents.push_back(componentVariants[componentIndexDice()]);
    }


    std::string path;
    for (auto &component : pathComponents) {
        path += "/" + component;
    }

    // Ignore path names with "network" prefixes, such as "//net".
    if (llvm::sys::path::root_name(path).size() != 0) {
      continue;
    } else {
      // Make sure we didn't ignore all test paths.
      atLeastOneTested = true;
    }

    EXPECT_EQ(normalize("/", path), normalize_path_model(pathComponents));
  }

  ASSERT_TRUE(atLeastOneTested);
}

