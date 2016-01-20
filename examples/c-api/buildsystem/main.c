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

static const char* progname;
static void usage() {
  fprintf(stderr, "usage: %s <build file path>\n", progname);
  exit(0);
}

static const char* basename(const char* path) {
  const char* result = strrchr(path, '/');
  return result ? result : path;
}

static void command_started(void* context,
                            llb_buildsystem_command_t* command) {
  char* description = llb_buildsystem_command_get_description(command);
  llb_data_t name;
  llb_buildsystem_command_get_name(command, &name);
  printf("%s: %.*s -- %s\n", __FUNCTION__, (int)name.length, name.data,
         description);
  free(description);
}

static void command_finished(void* context,
                             llb_buildsystem_command_t* command) {
  llb_data_t name;
  llb_buildsystem_command_get_name(command, &name);
  printf("%s: %.*s\n", __FUNCTION__, (int)name.length, name.data);
}

static void command_process_started(void* context,
                                    llb_buildsystem_command_t* command,
                                    llb_buildsystem_process_t* process) {
}

static void command_process_had_output(void* context,
                                       llb_buildsystem_command_t* command,
                                       llb_buildsystem_process_t* process,
                                       const llb_data_t* data) {
  llb_data_t name;
  llb_buildsystem_command_get_name(command, &name);
  printf("%s: %.*s\n", __FUNCTION__, (int)name.length, name.data);
  fwrite(data->data, data->length, 1, stdout);
}

static void command_process_finished(void* context,
                                     llb_buildsystem_command_t* command,
                                     llb_buildsystem_process_t* process,
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

  // Create a build system delegate.
  llb_buildsystem_delegate_t delegate = {};
  delegate.context = NULL;
  delegate.command_started = command_started;
  delegate.command_finished = command_finished;
  delegate.command_process_started = command_process_started;
  delegate.command_process_had_output = command_process_had_output;
  delegate.command_process_finished = command_process_finished;
    
  // Create a build system.
  llb_buildsystem_t* system = llb_buildsystem_create(delegate, invocation);
    
  // Build the default target.
  llb_data_t key = { 0, NULL };
  llb_buildsystem_build(system, &key);

  // Destroy the build system.
  llb_buildsystem_destroy(system);
    
  return 0;
}
