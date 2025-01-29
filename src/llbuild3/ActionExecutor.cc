//===-- ActionExecutor.cpp ------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild3/ActionExecutor.h"

#include <llbuild3/Errors.hpp>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <thread>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>


#include "llbuild3/Error.pb.h"

using namespace llbuild3;

ActionExecutorListener::~ActionExecutorListener() { }

#pragma mark - ActionExecutor implementation

namespace {

template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

inline Error makeExecutorError(ExecutorError code,
                             std::string desc = std::string()) {
  Error err;
  err.set_type(ErrorType::EXECUTOR);
  err.set_code(rawCode(code));
  if (!desc.empty()) {
    err.set_description(desc);
  }
  return err;
}


class ActionExecutorImpl {
private:
  typedef std::variant<ActionRequest, SubtaskRequest> AnyRequest;

  struct SchedulerItem {
    uint64_t actionID;
    uint64_t buildID;
    uint64_t workID;
    ActionPriority priority;

    SchedulerItem()
      : actionID(0), buildID(0), workID(0), priority(ActionPriority::Default)
    {
    }

    SchedulerItem(uint64_t actionID, const ActionRequest& r)
      : actionID(actionID), buildID(r.owner.buildID), workID(r.owner.workID),
        priority(r.priority)
    {
    }

    SchedulerItem(uint64_t actionID, const SubtaskRequest& r)
      : actionID(actionID), buildID(r.owner.buildID), workID(r.owner.workID),
        priority(r.priority)
    {
    }
  };

  struct SchedulerComparator {
    bool operator()(const SchedulerItem& lhs, const SchedulerItem& rhs) const {
      // priority order first
      if (lhs.priority > rhs.priority) {
        return true;
      }
      if (lhs.priority < rhs.priority) {
        return false;
      }

      // then just order by lower build id and then work id
      // - this does not currently take into account which engine is requesting
      //   the work, thus will unfairly prefer newer engines, for example
      return lhs.buildID < rhs.buildID &&
      lhs.workID < rhs.workID;
    }
  };

  typedef std::priority_queue<SchedulerItem, std::vector<SchedulerItem>, SchedulerComparator> ActionScheduler;

private:
  std::shared_ptr<LocalExecutor> localExecutor;
  std::shared_ptr<RemoteExecutor> remoteExecutor;

  // Storage and mapping for all pending items
  std::mutex pendingMutex;
  uint64_t lastID{0};
  std::unordered_map<uint64_t, AnyRequest> pending;


  // Local concurrency control
  unsigned numLanes;
  std::vector<std::unique_ptr<std::thread>> lanes;
  unsigned maxAsyncConcurrency;
  std::unique_ptr<std::thread> asyncLane;


  // Async (local) actions queue
  std::mutex asyncActionsMutex;
  ActionScheduler asyncActions;
  std::condition_variable asyncActionsCondition;

  // Local (synchronous) actions queue
  std::mutex localActionsMutex;
  ActionScheduler localActions;
  std::condition_variable localActionsCondition;

  // State control
  std::atomic<bool> shouldShutdown{false};

  // Listeners
  std::mutex listenerMutex;
  std::unordered_set<ActionExecutorListener*> listeners;

private:
  void asyncLaneHandler();
  void localLaneHandler(uint32_t laneNumber);

public:
  ActionExecutorImpl(std::shared_ptr<LocalExecutor> localExecutor,
                     std::shared_ptr<RemoteExecutor> remoteExecutor,
                     unsigned maxLocalConcurrency,
                     unsigned maxAsyncConcurrency)
  : localExecutor(localExecutor), remoteExecutor(remoteExecutor)
  , maxAsyncConcurrency(maxAsyncConcurrency)
  {
    // Determine local task concurrency
    numLanes = std::thread::hardware_concurrency();
    if (numLanes == 0) {
      numLanes = 1;
    }

    if (maxLocalConcurrency > 0 && numLanes > maxLocalConcurrency) {
      numLanes = maxLocalConcurrency;
    }

    // Start up the local lanes
    for (unsigned i = 0; i != numLanes; ++i) {
      lanes.push_back(std::unique_ptr<std::thread>(
        new std::thread(&ActionExecutorImpl::localLaneHandler, this, i)
      ));
    }

    // Start up the async handling thread
    asyncLane = std::unique_ptr<std::thread>(
      new std::thread(&ActionExecutorImpl::asyncLaneHandler, this)
    );
  }

  ~ActionExecutorImpl() {
    // Shut down the lanes.
    shouldShutdown = true;

    {
      std::lock_guard<std::mutex> lock(localActionsMutex);
      localActionsCondition.notify_all();
    }

    for (unsigned i = 0; i != numLanes; ++i) {
      lanes[i]->join();
    }

    {
      std::lock_guard<std::mutex> lock(asyncActionsMutex);
      asyncActionsCondition.notify_all();
    }
    asyncLane->join();
  }

  void registerProvider(std::unique_ptr<ActionProvider>&& provider) {
    // FIXME: implement
  }

  void attachListener(ActionExecutorListener* listener) {
    std::lock_guard<std::mutex> lock(listenerMutex);
    listeners.insert(listener);
  }

  void detachListener(ActionExecutorListener* listener) {
    std::lock_guard<std::mutex> lock(listenerMutex);
    listeners.erase(listener);
  }

  result<Label, Error> resolveFunction(const Label& name) {
    // FIXME: implement
    return fail(makeExecutorError(ExecutorError::Unimplemented));
  }

  result<ActionID, Error> submit(ActionRequest request) {
    // FIXME: implement
    return fail(makeExecutorError(ExecutorError::Unimplemented));
  }

  result<uint64_t, Error> submit(SubtaskRequest request) {
    if (!request.si.has_value()) {
      // bad request, no interface
      return fail(makeExecutorError(ExecutorError::BadRequest, "no subtask interface"));
    }

    uint64_t actionID = 0;
    {
      std::lock_guard<std::mutex> lock(pendingMutex);
      actionID = ++lastID;
      pending.insert({actionID, {request}});
    }

    SchedulerItem item(actionID, request);
    std::visit(overloaded{
      [this, item](Subtask){
        std::lock_guard<std::mutex> lock(localActionsMutex);
        localActions.push(item);
        localActionsCondition.notify_one();
      },
      [this, item](AsyncSubtask){
        std::lock_guard<std::mutex> lock(asyncActionsMutex);
        asyncActions.push(item);
        asyncActionsCondition.notify_one();
      }
    }, request.subtask);

    return actionID;
  }

  std::optional<Error> cancel(ActionID aid, ActionOwner owner) {
    // FIXME: implement
    return makeExecutorError(ExecutorError::Unimplemented);
  }

private:
  void processLocalAction(const SchedulerItem& item, ActionRequest req) {
    // FIXME: implement
  }

  void processLocalSubtask(const SchedulerItem& item, SubtaskRequest req) {
    if (!req.si.has_value()) {
      // bad request, no interface

      SubtaskResult res{fail(
        makeExecutorError(ExecutorError::BadRequest, "no subtask interface")
      )};
      notifySubtaskComplete(item.actionID, res);

      std::lock_guard<std::mutex> lock(pendingMutex);
      pending.erase(item.actionID);
      return;
    }

    SyncSubtask* st = std::get_if<SyncSubtask>(&req.subtask);
    if (st == nullptr) {
      // Wha? Wrong task type?

      SubtaskResult res{fail(
        makeExecutorError(ExecutorError::InternalInconsistency, "not a sync subtask")
      )};
      notifySubtaskComplete(item.actionID, res);

      std::lock_guard<std::mutex> lock(pendingMutex);
      pending.erase(item.actionID);
      return;
    }

    notifySubtaskStart(item.actionID);
    auto value = (*st)(*req.si);

    notifySubtaskComplete(item.actionID, value);

    std::lock_guard<std::mutex> lock(pendingMutex);
    pending.erase(item.actionID);
  }

  void notifySubtaskStart(uint64_t actionID) {
    std::lock_guard<std::mutex> lock(listenerMutex);
    for (auto l : listeners) {
      l->notifySubtaskStart(actionID);
    }
  }

  void notifySubtaskComplete(uint64_t actionID, SubtaskResult result) {
    std::lock_guard<std::mutex> lock(listenerMutex);
    for (auto l : listeners) {
      l->notifySubtaskComplete(actionID, result);
    }
  }
};


void ActionExecutorImpl::localLaneHandler(uint32_t laneNumber) {
  // Set the thread name, if available.
  std::string threadName = "llbuild3-exec-" + std::to_string(laneNumber);
#if defined(__APPLE__)
  pthread_setname_np(threadName.c_str());
#elif defined(__linux__)
  pthread_setname_np(threadName.c_str());
#endif

  while (true) {
    SchedulerItem item{};
    {
      std::unique_lock<std::mutex> lock(localActionsMutex);

      // While the queue is empty, wait for an item.
      while (!shouldShutdown && localActions.empty()) {
        localActionsCondition.wait(lock);
      }
      if (shouldShutdown && localActions.empty()) {
        return;
      }

      item = localActions.top();
      localActions.pop();
    }

    // If we got an empty itemID, the queue is shutting down.
    if (item.actionID == 0)
      break;

    AnyRequest req{};
    {
      std::lock_guard<std::mutex> lock(pendingMutex);
      auto it = pending.find(item.actionID);
      assert(it != pending.end());
      if (it == pending.end()) {
        // Wha? Bad item in the queue...
        continue;
      }
      req = it->second;
    }

    std::visit(overloaded{
      [this, item](ActionRequest req) { processLocalAction(item, req); },
      [this, item](SubtaskRequest req) { processLocalSubtask(item, req); }
    }, req);
  }
}


void ActionExecutorImpl::asyncLaneHandler() {
  // Set the thread name, if available.
  std::string threadName = "llbuild3-exec-async";
#if defined(__APPLE__)
  pthread_setname_np(threadName.c_str());
#elif defined(__linux__)
  pthread_setname_np(threadName.c_str());
#endif

  while (true) {
    SchedulerItem item{};
    {
      std::unique_lock<std::mutex> lock(asyncActionsMutex);

      // While the queue is empty, wait for an item.
      while (!shouldShutdown && asyncActions.empty()) {
        asyncActionsCondition.wait(lock);
      }
      if (shouldShutdown && asyncActions.empty()) {
        return;
      }

      item = asyncActions.top();
      asyncActions.pop();
    }

    // If we got an empty taskID, the queue is shutting down.
    if (item.actionID == 0) {
      break;
    }

    AnyRequest req{};
    {
      std::lock_guard<std::mutex> lock(pendingMutex);
      auto it = pending.find(item.actionID);
      assert(it != pending.end());
      if (it == pending.end()) {
        // Wha? Bad item in the queue...
        continue;
      }
      req = it->second;
    }

    SubtaskRequest* sreq = std::get_if<SubtaskRequest>(&req);
    if (sreq == nullptr) {
      // Wha? Wrong task type?

      SubtaskResult res{fail(
        makeExecutorError(ExecutorError::InternalInconsistency, "not a subtask")
      )};
      notifySubtaskComplete(item.actionID, res);

      std::lock_guard<std::mutex> lock(pendingMutex);
      pending.erase(item.actionID);
      continue;
    }

    if (!sreq->si.has_value()) {
      // bad request, no interface

      SubtaskResult res{fail(
        makeExecutorError(ExecutorError::BadRequest, "no subtask interface")
      )};
      notifySubtaskComplete(item.actionID, res);

      std::lock_guard<std::mutex> lock(pendingMutex);
      pending.erase(item.actionID);
      continue;
    }

    AsyncSubtask* ast = std::get_if<AsyncSubtask>(&sreq->subtask);
    if (ast == nullptr) {
      // Wha? Wrong task type?

      SubtaskResult res{fail(
        makeExecutorError(ExecutorError::InternalInconsistency, "not an async subtask")
      )};
      notifySubtaskComplete(item.actionID, res);

      std::lock_guard<std::mutex> lock(pendingMutex);
      pending.erase(item.actionID);
      continue;
    }

    notifySubtaskStart(item.actionID);
    (*ast)(*sreq->si, std::function([this, actionID = item.actionID](SubtaskResult value) {
      notifySubtaskComplete(actionID, value);

      std::lock_guard<std::mutex> lock(pendingMutex);
      pending.erase(actionID);
    }));
  }
}

} // anonymous namespace

#pragma mark - ActionExecutor

ActionExecutor::ActionExecutor(std::shared_ptr<LocalExecutor> localExecutor,
                               std::shared_ptr<RemoteExecutor> remoteExecutor,
                               unsigned maxLocalConcurrency,
                               unsigned maxAsyncConcurrency)
  : impl(new ActionExecutorImpl(localExecutor, remoteExecutor, maxLocalConcurrency, maxAsyncConcurrency)) {
}

ActionExecutor::~ActionExecutor() {
  delete static_cast<ActionExecutorImpl*>(impl);
}

void ActionExecutor::registerProvider(std::unique_ptr<ActionProvider>&& provider) {
  static_cast<ActionExecutorImpl*>(impl)->registerProvider(std::move(provider));
}

void ActionExecutor::attachListener(ActionExecutorListener* listener) {
  static_cast<ActionExecutorImpl*>(impl)->attachListener(listener);
}
void ActionExecutor::detachListener(ActionExecutorListener* listener) {
  static_cast<ActionExecutorImpl*>(impl)->detachListener(listener);
}

result<Label, Error> ActionExecutor::resolveFunction(const Label& name) {
  return static_cast<ActionExecutorImpl*>(impl)->resolveFunction(name);
}

result<ActionID, Error> ActionExecutor::submit(ActionRequest request) {
  return static_cast<ActionExecutorImpl*>(impl)->submit(request);
}

result<uint64_t, Error> ActionExecutor::submit(SubtaskRequest request) {
  return static_cast<ActionExecutorImpl*>(impl)->submit(request);
}

std::optional<Error> ActionExecutor::cancel(ActionID aid, ActionOwner owner) {
  return static_cast<ActionExecutorImpl*>(impl)->cancel(aid, owner);
}
