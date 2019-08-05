//===- buildvalue.h -----------------------------------------------*- C -*-===//
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
//
// These are the C API interfaces to the llbuild library.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_PUBLIC_BUILDVALUE_H
#define LLBUILD_PUBLIC_BUILDVALUE_H

#ifndef LLBUILD_PUBLIC_LLBUILD_H
#error Clients must include the "llbuild.h" umbrella header.
#endif

LLBUILD_ASSUME_NONNULL_BEGIN

typedef enum LLBUILD_ENUM_ATTRIBUTES {
  /// An invalid value, for sentinel purposes.
  llb_build_value_kind_invalid = 0,

  /// A value produced by a virtual input.
  llb_build_value_kind_virtual_input LLBUILD_SWIFT_NAME(virtualInput) = 1,

  /// A value produced by an existing input file.
  llb_build_value_kind_existing_input LLBUILD_SWIFT_NAME(existingInput) = 2,

  /// A value produced by a missing input file.
  llb_build_value_kind_missing_input LLBUILD_SWIFT_NAME(missingInput) = 3,

  /// The contents of a directory.
  llb_build_value_kind_directory_contents LLBUILD_SWIFT_NAME(directoryContents) = 4,

  /// The signature of a directories contents.
  llb_build_value_kind_directory_tree_signature LLBUILD_SWIFT_NAME(directoryTreeSignature) = 5,

  /// The signature of a directories structure.
  llb_build_value_kind_directory_tree_structure_signature LLBUILD_SWIFT_NAME(directoryTreeStructureSignature) = 6,

  /// A value produced by stale file removal.
  llb_build_value_kind_stale_file_removal LLBUILD_SWIFT_NAME(staleFileRemoval) = 7,

  /// A value produced by a command which succeeded, but whose output was missing.
  llb_build_value_kind_missing_output LLBUILD_SWIFT_NAME(missingOutput) = 8,

  /// A value for a produced output whose command failed or was cancelled.
  llb_build_value_kind_failed_input LLBUILD_SWIFT_NAME(failedInput) = 9,

  /// A value produced by a successful command.
  llb_build_value_kind_successful_command LLBUILD_SWIFT_NAME(successfulCommand) = 10,

  /// A value produced by a failing command.
  llb_build_value_kind_failed_command LLBUILD_SWIFT_NAME(failedCommand) = 11,

  /// A value produced by a command which was skipped because one of its dependencies failed.
  llb_build_value_kind_propagated_failure_command LLBUILD_SWIFT_NAME(propagatedFailureCommand) = 12,

  /// A value produced by a command which was cancelled.
  llb_build_value_kind_cancelled_command LLBUILD_SWIFT_NAME(cancelledCommand) = 13,

  /// A value produced by a command which was skipped.
  llb_build_value_kind_skipped_command LLBUILD_SWIFT_NAME(skippedCommand) = 14,

  /// Sentinel value representing the result of "building" a top-level target.
  llb_build_value_kind_target = 15,

  /// The filtered contents of a directory.
  llb_build_value_kind_filtered_directory_contents LLBUILD_SWIFT_NAME(filteredDirectoryContents) = 16,

  /// A value produced by a successful command with an output signature.
  llb_build_value_kind_successful_command_with_output_signature LLBUILD_SWIFT_NAME(successfulCommandWithOutputSignature) = 17,
  
} llb_build_value_kind_t LLBUILD_SWIFT_NAME(BuildValueKind);

typedef struct llb_build_value_file_timestamp_t_ {
  uint64_t seconds;
  uint64_t nanoseconds;
} llb_build_value_file_timestamp_t LLBUILD_SWIFT_NAME(BuildValueFileTimestamp);

typedef struct llb_build_value_file_info_t_ {
  /// The device number.
  uint64_t device;
  /// The inode number.
  uint64_t inode;
  /// The mode flags of the file.
  uint64_t mode;
  /// The size of the file.
  uint64_t size;
  /// The modification time of the file.
  llb_build_value_file_timestamp_t modTime;
} llb_build_value_file_info_t LLBUILD_SWIFT_NAME(BuildValueFileInfo);

typedef uint64_t llb_build_value_command_signature_t LLBUILD_SWIFT_NAME(BuildValueCommandSignature);

typedef struct llb_build_value_ llb_build_value;

LLBUILD_EXPORT llb_build_value * llb_build_value_make(llb_data_t * data);
LLBUILD_EXPORT llb_build_value_kind_t llb_build_value_get_kind(llb_build_value * value);
LLBUILD_EXPORT void llb_build_value_get_value_data(llb_build_value * value, void *_Nullable context, void (*_Nullable iteration)(void *_Nullable context, uint8_t data));
LLBUILD_EXPORT void llb_build_value_destroy(llb_build_value * value);


// Invalid
LLBUILD_EXPORT llb_build_value * llb_build_value_make_invalid();

// Virtual Input
LLBUILD_EXPORT llb_build_value * llb_build_value_make_virtual_input();

// Existing Input
LLBUILD_EXPORT llb_build_value * llb_build_value_make_existing_input(llb_build_value_file_info_t fileInfo);
LLBUILD_EXPORT llb_build_value_file_info_t llb_build_value_get_output_info(llb_build_value * value);

// Missing Input
LLBUILD_EXPORT llb_build_value * llb_build_value_make_missing_input();

// Directory Contents
LLBUILD_EXPORT llb_build_value * llb_build_value_make_directory_contents(llb_build_value_file_info_t directoryInfo, const char *_Nonnull const *_Nonnull values, int32_t count_values);
LLBUILD_EXPORT void llb_build_value_get_directory_contents(llb_build_value * value, void *_Nullable context, void (* iterator)(void *_Nullable context, llb_data_t data));

// Directory Tree Signature
LLBUILD_EXPORT llb_build_value * llb_build_value_make_directory_tree_signature(llb_build_value_command_signature_t signature);
LLBUILD_EXPORT llb_build_value_command_signature_t llb_build_value_get_directory_tree_signature(llb_build_value * value);

// Directory Tree Structure Signature
LLBUILD_EXPORT llb_build_value * llb_build_value_make_directory_tree_structure_signature(llb_build_value_command_signature_t signature);
LLBUILD_EXPORT llb_build_value_command_signature_t llb_build_value_get_directory_tree_structure_signature(llb_build_value * value);


// Missing Output
LLBUILD_EXPORT llb_build_value * llb_build_value_make_missing_output();

// Failed Input
LLBUILD_EXPORT llb_build_value * llb_build_value_make_failed_input();

// Successful Command
LLBUILD_EXPORT llb_build_value * llb_build_value_make_successful_command(const llb_build_value_file_info_t * outputInfos, int32_t count_outputInfos);
LLBUILD_EXPORT void llb_build_value_get_file_infos(llb_build_value * value, void *_Nullable context, void (* iterator)(void *_Nullable context, llb_build_value_file_info_t fileInfo));

// Failed Command
LLBUILD_EXPORT llb_build_value * llb_build_value_make_failed_command();

// Propagated Failure Command
LLBUILD_EXPORT llb_build_value * llb_build_value_make_propagated_failure_command();

// Cancelled Command
LLBUILD_EXPORT llb_build_value * llb_build_value_make_cancelled_command();

// Skipped Command
LLBUILD_EXPORT llb_build_value * llb_build_value_make_skipped_command();

// Target
LLBUILD_EXPORT llb_build_value * llb_build_value_make_target();

// Stale File Removal
LLBUILD_EXPORT llb_build_value * llb_build_value_make_stale_file_removal(const char *_Nonnull const *_Nonnull values, int32_t count_values);
LLBUILD_EXPORT void llb_build_value_get_stale_file_list(llb_build_value * value, void *_Nullable context, void(* iterator)(void *_Nullable context, llb_data_t data));

// Filtered Directory Contents
LLBUILD_EXPORT llb_build_value * llb_build_value_make_filtered_directory_contents(const char *_Nonnull const *_Nonnull values, int32_t count_values);

// Successful Command With Output Signature
LLBUILD_EXPORT llb_build_value * llb_build_value_make_successful_command_with_output_signature(const llb_build_value_file_info_t * outputInfos, int32_t count_outputInfos, llb_build_value_command_signature_t signature);
LLBUILD_EXPORT llb_build_value_command_signature_t llb_build_value_get_output_signature(llb_build_value * value);

LLBUILD_ASSUME_NONNULL_END

#endif
