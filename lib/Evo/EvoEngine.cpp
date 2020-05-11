//===-- EvoEngine.cpp -----------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Evo/EvoEngine.h"

#include "llbuild/Basic/ExecutionQueue.h"
#include "llbuild/Basic/ShellUtility.h"

#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace llbuild;
using namespace llbuild::evo;

EvoEngine::~EvoEngine() { }
EvoRule::~EvoRule() { }

class EvoTask : public core::Task, public EvoEngine {
private:
  class ProcDescriptor;
  class TaskDescriptor;

private:
  EvoRule* rule;

  std::unique_ptr<std::thread> taskThread;
  std::mutex taskMutex;
  std::condition_variable taskCondition;

  core::TaskInterface coreInterface{nullptr, nullptr};
  bool engineState_inputsAvailable{false};
  uintptr_t engineState_pendingInputs{0};
  bool taskState_resultAvailable{false};

  std::vector<std::pair<bool, core::ValueType>> inputs;
  std::vector<std::pair<bool, ProcDescriptor>> procs;
  std::vector<std::pair<bool, TaskDescriptor>> tasks;

  core::ValueType nullvalue;
  EvoProcessResult nullproc;

public:
  EvoTask(EvoRule* rule) : rule(rule) { }
  ~EvoTask();

  // core::Task required methods
  void start(core::TaskInterface) override;
  void provideValue(core::TaskInterface, uintptr_t inputID, const core::ValueType& value) override;
  void inputsAvailable(core::TaskInterface) override;

  // EvoEngine methods
  EvoInputHandle request(const core::KeyType& key) override;
  EvoProcessHandle spawn(
    ArrayRef<StringRef> commandLine,
    ArrayRef<std::pair<StringRef, StringRef>> environment,
    basic::ProcessAttributes attributes
  ) override;
  EvoTaskHandle spawn(StringRef description, EvoTaskFn work) override;

  const core::ValueType& wait(EvoInputHandle) override;
  const EvoProcessResult& wait(EvoProcessHandle) override;
  const core::ValueType& wait(EvoTaskHandle) override;

private:
  void run();

  class ProcDescriptor : public basic::JobDescriptor, public basic::ProcessDelegate {
  private:
    const core::KeyType& key;
  public:
    ArrayRef<StringRef> commandLine;
    ArrayRef<std::pair<StringRef, StringRef>> environment;
    basic::ProcessAttributes attributes;
    EvoProcessResult result;

    ProcDescriptor(const core::KeyType& key,
                   ArrayRef<StringRef> commandLine,
                   ArrayRef<std::pair<StringRef, StringRef>> environment,
                   basic::ProcessAttributes attributes
    ) : key(key), commandLine(commandLine), environment(environment), attributes(attributes) {
    }

    StringRef getOrdinalName() const override { return StringRef(key.str()); }
    void getShortDescription(SmallVectorImpl<char> &result) const override {
      llvm::raw_svector_ostream(result) << getOrdinalName();
    }

    void getVerboseDescription(SmallVectorImpl<char> &result) const override {
      llvm::raw_svector_ostream os(result);
      bool first = true;
      for (const auto& arg: commandLine) {
        if (!first) os << " ";
        first = false;
        basic::appendShellEscapedString(os, arg);
      }
    }

    void processStarted(basic::ProcessContext* ctx,
                        basic::ProcessHandle handle) override { }

    void processHadError(basic::ProcessContext* ctx,
                         basic::ProcessHandle handle,
                         const Twine& message) override {
      result.errors.push_back(message.str());
    }

    void processHadOutput(basic::ProcessContext* ctx,
                          basic::ProcessHandle handle,
                          StringRef data) override {
      result.output += data;
    }

    void processFinished(basic::ProcessContext* ctx,
                         basic::ProcessHandle handle,
                         const basic::ProcessResult& result) override { }
  };

  class TaskDescriptor : public basic::JobDescriptor {
  private:
    std::string desc;
  public:
    core::ValueType value;

    TaskDescriptor(const core::KeyType& key, StringRef task) {
      desc = key.str();
      desc += task;
    }

    StringRef getOrdinalName() const override { return StringRef(desc); }
    void getShortDescription(SmallVectorImpl<char> &result) const override {
      llvm::raw_svector_ostream(result) << desc;
    }
    void getVerboseDescription(SmallVectorImpl<char> &result) const override {
      llvm::raw_svector_ostream(result) << desc;
    }
  };
};

core::Task* EvoRule::createTask(core::BuildEngine&) {
  return new EvoTask(this);
}


// MARK: - EvoTask - core run routine

void EvoTask::run() {
  core::ValueType value = rule->run(*this);

  // The core expects that we won't call taskIsComplete until all inputs are
  // available, thus we wait until that has happened.
  std::unique_lock<std::mutex> lock(taskMutex);
  taskState_resultAvailable = true;

  // Wake up the engine thread if it is waiting on the result.
  lock.unlock();
  taskCondition.notify_all();
  lock.lock();

  while (!engineState_inputsAvailable) {
    taskCondition.wait(lock);
  }

  coreInterface.complete(std::move(value));
}

EvoTask::~EvoTask() {
  if (taskThread) {
    taskThread->join();
  }
}

// MARK: - EvoTask - core::Task Protocol

void EvoTask::start(core::TaskInterface ti) {
  coreInterface = ti;
  taskThread = std::make_unique<std::thread>(&EvoTask::run, this);

  // Wait for an input requests from the task, or the completion of it before
  // returning. This is necessary to keep the task in the core engine's
  // InProgressWaiting state until we are sure we are done requesting inputs,
  // since the engine currently considers it an error to request them after we
  // receive the inputsAvailable() call.
  std::unique_lock<std::mutex> lock(taskMutex);
  while (!engineState_pendingInputs && !taskState_resultAvailable) {
    taskCondition.wait(lock);
  }
}

void EvoTask::provideValue(core::TaskInterface, uintptr_t inputID,
                  const core::ValueType& value) {
  std::unique_lock<std::mutex> lock(taskMutex);
  inputs[inputID].first = true;
  inputs[inputID].second = value; // FIXME: avoid copying value ?
  --engineState_pendingInputs;

  // Wake up the run thread to let it process the input.
  lock.unlock();
  taskCondition.notify_all();
  lock.lock();

  // Wait for additional input requests from the task, or the completion of it
  // before returning. This is necessary to keep the task in the core engine's
  // InProgressWaiting state until we are sure we are done requesting inputs,
  // since the engine currently considers it an error to request them after we
  // receive the inputsAvailable() call.
  while (!engineState_pendingInputs && !taskState_resultAvailable) {
    taskCondition.wait(lock);
  }
}

void EvoTask::inputsAvailable(core::TaskInterface) {
  {
    std::lock_guard<std::mutex> lock(taskMutex);
    assert(engineState_pendingInputs == 0);
    engineState_inputsAvailable = true;
  }
  taskCondition.notify_all();
}


// MARK: - EvoTask - EvoEngine Protocol

EvoInputHandle EvoTask::request(const core::KeyType& key) {
  std::unique_lock<std::mutex> lock(taskMutex);
  size_t index = inputs.size();
  inputs.push_back(std::make_pair<bool, core::ValueType>(false, {}));
  coreInterface.request(key, index);
  ++engineState_pendingInputs;
  lock.unlock();

  // We've altered our engineState, wake up that side if it is waiting.
  taskCondition.notify_all();

  return reinterpret_cast<EvoInputHandle>(index);
}


EvoProcessHandle EvoTask::spawn(
  ArrayRef<StringRef> commandLine,
  ArrayRef<std::pair<StringRef, StringRef>> environment,
  basic::ProcessAttributes attributes
) {
  std::lock_guard<std::mutex> lock(taskMutex);
  size_t index = procs.size();
  procs.emplace_back(false, ProcDescriptor(rule->key, commandLine, environment, attributes));

  coreInterface.spawn(basic::QueueJob(
    &tasks[index].second,
    [this, index](basic::QueueJobContext* context) {
      auto& procInfo = procs[index].second;

      auto completionFn = [this, index](basic::ProcessResult result) {
        std::unique_lock<std::mutex> lock(taskMutex);
        auto& procState = procs[index];
        procState.first = true;
        procState.second.result.proc = result;
        lock.unlock();
        taskCondition.notify_all();
      };
      coreInterface.spawn(
        context,
        procInfo.commandLine,
        procInfo.environment,
        procInfo.attributes,
        {completionFn}
      );
    }
  ));
  return reinterpret_cast<EvoProcessHandle>(index);
}

EvoTaskHandle EvoTask::spawn(StringRef description, EvoTaskFn work) {
  std::lock_guard<std::mutex> lock(taskMutex);
  size_t index = tasks.size();
  tasks.emplace_back(false, TaskDescriptor(rule->key, description));

  coreInterface.spawn(basic::QueueJob(
    &tasks[index].second,
    [this, index, work](basic::QueueJobContext*){
      core::ValueType&& value = work();
      std::unique_lock<std::mutex> lock(taskMutex);
      tasks[index].first = true;
      tasks[index].second.value = value;
      lock.unlock();
      taskCondition.notify_all();
    }
  ));
  return reinterpret_cast<EvoTaskHandle>(index);
}

const core::ValueType& EvoTask::wait(EvoInputHandle handle) {
  std::unique_lock<std::mutex> lock(taskMutex);
  size_t index = reinterpret_cast<size_t>(handle);
  const auto& input = inputs.at(index);
  while (!input.first) {
    taskCondition.wait(lock);
  }
  return input.second;
}

const EvoProcessResult& EvoTask::wait(EvoProcessHandle handle) {
  std::unique_lock<std::mutex> lock(taskMutex);
  size_t index = reinterpret_cast<size_t>(handle);
  const auto& proc = procs.at(index);
  while (!proc.first) {
    taskCondition.wait(lock);
  }
  return proc.second.result;
}

const core::ValueType& EvoTask::wait(EvoTaskHandle handle) {
  std::unique_lock<std::mutex> lock(taskMutex);
  size_t index = reinterpret_cast<size_t>(handle);
  const auto& task = tasks.at(index);
  while (!task.first) {
    taskCondition.wait(lock);
  }
  return task.second.value;
}
