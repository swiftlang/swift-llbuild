//===- unittests/Core/DependencyInfoParserTest.cpp ------------------------===//
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

#include "llbuild/Core/DependencyInfoParser.h"

#include "gtest/gtest.h"

#include <string>
#include <vector>

using namespace llbuild;
using namespace llbuild::core;

namespace {

TEST(DependencyInfoParserTest, basic) {
  typedef std::pair<std::string, std::string> Event;
  typedef std::pair<std::string, uint64_t> Error;
  struct TestActions : public DependencyInfoParser::ParseActions {
    std::vector<Event> events;
    std::vector<Error> errors;

    virtual void error(const char* message, uint64_t length) override {
      errors.push_back({ message, length });
    }

    virtual void actOnVersion(StringRef name) override {
      events.push_back({ "version", std::string(name) });
    }

    virtual void actOnInput(StringRef name) override {
      events.push_back({ "input", std::string(name) });
    }

    virtual void actOnOutput(StringRef name) override {
      events.push_back({ "output", std::string(name) });
    }

    virtual void actOnMissing(StringRef name) override {
      events.push_back({ "missing", std::string(name) });
    }
  };

#define INPUT(str)                                                             \
  StringRef(str, sizeof(str) - 1);                                             \
  assert(sizeof(str) != 0);

  // Check missing terminator diagnose (on empty file).
  {
    TestActions actions;
    auto input = INPUT("xxx");
    DependencyInfoParser(input, actions).parse();
    EXPECT_EQ(std::vector<Error>{ Error("missing null terminator", 3) },
              actions.errors);
    EXPECT_EQ(std::vector<Event>{}, actions.events);
  }

  // Check invalid initial record.
  {
    TestActions actions;
    auto input = INPUT("\x01\x00");
    DependencyInfoParser(input, actions).parse();
    EXPECT_EQ(std::vector<Error>{ Error("missing version record", 0) },
              actions.errors);
    EXPECT_EQ(std::vector<Event>{}, actions.events);
  }

  // Check empty operand.
  {
    TestActions actions;
    auto input = INPUT("\x00\x00");
    DependencyInfoParser(input, actions).parse();
    EXPECT_EQ(std::vector<Error>{ Error("empty operand", 0) },
              actions.errors);
    EXPECT_EQ(std::vector<Event>{}, actions.events);
  }

  // Check duplicate version.
  {
    TestActions actions;
    auto input = INPUT("\x00VERSION\x00\x00VERSION\x00");
    DependencyInfoParser(input, actions).parse();
    EXPECT_EQ(std::vector<Error>{ Error("invalid duplicate version", 9) },
              actions.errors);
    EXPECT_EQ(std::vector<Event>{ Event("version", "VERSION") },
              actions.events);
  }

  // Check a valid file.
  {
    TestActions actions;
    auto input = INPUT("\x00VERSION\x00"
                       "\x10INPUT\x00"
                       "\x11MISSING\x00"
                       "\x40OUTPUT\x00");
    DependencyInfoParser(input, actions).parse();
    EXPECT_EQ(std::vector<Error>{}, actions.errors);
    auto expectedEvents = std::vector<Event>{
        Event("version", "VERSION"),
        Event("input", "INPUT"),
        Event("missing", "MISSING"),
        Event("output", "OUTPUT"),
    };
    EXPECT_EQ(expectedEvents, actions.events);
  }
}

}
