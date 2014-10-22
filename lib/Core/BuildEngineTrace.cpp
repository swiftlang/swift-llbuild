//===-- BuildEngineTrace.cpp ----------------------------------------------===//
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

#include "BuildEngineTrace.h"

#include "llbuild/Core/BuildEngine.h"

#include <cassert>
#include <cstdio>

#include <errno.h>

using namespace llbuild;
using namespace llbuild::core;

BuildEngineTrace::BuildEngineTrace() : OutputPtr(0), IsOpen(false) {}

BuildEngineTrace::~BuildEngineTrace() {
  assert(!IsOpen);
}

bool BuildEngineTrace::open(const std::string& Filename,
                            std::string* Error_Out) {
  assert(!IsOpen);

  FILE *fp = fopen(Filename.c_str(), "wb");
  if (!fp) {
    *Error_Out = "unable to open '" + Filename + "' (" +
      ::strerror(errno) + ")";
    return false;
  }
  OutputPtr = fp;
  IsOpen = true;

  // Write the opening header.
  fprintf(fp, "[\n");
  return true;
}

bool BuildEngineTrace::close(std::string* Error_Out) {
  assert(IsOpen);

  FILE *FP = static_cast<FILE*>(OutputPtr);

  // Write the footer.
  fprintf(FP, "]\n");

  bool Success = fclose(FP) == 0;
  OutputPtr = nullptr;
  IsOpen = false;

  if (!Success) {
    *Error_Out = "unable to close file";
    return false;
  }

  return true;
}

#pragma mark - Tracing APIs

void BuildEngineTrace::createdTaskForRule(const Task* Task,
                                          const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"created-task-for-rule\", \"%s\", \"%s\" },\n",
          Task->Name.c_str(), Rule->Key.c_str());
}

void BuildEngineTrace::handlingTaskInputRequest(const Task* Task,
                                                const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"handling-task-input-request\", \"%s\", \"%s\" },\n",
          Task->Name.c_str(), Rule->Key.c_str());
}

void BuildEngineTrace::readyingTaskInputRequest(const Task* Task,
                                                const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"readying-task-input-request\", \"%s\", \"%s\" },\n",
          Task->Name.c_str(), Rule->Key.c_str());
}

void BuildEngineTrace::addedRulePendingTask(const Rule* Rule,
                                            const Task* Task) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"added-rule-pending-task\", \"%s\", \"%s\" },\n",
          Rule->Key.c_str(), Task->Name.c_str());
}

void BuildEngineTrace::completedTaskInputRequest(const Task* Task,
                                                 const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"completed-task-input-request\", \"%s\", \"%s\" },\n",
          Task->Name.c_str(), Rule->Key.c_str());
}

void BuildEngineTrace::updatedTaskWaitCount(const Task* Task,
                                            unsigned WaitCount) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"updated-task-wait-count\", \"%s\", %d },\n",
          Task->Name.c_str(), WaitCount);
}

void BuildEngineTrace::unblockedTask(const Task* Task) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"unblocked-task\", \"%s\" },\n", Task->Name.c_str());
}

void BuildEngineTrace::finishedTask(const Task* Task, const Rule* Rule) {
  FILE *FP = static_cast<FILE*>(OutputPtr);

  fprintf(FP, "{ \"finished-task\", \"%s\", \"%s\" },\n",
          Task->Name.c_str(), Rule->Key.c_str());
}
