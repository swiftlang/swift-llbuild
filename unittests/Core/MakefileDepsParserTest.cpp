//===- unittests/Core/MakefileDepsParserTest.cpp --------------------------===//
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

#include "llbuild/Core/MakefileDepsParser.h"

#include "gtest/gtest.h"

#include <string>
#include <vector>

using namespace llbuild;
using namespace llbuild::core;

namespace {

TEST(MakefileDepsParserTest, Basic) {
  typedef std::pair<std::string, std::vector<std::string>> RuleRecord;
  typedef std::pair<std::string, uint64_t> ErrorRecord;
  struct TestActions : public MakefileDepsParser::ParseActions {
    std::vector<RuleRecord> Records;
    std::vector<std::pair<std::string, uint64_t>> Errors;

    virtual void error(const char* Message, uint64_t Length) override {
      Errors.push_back({ Message, Length });
    }

    virtual void actOnRuleStart(const char* Name, uint64_t Length) override {
      Records.push_back({ std::string(Name, Name + Length), {} });
    }

    virtual void actOnRuleDependency(const char* Dependency,
                                     uint64_t Length) override {
      assert(!Records.empty());
      Records.back().second.push_back(std::string(Dependency,
                                                  Dependency+Length));
    }
    virtual void actOnRuleEnd() override {}
  };

  TestActions Actions;
  std::string Input;

  // Check a basic valid input with an escape sequence.
  Input = "a: b\\$c d\\\ne";
  MakefileDepsParser(Input.data(), Input.size(), Actions).parse();
  EXPECT_EQ(0U, Actions.Errors.size());
  EXPECT_EQ(1U, Actions.Records.size());
  EXPECT_EQ(RuleRecord("a", { "b\\$c", "d", "e" }),
            Actions.Records[0]);

  // Check a basic valid input.
  Actions.Errors.clear();
  Actions.Records.clear();
  Input = "a: b c d";
  MakefileDepsParser(Input.data(), Input.size(), Actions).parse();
  EXPECT_EQ(0U, Actions.Errors.size());
  EXPECT_EQ(1U, Actions.Records.size());
  EXPECT_EQ(RuleRecord("a", { "b", "c", "d" }),
            Actions.Records[0]);

  // Check a basic valid input with two rules.
  Actions.Errors.clear();
  Actions.Records.clear();
  Input = "a: b c d\none: two three";
  MakefileDepsParser(Input.data(), Input.size(), Actions).parse();
  EXPECT_EQ(0U, Actions.Errors.size());
  EXPECT_EQ(2U, Actions.Records.size());
  EXPECT_EQ(RuleRecord("a", { "b", "c", "d" }),
            Actions.Records[0]);
  EXPECT_EQ(RuleRecord("one", { "two", "three" }),
            Actions.Records[1]);

  // Check a valid input with a trailing newline.
  Input = "out: \\\n  in1\n";
  Actions.Errors.clear();
  Actions.Records.clear();
  MakefileDepsParser(Input.data(), Input.size(), Actions).parse();
  EXPECT_EQ(0U, Actions.Errors.size());
  EXPECT_EQ(1U, Actions.Records.size());
  EXPECT_EQ(RuleRecord("out", { "in1" }),
            Actions.Records[0]);

  // Check error case if leading garbage.
  Actions.Errors.clear();
  Actions.Records.clear();
  Input = "  =$ a";
  MakefileDepsParser(Input.data(), Input.size(), Actions).parse();
  EXPECT_EQ(1U, Actions.Errors.size());
  EXPECT_EQ(Actions.Errors[0],
            ErrorRecord("unexpected character in file", 2U));
  EXPECT_EQ(0U, Actions.Records.size());

  // Check error case if no ':'.
  Actions.Errors.clear();
  Actions.Records.clear();
  Input = "a";
  MakefileDepsParser(Input.data(), Input.size(), Actions).parse();
  EXPECT_EQ(1U, Actions.Errors.size());
  EXPECT_EQ(Actions.Errors[0],
            ErrorRecord("missing ':' following rule", 1U));
  EXPECT_EQ(1U, Actions.Records.size());
  EXPECT_EQ(RuleRecord("a", {}),
            Actions.Records[0]);


  // Check error case in dependency list.
  Actions.Errors.clear();
  Actions.Records.clear();
  Input = "a: b$";
  MakefileDepsParser(Input.data(), Input.size(), Actions).parse();
  EXPECT_EQ(1U, Actions.Errors.size());
  EXPECT_EQ(Actions.Errors[0],
            ErrorRecord("unexpected character in prerequisites", 4U));
  EXPECT_EQ(1U, Actions.Records.size());
  EXPECT_EQ(RuleRecord("a", { "b" }),
            Actions.Records[0]);
  
}

}
