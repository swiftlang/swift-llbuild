//===- main.c -------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2015 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file contains a basic example of using the libllbuild C APIs for working
// with the BuildSystem component.
//
//===----------------------------------------------------------------------===//

#include <llbuild/llbuild.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

static const char* progname;
static void usage() {
  fprintf(stderr, "usage: %s <build file path>\n", progname);
  exit(0);
}

static const char* basename(const char* path) {
  const char* result = strrchr(path, '/');
  return result ? result : path;
}

// "Fancy" Command Implementation

static bool
fancy_command_execute_command(
    void *context, llb_buildsystem_command_t* command,
    llb_buildsystem_command_interface_t* bsci, llb_task_t* task,
    llb_buildsystem_queue_job_context_t* job) {
  printf("%s\n", __FUNCTION__);
  fflush(stdout);
  return true;
}

// "Fancy" Tool Implementation

static llb_buildsystem_command_t*
fancy_tool_create_command(void *context, const llb_data_t* name) {
  llb_buildsystem_external_command_delegate_t delegate;
  delegate.context = NULL;
  delegate.get_signature = NULL;
  delegate.execute_command = fancy_command_execute_command;
  return llb_buildsystem_external_command_create(name, delegate);
}

// Build System Implementation

static bool fs_get_file_contents(void* context, const char* path,
                                 llb_data_t* data_out) {
  printf(" -- read file contents: %s\n", path);
  fflush(stdout);

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }
  
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t* buffer = malloc(size);
  if (!buffer) {
    return false;
  }
  data_out->data = buffer;
  data_out->length = size;
  int n = fread(buffer, 1, size, fp);
  if (n != size) {
    return false;
  }

  return true;
}

static void fs_get_file_info(void* context, const char* path,
                             llb_fs_file_info_t* file_info_out) {
  printf(" -- stat: %s\n", path);
  fflush(stdout);

  struct stat buf;
  if (stat(path, &buf) != 0) {
    memset(file_info_out, 0, sizeof(*file_info_out));
    return;
  }

  file_info_out->device = buf.st_dev;
  file_info_out->inode = buf.st_ino;
  file_info_out->size = buf.st_size;
  file_info_out->mod_time.seconds = buf.st_mtime;
  file_info_out->mod_time.nanoseconds = 0;
}

static llb_buildsystem_tool_t* lookup_tool(void *context,
                                           const llb_data_t* name) {
  if (name->length == 5 && memcmp(name->data, "fancy", 5) == 0) {
    llb_buildsystem_tool_delegate_t delegate = {};
    delegate.create_command = fancy_tool_create_command;
    return llb_buildsystem_tool_create(name, delegate);
  }

  return NULL;
}

static void handle_diagnostic(void* context,
                              llb_buildsystem_diagnostic_kind_t kind,
                              const char* filename, int line, int column,
                              const char* message) {
  const char* kindName = llb_buildsystem_diagnostic_kind_get_name(kind);
  printf("%s:%d: %s: %s\n", filename, line, kindName, message);
  fflush(stdout);
}

static void had_command_failure(void* context_p) {
  printf("%s\n", __FUNCTION__);
  fflush(stdout);
}

static void command_started(void* context,
                            llb_buildsystem_command_t* command) {
  char* description = llb_buildsystem_command_get_description(command);
  llb_data_t name;
  llb_buildsystem_command_get_name(command, &name);
  printf("%s: %.*s -- %s\n", __FUNCTION__, (int)name.length, name.data,
         description);
  free(description);
  fflush(stdout);
}

static void command_finished(void* context,
                             llb_buildsystem_command_t* command,
                             llb_buildsystem_command_result_t result) {
  llb_data_t name;
  llb_buildsystem_command_get_name(command, &name);
  printf("%s: %.*s\n", __FUNCTION__, (int)name.length, name.data);
  fflush(stdout);
}

static void command_process_started(void* context,
                                    llb_buildsystem_command_t* command,
                                    llb_buildsystem_process_t* process) {
}

static void command_process_had_error(void* context,
                                       llb_buildsystem_command_t* command,
                                       llb_buildsystem_process_t* process,
                                       const llb_data_t* data) {
  llb_data_t name;
  llb_buildsystem_command_get_name(command, &name);
  printf("%s: %.*s\n", __FUNCTION__, (int)name.length, name.data);
  fwrite(data->data, data->length, 1, stdout);
  fflush(stdout);
}

static void command_process_had_output(void* context,
                                       llb_buildsystem_command_t* command,
                                       llb_buildsystem_process_t* process,
                                       const llb_data_t* data) {
  llb_data_t name;
  llb_buildsystem_command_get_name(command, &name);
  printf("%s: %.*s\n", __FUNCTION__, (int)name.length, name.data);
  fwrite(data->data, data->length, 1, stdout);
  fflush(stdout);
}

static void command_process_finished(void* context,
                                     llb_buildsystem_command_t* command,
                                     llb_buildsystem_process_t* process,
                                     llb_buildsystem_command_result_t result,
                                     int exit_status) {
}

int main(int argc, char **argv) {
  progname = basename(argv[0]);

  if (argc != 2) {
    usage();
  }

  const char* buildFilePath = argv[1];

  // Create an invocation.
  llb_buildsystem_invocation_t invocation = {};
  invocation.buildFilePath = buildFilePath;
  invocation.useSerialBuild = true;

  // Create a build system delegate.
  llb_buildsystem_delegate_t delegate = {};
  delegate.context = NULL;
  delegate.fs_get_file_contents = fs_get_file_contents;
  delegate.fs_get_file_info = fs_get_file_info;
  delegate.lookup_tool = lookup_tool;
  delegate.handle_diagnostic = handle_diagnostic;
  delegate.had_command_failure = had_command_failure;
  delegate.command_started = command_started;
  delegate.command_finished = command_finished;
  delegate.command_process_started = command_process_started;
  delegate.command_process_had_error = command_process_had_error;
  delegate.command_process_had_output = command_process_had_output;
  delegate.command_process_finished = command_process_finished;
    
  // Create a build system.
  llb_buildsystem_t* system = llb_buildsystem_create(delegate, invocation);

  // Initialize the system.
  llb_buildsystem_initialize(system);
  
  // Build the default target, twice.
  llb_data_t key = { 0, NULL };
  printf("initial build:\n");
  if (!llb_buildsystem_build(system, &key)) {
    printf("build had command failures\n");
  }
  printf("second build:\n");
  if (!llb_buildsystem_build(system, &key)) {
    printf("build had command failures\n");
  }    

  // Destroy the build system.
  llb_buildsystem_destroy(system);
    
  return 0;
}
