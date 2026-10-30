//===- LLVM.h ---------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_LLVM_H
#define LLBUILD_BASIC_LLVM_H

// None.h includes an enumerator that is desired & cannot be forward declared
// without a definition of NoneType.
#include "llvm/ADT/None.h"

// Forward declarations.
namespace llvm {
  // Containers
  class StringRef;
  class Twine;
  template <typename T> class SmallPtrSetImpl;
  template <typename T, unsigned N> class SmallPtrSet;
  template <typename T> class SmallVectorImpl;
  template <typename T, unsigned N> class SmallVector;
  template <unsigned N> class SmallString;
  template<typename T> class ArrayRef;
  template<typename T> class MutableArrayRef;
  template<typename T> class TinyPtrVector;
  template<typename T> class Optional;
  template <typename PT1, typename PT2> class PointerUnion;

  // Other common classes.
  class raw_ostream;
} // end namespace llvm;

namespace llbuild {
  // Containers
  using llvm::None;
  using llvm::Optional;
  using llvm::SmallPtrSetImpl;
  using llvm::SmallPtrSet;
  using llvm::SmallString;
  using llvm::StringRef;
  using llvm::Twine;
  using llvm::SmallVectorImpl;
  using llvm::SmallVector;
  using llvm::ArrayRef;
  using llvm::MutableArrayRef;
  using llvm::TinyPtrVector;
  using llvm::PointerUnion;

  // Other common classes.
  using llvm::raw_ostream;
}

#endif
