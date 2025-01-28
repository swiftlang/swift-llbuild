//===- Label.h --------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2024 - 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_LABELTRIE_H
#define LLBUILD3_LABELTRIE_H

#include <string>

#include "llbuild3/Label.pb.h"

namespace llbuild3 {
namespace support {

template <typename entry_type> class LabelTrie {
private:
  bool prefixOnly;

  struct node;
  typedef std::unordered_map<std::string, node> node_map;
  struct node {
    std::variant<node_map, entry_type> contents;
  };

  node_map root;

public:
  LabelTrie(bool prefixOnly = false) : prefixOnly(prefixOnly) {}
  ~LabelTrie() {}

  bool insert(std::pair<const Label&, entry_type> entry);

  std::optional<entry_type> operator[](const Label& label) const;

  bool contains(const Label& label) const { return (*this)[label].has_value(); }
};

template <typename entry_type>
bool LabelTrie<entry_type>::insert(std::pair<const Label&, entry_type> entry) {
  auto& [label, value] = entry;
  node_map* current = &root;
  int idx = 0;
  while (idx < label.components_size() - 1) {
    auto component = label.components(idx);
    if (current->contains(component)) {
      auto& next = current->at(component);
      if (std::holds_alternative<node_map>(next.contents)) {
        current = &std::get<node_map>(next.contents);
        idx++;
        continue;
      } else {
        return false;
      }
    }

    current->insert({component, node{node_map()}});
    current = &std::get<node_map>(current->at(component).contents);
    idx++;
  }

  auto lastComponent = label.components(idx);
  if (prefixOnly) {
    current->insert({lastComponent, {value}});
    return true;
  }

  current->insert({lastComponent, node{node_map()}});

  std::string target;
  if (label.name().empty()) {
    target = ":" + lastComponent;
  } else {
    target = ":" + label.name();
  }
  current = &std::get<node_map>(current->at(lastComponent).contents);
  current->insert({target, {value}});

  return true;
}

template <typename entry_type>
std::optional<entry_type>
LabelTrie<entry_type>::operator[](const Label& label) const {
  if (label.components_size() == 0)
    return {};

  const node_map* current = &root;
  for (auto component : label.components()) {
    if (!current->contains(component)) {
      return {};
    }

    auto& next = current->at(component);
    if (std::holds_alternative<entry_type>(next.contents)) {
      return std::get<entry_type>(next.contents);
    }
    current = &std::get<node_map>(next.contents);
  }

  if (prefixOnly)
    return {};

  std::string target;
  if (label.name().empty()) {
    target = ":" + label.components(label.components_size() - 1);
  } else {
    target = ":" + label.name();
  }
  if (current->contains(target)) {
    auto& next = current->at(target);
    if (std::holds_alternative<entry_type>(next.contents)) {
      return std::get<entry_type>(next.contents);
    }
  }

  return {};
}

}
}

#endif
