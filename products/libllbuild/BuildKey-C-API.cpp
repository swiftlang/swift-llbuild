//===-- Core-C-API.cpp ----------------------------------------------------===//
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

// Include the public API.
#include <llbuild/llbuild.h>

#include "llbuild/BuildSystem/BuildKey.h"

#include "BuildKey-C-API-Private.h"

#include "llvm/ADT/ArrayRef.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

size_t std::hash<CAPIBuildKey>::operator()(CAPIBuildKey &key) const {
  return key.getHashValue();
}

namespace {
static inline llb_build_key_kind_t internalToPublicBuildKeyKind(const BuildKey::Kind kind) {
  switch (kind) {
    case BuildKey::Kind::Command:
      return llb_build_key_kind_command;
    case BuildKey::Kind::CustomTask:
      return llb_build_key_kind_custom_task;
    case BuildKey::Kind::DirectoryContents:
      return llb_build_key_kind_directory_contents;
    case BuildKey::Kind::FilteredDirectoryContents:
      return llb_build_key_kind_filtered_directory_contents;
    case BuildKey::Kind::DirectoryTreeSignature:
      return llb_build_key_kind_directory_tree_signature;
    case BuildKey::Kind::DirectoryTreeStructureSignature:
      return llb_build_key_kind_directory_tree_structure_signature;
    case BuildKey::Kind::Node:
      return llb_build_key_kind_node;
    case BuildKey::Kind::Stat:
      return llb_build_key_kind_stat;
    case BuildKey::Kind::Target:
      return llb_build_key_kind_target;
    case BuildKey::Kind::Unknown:
      return llb_build_key_kind_unknown;
  }
}

llb_build_key_kind_t CAPIBuildKey::getKind() {
   return internalToPublicBuildKeyKind(internalBuildKey.getKind());
}

bool CAPIBuildKey::operator ==(const CAPIBuildKey &other) {
  if (hasIdentifier && other.hasIdentifier) {
    return identifier == other.identifier;
  }
  return internalBuildKey.getKeyData() == other.internalBuildKey.getKeyData();
}

static BuildKey::Kind publicToInternalBuildKeyKind(llb_build_key_kind_t kind) {
    switch (kind) {
    case llb_build_key_kind_command:
      return BuildKey::Kind::Command;
    case llb_build_key_kind_custom_task:
      return BuildKey::Kind::CustomTask;
    case llb_build_key_kind_directory_contents:
      return BuildKey::Kind::DirectoryContents;
    case llb_build_key_kind_directory_tree_signature:
      return BuildKey::Kind::DirectoryTreeSignature;
    case llb_build_key_kind_node:
      return BuildKey::Kind::Node;
    case llb_build_key_kind_target:
      return BuildKey::Kind::Target;
    case llb_build_key_kind_unknown:
      return BuildKey::Kind::Unknown;
    case llb_build_key_kind_directory_tree_structure_signature:
      return BuildKey::Kind::DirectoryTreeStructureSignature;
    case llb_build_key_kind_filtered_directory_contents:
      return BuildKey::Kind::FilteredDirectoryContents;
    case llb_build_key_kind_stat:
      return BuildKey::Kind::Stat;
  }
}
}

llb_build_key_t *llb_build_key_make(const llb_data_t *data) {
  return (llb_build_key_t *)new CAPIBuildKey(
    BuildKey::fromData(core::KeyType(
      reinterpret_cast<const char*>(data->data), static_cast<size_t>(data->length)
    ))
  );
}

bool llb_build_key_equal(llb_build_key_t *key1, llb_build_key_t *key2) {
  return (*(CAPIBuildKey *)key1) == (*(CAPIBuildKey *)key2);
}

size_t llb_build_key_hash(llb_build_key_t *key) {
  return std::hash<CAPIBuildKey>{}(*((CAPIBuildKey *)key));
}

llb_build_key_kind_t llb_build_key_get_kind(llb_build_key_t *_Nonnull key) {
  return ((CAPIBuildKey *)key)->getKind();
}

void llb_build_key_get_key_data(llb_build_key_t *key, void *_Nonnull context, void (*_Nonnull iteration)(void *context, uint8_t *data, size_t count)) {
  auto &keyData = ((CAPIBuildKey *)key)->getInternalBuildKey().getKeyData();
  iteration(context, (uint8_t *)keyData.data(), keyData.size());
}

void llb_build_key_destroy(llb_build_key_t *key) {
  delete (CAPIBuildKey *)key;
}

char llb_build_key_identifier_for_kind(llb_build_key_kind_t kind) {
  return BuildKey::identifierForKind(publicToInternalBuildKeyKind(kind));
}

llb_build_key_kind_t llb_build_key_kind_for_identifier(char identifier) {
  return internalToPublicBuildKeyKind(BuildKey::kindForIdentifier(identifier));
}

llb_build_key_t *llb_build_key_make_command(const char *name) {
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeCommand(StringRef(name)));
}

void llb_build_key_get_command_name(llb_build_key_t *key, llb_data_t *out_name) {
  auto name = ((CAPIBuildKey *)key)->getInternalBuildKey().getCommandName();
  out_name->length = name.size();
  out_name->data = (const uint8_t*)strdup(name.str().c_str());
}

llb_build_key_t *llb_build_key_make_custom_task(const char *name, const char *taskData) {
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeCustomTask(StringRef(name), StringRef(taskData)));
}

llb_build_key_t *llb_build_key_make_custom_task_with_data(const char *name, llb_data_t data) {
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeCustomTask(StringRef(name), StringRef((const char *)data.data, data.length)));
}


void llb_build_key_get_custom_task_name(llb_build_key_t *key, llb_data_t *out_name) {
  auto name = ((CAPIBuildKey *)key)->getInternalBuildKey().getCustomTaskName();
  out_name->length = name.size();
  out_name->data = (const uint8_t*)strdup(name.str().c_str());
}

void llb_build_key_get_custom_task_data(llb_build_key_t *key, llb_data_t *out_task_data) {
  auto data = ((CAPIBuildKey *)key)->getInternalBuildKey().getCustomTaskData();
  out_task_data->length = data.size();
  out_task_data->data = (const uint8_t*)strdup(data.str().c_str());
}

void llb_build_key_get_custom_task_data_no_copy(llb_build_key_t *key, llb_data_t *out_task_data) {
  auto data = ((CAPIBuildKey *)key)->getInternalBuildKey().getCustomTaskData();
  out_task_data->length = data.size();
  out_task_data->data = (const uint8_t *)data.data();
}

llb_build_key_t *llb_build_key_make_directory_contents(const char *path) {
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeDirectoryContents(StringRef(path)));
}

void llb_build_key_get_directory_path(llb_build_key_t *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->getInternalBuildKey().getDirectoryPath();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

llb_build_key_t *llb_build_key_make_filtered_directory_contents(const char *path, const char *const *filters, int32_t count_filters) {
  auto filtersToPass = std::vector<StringRef>();
  for (int i = 0; i < count_filters; i++) {
    filtersToPass.push_back(StringRef(filters[i]));
  }
  
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeFilteredDirectoryContents(StringRef(path), basic::StringList(ArrayRef<StringRef>(filtersToPass))));
}

void llb_build_key_get_filtered_directory_path(llb_build_key_t *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->getInternalBuildKey().getFilteredDirectoryPath();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

void llb_build_key_get_filtered_directory_filters(llb_build_key_t *key, void *context, IteratorFunction iterator) {
  auto filters = ((CAPIBuildKey *)key)->getInternalBuildKey().getContentExclusionPatternsAsStringList();
  for (auto filter: filters.getValues()) {
    llb_data_t data;
    data.length = filter.size();
    data.data = (const uint8_t*)strdup(filter.str().c_str());
    iterator(context, data);
    llb_data_destroy(&data);
  }
}

llb_build_key_t *llb_build_key_make_directory_tree_signature(const char *_Nonnull path, const char* const* filters, int32_t count_filters) {
  auto filtersToPass = std::vector<StringRef>();
  for (int i = 0; i < count_filters; i++) {
    filtersToPass.push_back(StringRef(filters[i]));
  }
  
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeDirectoryTreeSignature(StringRef(path), basic::StringList(ArrayRef<StringRef>(filtersToPass))));
}

void llb_build_key_get_directory_tree_signature_path(llb_build_key_t *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->getInternalBuildKey().getDirectoryTreeSignaturePath();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

void llb_build_key_get_directory_tree_signature_filters(llb_build_key_t *key, void *context, IteratorFunction iterator) {
  llb_build_key_get_filtered_directory_filters(key, context, iterator);
}

llb_build_key_t *llb_build_key_make_directory_tree_structure_signature(const char *path, const char* const* filters, int32_t count_filters) {
  auto filtersToPass = std::vector<StringRef>();
  for (int i = 0; i < count_filters; i++) {
    filtersToPass.push_back(StringRef(filters[i]));
  }

  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeDirectoryTreeStructureSignature(StringRef(path),basic::StringList(ArrayRef<StringRef>(filtersToPass))));
}

void llb_build_key_get_directory_tree_structure_signature_filters(llb_build_key_t *key, void *context, IteratorFunction iterator) {
  llb_build_key_get_filtered_directory_filters(key, context, iterator);
}



void llb_build_key_get_directory_tree_structure_signature_path(llb_build_key_t *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->getInternalBuildKey().getFilteredDirectoryPath();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

llb_build_key_t *llb_build_key_make_node(const char *path) {
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeNode(StringRef(path)));
}

void llb_build_key_get_node_path(llb_build_key_t *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->getInternalBuildKey().getNodeName();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

llb_build_key_t *llb_build_key_make_stat(const char *path) {
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeStat(StringRef(path)));
}

void llb_build_key_get_stat_path(llb_build_key_t *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->getInternalBuildKey().getStatName();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

llb_build_key_t *llb_build_key_make_target(const char *name) {
  return (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeTarget(StringRef(name)));
}

void llb_build_key_get_target_name(llb_build_key_t *key, llb_data_t *out_name) {
  auto name = ((CAPIBuildKey *)key)->getInternalBuildKey().getTargetName();
  out_name->length = name.size();
  out_name->data = (const uint8_t*)strdup(name.str().c_str());
}
