//===- unittests/CAPI/BuildSystem-C-API.cpp -------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/llbuild.h"
#include "llbuild/buildsystem.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <stdlib.h>
#include <string.h>

#include "gtest/gtest.h"

namespace {

// Reads file contents to a string (private helper function for unit test).
static std::string readFileContents(std::string path) {
  FILE * fp = fopen(path.c_str(), "rb");
  std::string str;
  if (fp) {
    fseek(fp, 0, SEEK_END);
    uint64_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    str.resize(size);
    size_t pos = 0;
    while (pos < size) {
      size_t result = fread((void *)(str.data() + pos), 1, size - pos, fp);
      pos += result;
    }
    fclose(fp);
  }
  return str;
}

// Writes file contents from a string (private helper function for unit test).
static void writeFileContents(std::string path, std::string str) {
  FILE * fp = fopen(path.c_str(), "wb");
  if (fp) {
    const size_t size = str.size();
    size_t pos = 0;
    while (pos < size) {
      size_t result = fwrite((void *)(str.data() + pos), 1, size - pos, fp);
      pos += result;
    }
    fclose(fp);
  }
}

static void depinfo_tester_command_start(void* context,
                                         llb_buildsystem_command_t* command,
                                         llb_buildsystem_interface_t* bi,
                                         llb_task_interface_t ti) {}

static void depinfo_tester_command_provide_value(void* context,
                                                 llb_buildsystem_command_t* command,
                                                 llb_buildsystem_interface_t* bi,
                                                 llb_task_interface_t ti,
                                                 const llb_build_value* value,
                                                 uintptr_t inputID) {}
  
static bool
depinfo_tester_command_execute_command(void *context,
                                       llb_buildsystem_command_t* command,
                                       llb_buildsystem_interface_t* bi,
                                       llb_task_interface_t ti,
                                       llb_buildsystem_queue_job_context_t* job) {
  // The tester tool is given a direct input file whose only contents are the
  // path of another, indirect input file.  It is also given the paths to which
  // it should emit the output file and an ld-style dependency-info file.  It
  // copies the indirect input file to the output file, and creates the depinfo
  // file, recording the direct and indirect input files as input dependencies.
  
  // Because the llb_buildsystem_* API doesn't seem to currently give us access
  // to the command inputs and outputs, and because extending it to do so would
  // be beyond the scope of the change being tested here, we encode the paths of
  // the input, output, and dep-info files in the command the description, which
  // we can access using llb_buildsystem_* calls.
  
  // So we get the description, which is all we have access to, and rely on the
  // use of the pipe character as a record separator in the unit test manifest.
  // llb_buildsystem_command_get_description() function is documented to return
  // a mutable copy that we then have to free, so we're free to modify it.
  char * desc = llb_buildsystem_command_get_description(command);
  char * ptr = desc;
  llbuild::basic::sys::strsep(&ptr, "|"); // skip over rule name
  std::string directInputPath = llbuild::basic::sys::strsep(&ptr, "|");
  puts(directInputPath.c_str());
  std::string outputPath = llbuild::basic::sys::strsep(&ptr, "|");
  puts(outputPath.c_str());
  std::string depInfoPath = llbuild::basic::sys::strsep(&ptr, "|");
  puts(depInfoPath.c_str());
  
  // Read the absolute path of the indirect input from the direct input.
  std::string indirectInputPath = readFileContents(directInputPath);
  if (indirectInputPath.empty()) {
    return false;
  }
  
  // Read the contents of the indirect input.
  std::string indirectContents = readFileContents(indirectInputPath);
  if (indirectContents.empty()) {
    return false;
  }
  
  // Write the contents of the indirect input to the output.
  writeFileContents(outputPath, indirectContents);
  
  // Write out the ld-style dependency info file, which consists of a sequence
  // of records.  Each record consists of a type byte followed by a C string
  // (i.e. null-terminated).  Type zero is an information string about the tool
  // that produced the dep-info file (commonly containing its name and version)
  // and type 0x10 is plain input file.
  std::string depInfoContents;
  depInfoContents.append("\0", 1);
  depInfoContents.append(std::string("version"));
  depInfoContents.append("\0", 1);
  depInfoContents.append("\020");
  depInfoContents.append(directInputPath);
  depInfoContents.append("\0", 1);
  depInfoContents.append("\020");
  depInfoContents.append(indirectInputPath);
  depInfoContents.append("\0", 1);
  writeFileContents(depInfoPath, depInfoContents);

  // Clean up.
  llb_free(desc);
  return true;
}

static llb_buildsystem_command_t*
depinfo_tester_tool_create_command(void *context, const llb_data_t* name) {
  llb_buildsystem_external_command_delegate_t delegate;
  delegate.context = NULL;
  delegate.get_signature = NULL;
  delegate.start = depinfo_tester_command_start;
  delegate.provide_value = depinfo_tester_command_provide_value;
  delegate.execute_command = depinfo_tester_command_execute_command;
  delegate.execute_command_ex = NULL;
  delegate.is_result_valid = NULL;
  return llb_buildsystem_external_command_create(name, delegate);
}

static bool fs_get_file_contents(void* context, const char* path,
                                llb_data_t* data_out) {
#if defined(_WIN32)
  llvm::SmallVector<llvm::UTF16, 20> wPath;
  llvm::convertUTF8ToUTF16String(path, wPath);
  wprintf(L" -- read file contents: %ls\n", (LPCWSTR)wPath.data());
  fflush(stdout);
  FILE* fp;
  if (_wfopen_s(&fp, (LPCWSTR)wPath.data(), L"rb")) {
    return false;
  }
#else
  printf(" -- read file contents: %s\n", path);
  fflush(stdout);

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }
#endif

  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t* buffer = (uint8_t*)llb_alloc(size);
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
  if (name->length == 26 && memcmp(name->data, "custom-depinfo-tester-tool", 5) == 0) {
    llb_buildsystem_tool_delegate_t delegate = {};
    delegate.create_command = depinfo_tester_tool_create_command;
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
  llb_free(description);
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

static void command_found_discovered_dependency(void* context,
                                                llb_buildsystem_command_t* command,
                                                const char *path,
                                                llb_buildsystem_discovered_dependency_kind_t kind) {
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
                                     const llb_buildsystem_command_extended_result_t* result) {
}

TEST(BuildSystemCAPI, CustomToolWithDiscoveredDependencies) {
  std::string tmpDirPath = llbuild::basic::sys::makeTmpDir();
  for (auto& c : tmpDirPath) {
    if (c == '\\') {
      c = '/';
    }
  }
  // We write out an indirectly referenced file containing data to be copied to
  // the output file.
  std::string indirectInputFilePath = tmpDirPath + "/" + "indirect-input-file";
  writeFileContents(indirectInputFilePath, "1");
  
  // Write out a directly referenced file containing the path of the indirectly
  // referenced file.
  std::string directInputFilePath = tmpDirPath + "/" + "direct-input-file";
  writeFileContents(directInputFilePath, indirectInputFilePath);
  
  // The output file will be written by the tool.
  std::string outputFilePath = tmpDirPath + "/" + "output-file";
  
  // The dependency info file will also be written by the tool.
  std::string depInfoFilePath = tmpDirPath + "/" + "dep-info-file";
  
  // Write out a build manifest containing node definitions and a build rule to
  // use a custom rule to copy the contents of the indirect input file to the
  // output file, via the path in the direct input file.
  std::string buildFilePath = tmpDirPath + "/llbuild";  
  std::ostringstream buildFileContents;
  buildFileContents << "client:" << std::endl;
  buildFileContents << "  name: basic" << std::endl;
  buildFileContents << std::endl;
  buildFileContents << "targets:" << std::endl;
  buildFileContents << "  \"\": [\"<all>\"]" << std::endl;
  buildFileContents << std::endl;
  buildFileContents << "nodes:" << std::endl;
  buildFileContents << "  \"" << outputFilePath << "\": {}" << std::endl;
  buildFileContents << std::endl;
  buildFileContents << "commands:" << std::endl;
  buildFileContents << "  \"copy-indirectly|" << directInputFilePath << "|" << outputFilePath << "|" << depInfoFilePath << "\":" << std::endl;
  buildFileContents << "    tool: custom-depinfo-tester-tool" << std::endl;
  buildFileContents << "    inputs: [\"" << directInputFilePath << "\"]" << std::endl;
  buildFileContents << "    outputs: [\"" << outputFilePath << "\", \"" << depInfoFilePath << "\"]" << std::endl;
  buildFileContents << "    deps: \"" << depInfoFilePath << "\"" << std::endl;
  buildFileContents << "  \"<all>\":" << std::endl;
  buildFileContents << "    tool: phony" << std::endl;
  buildFileContents << "    inputs: [\"" << outputFilePath << "\"]" << std::endl;
  buildFileContents << "    outputs: [\"<all>\"]" << std::endl;
  writeFileContents(buildFilePath, buildFileContents.str());

  // Create an invocation.
  llb_buildsystem_invocation_t invocation = {};
  invocation.buildFilePath = buildFilePath.c_str();
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
  delegate.command_found_discovered_dependency = command_found_discovered_dependency;
  delegate.command_process_started = command_process_started;
  delegate.command_process_had_error = command_process_had_error;
  delegate.command_process_had_output = command_process_had_output;
  delegate.command_process_finished = command_process_finished;
  
  // Create a build system.
  llb_buildsystem_t* system = llb_buildsystem_create(delegate, invocation);
  
  // Initialize the system.
  llb_buildsystem_initialize(system);
  
  // Build the default target once.
  llb_data_t key = { 0, NULL };
  printf("initial build:\n");
  if (!llb_buildsystem_build(system, &key)) {
    printf("build had command failures\n");
  }
  
  // Check that the output file was produced and has the expected contents.
  std::string outputContentsFirstBuild = readFileContents(outputFilePath);
  EXPECT_EQ(outputContentsFirstBuild, "1");
  
  // Wait for at least a second because some filesystem only have file mod
  // times on the granularity of a second.
  // FIXME: might be better to just fake the timestamp on the file
  llbuild::basic::sys::sleep(1);

  // Change the contents of the indirect (not the direct) file.
  writeFileContents(indirectInputFilePath, "2");
  
  // Build the target again.
  printf("second build:\n");
  if (!llb_buildsystem_build(system, &key)) {
    printf("build had command failures\n");
  }
  
  // Check that the output file was updated and has the expected contents.
  std::string outputContentsSecondBuild = readFileContents(outputFilePath);
  EXPECT_EQ(outputContentsSecondBuild, "2");
  
  // Destroy the build system.
  llb_buildsystem_destroy(system);
}

}
