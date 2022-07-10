//===--- TaskQueue.cpp - Task Execution Work Queue ------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file includes the appropriate platform-specific TaskQueue
/// implementation (or the default serial fallback if one is not available),
/// as well as any platform-agnostic TaskQueue functionality.
///
//===----------------------------------------------------------------------===//

#include "swift/Basic/TaskQueue.h"

using namespace swift;
using namespace swift::sys;

// Include the correct TaskQueue implementation.
#if LLVM_ON_UNIX && !defined(__CYGWIN__)
#include "Unix/TaskQueue.inc"
#else
#include "Default/TaskQueue.inc"
#endif

TaskQueue::TaskQueue(unsigned NumberOfParallelTasks)
  : NumberOfParallelTasks(NumberOfParallelTasks) {}

TaskQueue::~TaskQueue() = default;

// DummyTaskQueue implementation

DummyTaskQueue::DummyTaskQueue(unsigned NumberOfParallelTasks)
  : TaskQueue(NumberOfParallelTasks) {}

DummyTaskQueue::~DummyTaskQueue() = default;

void DummyTaskQueue::addTask(const char *ExecPath, ArrayRef<const char *> Args,
                             ArrayRef<const char *> Env, void *Context) {
  QueuedTasks.emplace(
    std::unique_ptr<DummyTask>(new DummyTask(ExecPath, Args, Env, Context)));
}

bool DummyTaskQueue::execute(TaskQueue::TaskBeganCallback Began,
                             TaskQueue::TaskFinishedCallback Finished,
                             TaskQueue::TaskSignalledCallback Signalled) {
  typedef std::pair<ProcessId, std::unique_ptr<DummyTask>> PidTaskPair;
  std::queue<PidTaskPair> ExecutingTasks;

  bool SubtaskFailed = false;

  static ProcessId Pid = 0;

  unsigned MaxNumberOfParallelTasks = getNumberOfParallelTasks();

  while ((!QueuedTasks.empty() && !SubtaskFailed) ||
         !ExecutingTasks.empty()) {
    // Enqueue additional tasks if we have additional tasks, we aren't already
    // at the parallel limit, and no earlier subtasks have failed.
    while (!SubtaskFailed && !QueuedTasks.empty() &&
           ExecutingTasks.size() < MaxNumberOfParallelTasks) {
      std::unique_ptr<DummyTask> T(QueuedTasks.front().release());
      QueuedTasks.pop();

      if (Began)
        Began(++Pid, T->Context);

      ExecutingTasks.push(PidTaskPair(Pid, std::move(T)));
    }

    // Finish the first scheduled task.
    PidTaskPair P = std::move(ExecutingTasks.front());
    ExecutingTasks.pop();

    if (Finished) {
      std::string Output = "Output placeholder\n";
        if (Finished(P.first, 0, Output, P.second->Context) ==
            TaskFinishedResponse::StopExecution)
          SubtaskFailed = true;
    }
  }

  return false;
}
