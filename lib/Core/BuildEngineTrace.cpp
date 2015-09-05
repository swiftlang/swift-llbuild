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
#include <cstring>

#include <errno.h>

using namespace llbuild;
using namespace llbuild::core;

BuildEngineTrace::BuildEngineTrace() {}

BuildEngineTrace::~BuildEngineTrace() {}

bool BuildEngineTrace::open(const std::string& filename,
                            std::string* error_out) {
  assert(!isOpen());

  FILE *fp = fopen(filename.c_str(), "wb");
  if (!fp) {
    *error_out = "unable to open '" + filename + "' (" +
      ::strerror(errno) + ")";
    return false;
  }
  outputPtr = fp;
  assert(isOpen());

  // Write the opening header.
  fprintf(fp, "[\n");
  return true;
}

bool BuildEngineTrace::close(std::string* error_out) {
  assert(isOpen());

  FILE *fp = static_cast<FILE*>(outputPtr);

  // Write the footer.
  fprintf(fp, "]\n");

  bool success = fclose(fp) == 0;
  outputPtr = nullptr;
  assert(!isOpen());

  if (!success) {
    *error_out = "unable to close file";
    return false;
  }

  return true;
}

#pragma mark - Tracing APIs

const char* BuildEngineTrace::getTaskName(const Task* task) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  // See if we have already assigned a name.
  auto it = taskNames.find(task);
  if (it != taskNames.end())
    return it->second.c_str();

  // Otherwise, create a name.
  char name[64];
  sprintf(name, "T%d", ++numNamedTasks);
  auto result = taskNames.emplace(task, name);

  // Report the newly seen rule.
  fprintf(fp, "{ \"new-task\", \"%s\" },\n", name);

  return result.first->second.c_str();
}

const char* BuildEngineTrace::getRuleName(const Rule* rule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  // See if we have already assigned a name.
  auto it = ruleNames.find(rule);
  if (it != ruleNames.end())
    return it->second.c_str();

  // Otherwise, create a name.
  char name[64];
  sprintf(name, "R%d", ++numNamedRules);
  auto result = ruleNames.emplace(rule, name);

  // Report the newly seen rule.
  fprintf(fp, "{ \"new-rule\", \"%s\", \"%s\" },\n", name, rule->key.c_str());

  return result.first->second.c_str();
}

void BuildEngineTrace::buildStarted() {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"build-started\" },\n");
}

void BuildEngineTrace::handlingBuildInputRequest(const Rule* rule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"handling-build-input-request\", \"%s\" },\n",
          getRuleName(rule));
}

void BuildEngineTrace::createdTaskForRule(const Task* task,
                                          const Rule* rule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"created-task-for-rule\", \"%s\", \"%s\" },\n",
          getTaskName(task), getRuleName(rule));
}

void BuildEngineTrace::handlingTaskInputRequest(const Task* task,
                                                const Rule* rule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"handling-task-input-request\", \"%s\", \"%s\" },\n",
          getTaskName(task), getRuleName(rule));
}

void BuildEngineTrace::pausedInputRequestForRuleScan(const Rule* rule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"paused-input-request-for-rule-scan\", \"%s\" },\n",
          getRuleName(rule));
}

void BuildEngineTrace::readyingTaskInputRequest(const Task* task,
                                                const Rule* rule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"readying-task-input-request\", \"%s\", \"%s\" },\n",
          getTaskName(task), getRuleName(rule));
}

void BuildEngineTrace::addedRulePendingTask(const Rule* rule,
                                            const Task* task) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"added-rule-pending-task\", \"%s\", \"%s\" },\n",
          getRuleName(rule), getTaskName(task));
}

void BuildEngineTrace::completedTaskInputRequest(const Task* task,
                                                 const Rule* rule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"completed-task-input-request\", \"%s\", \"%s\" },\n",
          getTaskName(task), getRuleName(rule));
}

void BuildEngineTrace::updatedTaskWaitCount(const Task* task,
                                            unsigned waitCount) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"updated-task-wait-count\", \"%s\", %d },\n",
          getTaskName(task), waitCount);
}

void BuildEngineTrace::unblockedTask(const Task* task) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"unblocked-task\", \"%s\" },\n", getTaskName(task));
}

void BuildEngineTrace::readiedTask(const Task* task, const Rule* rule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"readied-task\", \"%s\", \"%s\" },\n",
          getTaskName(task), getRuleName(rule));
}

void BuildEngineTrace::finishedTask(const Task* task, const Rule* rule,
                                    bool wasChanged) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"finished-task\", \"%s\", \"%s\", \"%s\" },\n",
          getTaskName(task), getRuleName(rule),
          wasChanged ? "changed" : "unchanged");

  // Delete the task entry, as it could be reused.
  taskNames.erase(taskNames.find(task));
}

void BuildEngineTrace::buildEnded() {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"build-ended\" },\n");
}

#pragma mark - Dependency Scanning Tracing APIs

void BuildEngineTrace::checkingRuleNeedsToRun(const Rule* forRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"checking-rule-needs-to-run\", \"%s\" },\n",
          getRuleName(forRule));
}

void BuildEngineTrace::ruleScheduledForScanning(const Rule* forRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, ("{ \"rule-scheduled-for-scanning\", \"%s\"},\n"),
          getRuleName(forRule));
}

void BuildEngineTrace::ruleScanningNextInput(const Rule* forRule,
                                             const Rule* inputRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, ("{ \"rule-scanning-next-input\", \"%s\", \"%s\" },\n"),
          getRuleName(forRule), getRuleName(inputRule));
}

void
BuildEngineTrace::ruleScanningDeferredOnInput(const Rule* forRule,
                                              const Rule* inputRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, ("{ \"rule-scanning-deferred-on-input\", \"%s\", \"%s\" },\n"),
          getRuleName(forRule), getRuleName(inputRule));
}

void
BuildEngineTrace::ruleScanningDeferredOnTask(const Rule* forRule,
                                             const Task* inputTask) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, ("{ \"rule-scanning-deferred-on-task\", \"%s\", \"%s\" },\n"),
          getRuleName(forRule), getTaskName(inputTask));
}

void BuildEngineTrace::ruleNeedsToRunBecauseNeverBuilt(const Rule* forRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"rule-needs-to-run\", \"%s\", \"never-built\" },\n",
          getRuleName(forRule));
}

void BuildEngineTrace::ruleNeedsToRunBecauseInvalidValue(const Rule* forRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"rule-needs-to-run\", \"%s\", \"invalid-value\" },\n",
          getRuleName(forRule));
}

void
BuildEngineTrace::ruleNeedsToRunBecauseInputMissing(const Rule* forRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"rule-needs-to-run\", \"%s\", \"input-missing\" },\n",
          getRuleName(forRule));
}

void
BuildEngineTrace::ruleNeedsToRunBecauseInputRebuilt(const Rule* forRule,
                                                    const Rule* inputRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, ("{ \"rule-needs-to-run\", \"%s\", "
               "\"input-rebuilt\", \"%s\" },\n"),
          getRuleName(forRule), getRuleName(inputRule));
}

void BuildEngineTrace::ruleDoesNotNeedToRun(const Rule* forRule) {
  FILE *fp = static_cast<FILE*>(outputPtr);

  fprintf(fp, "{ \"rule-does-not-need-to-run\", \"%s\" },\n",
          getRuleName(forRule));
}

