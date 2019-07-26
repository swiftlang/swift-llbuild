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

#include "BuildSystem-C-API-Private.h"

#include "llvm/ADT/ArrayRef.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

namespace {
/// This class is used as a context pointer in the client
class CAPIBuildKey {
public:
  BuildKey internalBuildKey;
  CAPIBuildKey(const BuildKey &buildKey): internalBuildKey(buildKey) {}
  
  llb_build_key_kind_t getKind() {
    return internalToPublicBuildKeyKind(internalBuildKey.getKind());
  }
};

static const BuildKey::Kind publicToInternalBuildKeyKind(llb_build_key_kind_t kind) {
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

static BuildKey convertBuildKey(llb_build_key_t& key) {
  BuildKey::Kind kind = publicToInternalBuildKeyKind(key.kind);
  
  KeyType prefix(1, BuildKey::identifierForKind(kind));
  KeyType suffix((char*) key.key.data, key.key.length);
  return BuildKey::fromData(prefix + suffix);
}
}

llb_build_key *llb_build_key_make(llb_build_key_t key) {
  auto buildKey = convertBuildKey(key);
  return (llb_build_key *)new CAPIBuildKey(buildKey);
}

llb_build_key_kind_t llb_build_key_get_kind(llb_build_key *_Nonnull key) {
  return ((CAPIBuildKey *)key)->getKind();
}

void llb_build_key_get_key_data(llb_build_key *_Nonnull key, void *_Nullable context, void (* _Nonnull iteration)(void *_Nullable context, uint8_t data)) {
  auto keyData = ((CAPIBuildKey *)key)->internalBuildKey.getKeyData();
  for (auto element: keyData) {
    iteration(context, element);
  }
}

void llb_build_key_destroy(llb_build_key *key) {
  delete (CAPIBuildKey *)key;
}

char llb_build_key_identifier_for_kind(llb_build_key_kind_t kind) {
  return BuildKey::identifierForKind(publicToInternalBuildKeyKind(kind));
}

llb_build_key_kind_t llb_build_key_kind_for_identifier(char identifier) {
  return internalToPublicBuildKeyKind(BuildKey::kindForIdentifier(identifier));
}

llb_build_key *llb_build_key_make_command(const char *name) {
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeCommand(StringRef(name)));
}

void llb_build_key_get_command_name(llb_build_key *key, llb_data_t *out_name) {
  auto name = ((CAPIBuildKey *)key)->internalBuildKey.getCommandName();
  out_name->length = name.size();
  out_name->data = (const uint8_t*)strdup(name.str().c_str());
}

llb_build_key *llb_build_key_make_custom_task(const char *name, const char *taskData) {
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeCustomTask(StringRef(name), StringRef(taskData)));
}

void llb_build_key_get_custom_task_name(llb_build_key *key, llb_data_t *out_name) {
  auto name = ((CAPIBuildKey *)key)->internalBuildKey.getCustomTaskName();
  out_name->length = name.size();
  out_name->data = (const uint8_t*)strdup(name.str().c_str());
}

void llb_build_key_get_custom_task_data(llb_build_key *key, llb_data_t *out_task_data) {
  auto data = ((CAPIBuildKey *)key)->internalBuildKey.getCustomTaskData();
  out_task_data->length = data.size();
  out_task_data->data = (const uint8_t*)strdup(data.str().c_str());
}

llb_build_key *llb_build_key_make_directory_contents(const char *path) {
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeDirectoryContents(StringRef(path)));
}

void llb_build_key_get_directory_path(llb_build_key *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->internalBuildKey.getDirectoryPath();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

llb_build_key *llb_build_key_make_filtered_directory_contents(const char *path, const char *const *filters, size_t count_filters) {
  auto filtersToPass = std::vector<StringRef>();
  for (int i = 0; i < count_filters; i++) {
    filtersToPass.push_back(StringRef(filters[i]));
  }
  
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeFilteredDirectoryContents(StringRef(path), basic::StringList(ArrayRef<StringRef>(filtersToPass))));
}

void llb_build_key_get_filtered_directory_path(llb_build_key *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->internalBuildKey.getFilteredDirectoryPath();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

void llb_build_key_get_filtered_directory_filters(llb_build_key *key, void *context, IteratorFunction iterator) {
  auto filters = ((CAPIBuildKey *)key)->internalBuildKey.getContentExclusionPatternsAsStringList();
  size_t index = 0;
  for (auto filter: filters.getValues()) {
    llb_data_t data;
    data.length = filter.size();
    data.data = (const uint8_t*)strdup(filter.str().c_str());
    iterator(context, data);
    llb_data_destroy(&data);
    index++;
  }
}

llb_build_key *llb_build_key_make_directory_tree_signature(const char *_Nonnull path, const char* const* filters, size_t count_filters) {
  auto filtersToPass = std::vector<StringRef>();
  for (int i = 0; i < count_filters; i++) {
    filtersToPass.push_back(StringRef(filters[i]));
  }
  
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeDirectoryTreeSignature(StringRef(path), basic::StringList(ArrayRef<StringRef>(filtersToPass))));
}

void llb_build_key_get_directory_tree_signature_path(llb_build_key *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->internalBuildKey.getDirectoryTreeSignaturePath();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

void llb_build_key_get_directory_tree_signature_filters(llb_build_key *key, void *context, IteratorFunction iterator) {
  llb_build_key_get_filtered_directory_filters(key, context, iterator);
}

llb_build_key *llb_build_key_make_directory_tree_structure_signature(const char *path) {
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeDirectoryTreeStructureSignature(StringRef(path)));
}

void llb_build_key_get_directory_tree_structure_signature_path(llb_build_key *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->internalBuildKey.getDirectoryPath();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

llb_build_key *llb_build_key_make_node(const char *path) {
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeNode(StringRef(path)));
}

void llb_build_key_get_node_path(llb_build_key *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->internalBuildKey.getNodeName();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

llb_build_key *llb_build_key_make_stat(const char *path) {
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeStat(StringRef(path)));
}

void llb_build_key_get_stat_path(llb_build_key *key, llb_data_t *out_path) {
  auto path = ((CAPIBuildKey *)key)->internalBuildKey.getStatName();
  out_path->length = path.size();
  out_path->data = (const uint8_t*)strdup(path.str().c_str());
}

llb_build_key *llb_build_key_make_target(const char *name) {
  return (llb_build_key *)new CAPIBuildKey(BuildKey::makeTarget(StringRef(name)));
}

void llb_build_key_get_target_name(llb_build_key *key, llb_data_t *out_name) {
  auto name = ((CAPIBuildKey *)key)->internalBuildKey.getTargetName();
  out_name->length = name.size();
  out_name->data = (const uint8_t*)strdup(name.str().c_str());
}
