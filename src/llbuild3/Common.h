//===- Common.h -------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD3_COMMON_H
#define LLBUILD3_COMMON_H

#include <string>

#define UUID_SYSTEM_GENERATOR 1
#include "uuid.h"

#include "llbuild3/Common.pb.h"


namespace llbuild3 {

typedef uuids::uuid EngineID;

inline EngineID generateEngineID() { return uuids::uuid_system_generator()(); }

inline Stat makeStat(std::string_view name, int64_t value) {
  Stat s;
  s.set_name(name);
  s.set_int_value(value);
  return s;
}

inline Stat makeStat(std::string_view name, uint64_t value) {
  Stat s;
  s.set_name(name);
  s.set_uint_value(value);
  return s;
}

inline Stat makeStat(std::string_view name, std::string_view value) {
  Stat s;
  s.set_name(name);
  s.set_string_value(value);
  return s;
}

inline Stat makeStatB(std::string_view name, bool value) {
  Stat s;
  s.set_name(name);
  s.set_bool_value(value);
  return s;
}

inline Stat makeStat(std::string_view name, double value) {
  Stat s;
  s.set_name(name);
  s.set_double_value(value);
  return s;
}

inline Stat makeStat(std::string_view name, const CASID& value) {
  Stat s;
  s.set_name(name);
  *s.mutable_cas_object() = value;
  return s;
}

inline Stat makeStat(std::string_view name, const Error& value) {
  Stat s;
  s.set_name(name);
  *s.mutable_error_value() = value;
  return s;
}
}

#endif
