//===- BuildEngineTrace.h ---------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_BUILDENGINETRACE_H
#define LLBUILD_CORE_BUILDENGINETRACE_H

#include <string>
#include <unordered_map>

namespace llbuild {
namespace core {

class Rule;
class Task;

/// This class assists in writing build engine tracing information to an
/// external log file, suitable for ex post facto debugging and analysis.
class BuildEngineTrace {
    /// The output file pointer.
    void* outputPtr = nullptr;

    unsigned numNamedTasks = 0;
    std::unordered_map<const Task*, std::string> taskNames;
    unsigned numNamedRules = 0;
    std::unordered_map<const Rule*, std::string> ruleNames;

private:
    const char* getTaskName(const Task*);
    const char* getRuleName(const Rule*);

public:
    BuildEngineTrace();
    ~BuildEngineTrace();

    /// Open an output file for writing, must be called prior to any trace
    /// recording, and may only be called once per trace object.
    ///
    /// \returns True on success.
    bool open(const std::string& path, std::string* error_out);

    /// Close the output file; no subsequest trace recording may be done.
    ///
    /// \returns True on success.
    bool close(std::string* error_out);

    /// Check if the trace output is open.
    bool isOpen() const { return outputPtr != nullptr; }

    /// @name Trace Recording APIs
    /// @{

    /// @name Core Engine Operation
    /// @{

    void buildStarted();
    void handlingBuildInputRequest(const Rule* rule);
    void createdTaskForRule(const Task* task, const Rule* rule);
    void handlingTaskInputRequest(const Task* task, const Rule* rule);
    void pausedInputRequestForRuleScan(const Rule* rule);
    void readyingTaskInputRequest(const Task* task, const Rule* rule);
    void addedRulePendingTask(const Rule* rule, const Task* task);
    void completedTaskInputRequest(const Task* task, const Rule* rule);
    void updatedTaskWaitCount(const Task* task, unsigned waitCount);
    void unblockedTask(const Task* task);
    void readiedTask(const Task* task, const Rule* rule);
    void finishedTask(const Task* task, const Rule* rule, bool wasChanged);
    void buildEnded();

    /// @}

    /// @name Dependency Checking
    /// @{

    void checkingRuleNeedsToRun(const Rule* forRule);
    void ruleScheduledForScanning(const Rule* forRule);
    void ruleScanningNextInput(const Rule* forRule, const Rule* inputRule);
    void ruleScanningDeferredOnInput(const Rule* forRule,
                                     const Rule* inputRule);
    void ruleScanningDeferredOnTask(const Rule* forRule,
                                    const Task* inputTask);
    void ruleNeedsToRunBecauseNeverBuilt(const Rule* forRule);
    void ruleNeedsToRunBecauseInvalidValue(const Rule* forRule);
    void ruleNeedsToRunBecauseInputMissing(const Rule* forRule);
    void ruleNeedsToRunBecauseInputRebuilt(const Rule* forRule,
                                           const Rule* inputRule);
    void ruleDoesNotNeedToRun(const Rule* forRule);

    /// @}

    /// @}
};

}
}

#endif
