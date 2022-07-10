//===--- TaskQueue.h - Task Execution Work Queue ----------------*- C++ -*-===//
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

#ifndef SWIFT_BASIC_TASKQUEUE_H
#define SWIFT_BASIC_TASKQUEUE_H

#include "swift/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Program.h"

#include <functional>
#include <memory>
#include <queue>

namespace swift {
namespace sys {

class Task; // forward declared to allow for platform-specific implementations

typedef llvm::sys::ProcessInfo::ProcessId ProcessId;

/// \brief Indicates how a TaskQueue should respond to the task finished event.
enum class TaskFinishedResponse {
  /// Indicates that execution should continue.
  ContinueExecution,
  /// Indicates that execution should stop (no new tasks will begin execution,
  /// but tasks which are currently executing will be allowed to finish).
  StopExecution,
};

/// \brief A class encapsulating the execution of multiple tasks in parallel.
class TaskQueue {
  /// Tasks which have not begun execution.
  std::queue<std::unique_ptr<Task>> QueuedTasks;

  /// The number of tasks to execute in parallel.
  unsigned NumberOfParallelTasks;

public:
  /// \brief Create a new TaskQueue instance.
  ///
  /// \param NumberOfParallelTasks indicates the number of tasks which should
  /// be run in parallel. If 0, the TaskQueue will choose the most appropriate
  /// number of parallel tasks for the current system.
  TaskQueue(unsigned NumberOfParallelTasks = 0);
  virtual ~TaskQueue();

  // TODO: remove once -Wdocumentation stops warning for \param, \returns on
  // std::function (<rdar://problem/15665132>).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
  /// \brief A callback which will be executed when each task begins execution
  ///
  /// \param Pid the ProcessId of the task which just began execution.
  /// \param Context the context which was passed when the task was added
  typedef std::function<void(ProcessId Pid, void *Context)> TaskBeganCallback;

  /// \brief A callback which will be executed after each task finishes
  /// execution.
  ///
  /// \param Pid the ProcessId of the task which finished execution.
  /// \param ReturnCode the return code of the task which finished execution.
  /// \param Output the output from the task which finished execution,
  /// if available. (This may not be available on all platforms.)
  /// \param Context the context which was passed when the task was added
  ///
  /// \returns true if further execution of tasks should stop,
  /// false if execution should continue
  typedef std::function<TaskFinishedResponse(ProcessId Pid, int ReturnCode,
                                             StringRef Output, void *Context)>
    TaskFinishedCallback;

  /// \brief A callback which will be executed if a task exited abnormally due
  /// to a signal.
  ///
  /// \param Pid the ProcessId of the task which exited abnormally.
  /// \param ErrorMsg a string describing why the task exited abnormally. If
  /// no reason could be deduced, this may be empty.
  /// \param Output the output from the task which exited abnormally, if
  /// available. (This may not be available on all platforms.)
  /// \param Context the context which was passed when the task was added
  ///
  /// \returns a TaskFinishedResponse indicating whether or not execution
  /// should proceed
  typedef std::function<TaskFinishedResponse(ProcessId Pid, StringRef ErrorMsg,
                                             StringRef Output, void *Context)>
    TaskSignalledCallback;
#pragma clang diagnostic pop

  /// \brief Indicates whether TaskQueue supports buffering output on the
  /// current system.
  ///
  /// \note If this returns false, the TaskFinishedCallback passed
  /// to \ref execute will always receive an empty StringRef for output, even
  /// if the task actually generated output.
  static bool supportsBufferingOutput();

  /// \brief Indicates whether TaskQueue supports parallel execution on the
  /// current system.
  static bool supportsParallelExecution();

  /// \returns the maximum number of tasks which this TaskQueue will execute in
  /// parallel
  unsigned getNumberOfParallelTasks() const;

  /// \brief Adds a task to the TaskQueue.
  ///
  /// \param ExecPath the path to the executable which the task should execute
  /// \param Args the arguments which should be passed to the task
  /// \param Env the environment which should be used for the task;
  /// must be null-terminated. If empty, inherits the parent's environment.
  /// \param Context an optional context which will be associated with the task
  virtual void addTask(const char *ExecPath, ArrayRef<const char *> Args,
                       ArrayRef<const char *> Env = llvm::None,
                       void *Context = nullptr);

  /// \brief Synchronously executes the tasks in the TaskQueue.
  ///
  /// \param Began a callback which will be called when a task begins
  /// \param Finished a callback which will be called when a task finishes
  /// \param Signalled a callback which will be called if a task exited
  /// abnormally due to a signal
  ///
  /// \returns true if all tasks did not execute successfully
  virtual bool
  execute(TaskBeganCallback Began = TaskBeganCallback(),
          TaskFinishedCallback Finished = TaskFinishedCallback(),
          TaskSignalledCallback Signalled = TaskSignalledCallback());

  /// Returns true if there are any tasks that have been queued but have not
  /// yet been executed.
  bool hasRemainingTasks() {
    return !QueuedTasks.empty();
  }
};

/// \brief A class which simulates execution of tasks with behavior similar to
/// TaskQueue.
class DummyTaskQueue : public TaskQueue {
  class DummyTask {
  public:
    const char *ExecPath;
    ArrayRef<const char *> Args;
    ArrayRef<const char *> Env;
    void *Context;

    DummyTask(const char *ExecPath, ArrayRef<const char *> Args,
              ArrayRef<const char *> Env = llvm::None, void *Context = nullptr)
      : ExecPath(ExecPath), Args(Args), Env(Env), Context(Context) {}
  };

  std::queue<std::unique_ptr<DummyTask>> QueuedTasks;

public:
  /// \brief Create a new DummyTaskQueue instance.
  DummyTaskQueue(unsigned NumberOfParallelTasks = 0);
  virtual ~DummyTaskQueue();

  virtual void addTask(const char *ExecPath, ArrayRef<const char *> Args,
                       ArrayRef<const char *> Env = llvm::None,
                       void *Context = nullptr);

  virtual bool
  execute(TaskBeganCallback Began = TaskBeganCallback(),
          TaskFinishedCallback Finished = TaskFinishedCallback(),
          TaskSignalledCallback Signalled = TaskSignalledCallback());
};

} // end namespace sys
} // end namespace swift

#endif // SWIFT_BASIC_TASKQUEUE_H
