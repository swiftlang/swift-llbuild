//===- Logging.cpp ----------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild3/Logging.h"

using namespace llbuild3;

Logger::~Logger() { }

void NullLogger::error(LoggingContext, Error) { }
void NullLogger::event(LoggingContext, const std::vector<Stat>&) { }
