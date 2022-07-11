//===--- Task.cpp - Task object and management ----------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Object management routines for asynchronous task objects.
//
//===----------------------------------------------------------------------===//

#include "../CompatibilityOverride/CompatibilityOverride.h"
#include "swift/Runtime/Concurrency.h"
#include "swift/ABI/Task.h"
#include "swift/ABI/TaskLocal.h"
#include "swift/ABI/TaskOptions.h"
#include "swift/ABI/Metadata.h"
#include "swift/Runtime/Mutex.h"
#include "swift/Runtime/HeapObject.h"
#include "TaskGroupPrivate.h"
#include "TaskPrivate.h"
#include "Debug.h"
#include "Error.h"

#if SWIFT_CONCURRENCY_ENABLE_DISPATCH
#include <dispatch/dispatch.h>
#endif

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#if defined(SWIFT_CONCURRENCY_BACK_DEPLOYMENT)
#include <Availability.h>
#include <TargetConditionals.h>
#if TARGET_OS_WATCH
// Bitcode compilation for the watch device precludes defining the following asm
// symbols, so we don't use them... but simulators are okay.
#if TARGET_OS_SIMULATOR
asm("\n .globl _swift_async_extendedFramePointerFlags" \
    "\n _swift_async_extendedFramePointerFlags = 0x0");
#endif
#else
asm("\n .globl _swift_async_extendedFramePointerFlags" \
    "\n _swift_async_extendedFramePointerFlags = 0x0");
#endif
#else
#ifdef __APPLE__
#if __POINTER_WIDTH__ == 64
asm("\n .globl _swift_async_extendedFramePointerFlags" \
    "\n _swift_async_extendedFramePointerFlags = 0x1000000000000000");
#elif __ARM64_ARCH_8_32__
asm("\n .globl _swift_async_extendedFramePointerFlags" \
    "\n _swift_async_extendedFramePointerFlags = 0x10000000");
#else
asm("\n .globl _swift_async_extendedFramePointerFlags" \
    "\n _swift_async_extendedFramePointerFlags = 0x0");
#endif
#endif // __APPLE__
#endif // !defined(SWIFT_CONCURRENCY_BACK_DEPLOYMENT)

using namespace swift;
using FutureFragment = AsyncTask::FutureFragment;
using TaskGroup = swift::TaskGroup;

Metadata swift::TaskAllocatorSlabMetadata;
const void *const swift::_swift_concurrency_debug_asyncTaskSlabMetadata =
    &TaskAllocatorSlabMetadata;

void FutureFragment::destroy() {
  auto queueHead = waitQueue.load(std::memory_order_acquire);
  switch (queueHead.getStatus()) {
  case Status::Executing:
    assert(false && "destroying a task that never completed");

  case Status::Success:
    resultType->vw_destroy(getStoragePtr());
    break;

  case Status::Error:
    swift_errorRelease(getError());
    break;
  }
}

FutureFragment::Status AsyncTask::waitFuture(AsyncTask *waitingTask,
                                             AsyncContext *waitingTaskContext,
                                             TaskContinuationFunction *resumeFn,
                                             AsyncContext *callerContext,
                                             OpaqueValue *result) {
  using Status = FutureFragment::Status;
  using WaitQueueItem = FutureFragment::WaitQueueItem;

  assert(isFuture());
  auto fragment = futureFragment();

  auto queueHead = fragment->waitQueue.load(std::memory_order_acquire);
  bool contextIntialized = false;
  while (true) {
    switch (queueHead.getStatus()) {
    case Status::Error:
    case Status::Success:
      SWIFT_TASK_DEBUG_LOG("task %p waiting on task %p, completed immediately",
                           waitingTask, this);
      _swift_tsan_acquire(static_cast<Job *>(this));
      if (contextIntialized) waitingTask->flagAsRunning();
      // The task is done; we don't need to wait.
      return queueHead.getStatus();

    case Status::Executing:
      SWIFT_TASK_DEBUG_LOG("task %p waiting on task %p, going to sleep",
                           waitingTask, this);
      _swift_tsan_release(static_cast<Job *>(waitingTask));
      // Task is not complete. We'll need to add ourselves to the queue.
      break;
    }

    if (!contextIntialized) {
      contextIntialized = true;
      auto context =
          reinterpret_cast<TaskFutureWaitAsyncContext *>(waitingTaskContext);
      context->errorResult = nullptr;
      context->successResultPointer = result;
      context->ResumeParent = resumeFn;
      context->Parent = callerContext;
      waitingTask->flagAsSuspended();
    }

    // Put the waiting task at the beginning of the wait queue.
    waitingTask->getNextWaitingTask() = queueHead.getTask();
    auto newQueueHead = WaitQueueItem::get(Status::Executing, waitingTask);
    if (fragment->waitQueue.compare_exchange_weak(
            queueHead, newQueueHead,
            /*success*/ std::memory_order_release,
            /*failure*/ std::memory_order_acquire)) {
      // Escalate the priority of this task based on the priority
      // of the waiting task.
      swift_task_escalate(this, waitingTask->Flags.getPriority());
      _swift_task_clearCurrent();
      return FutureFragment::Status::Executing;
    }
  }
}

void NullaryContinuationJob::process(Job *_job) {
  auto *job = cast<NullaryContinuationJob>(_job);

  auto *task = job->Task;
  auto *continuation = job->Continuation;

  _swift_task_dealloc_specific(task, job);

  auto *context = cast<ContinuationAsyncContext>(continuation->ResumeContext);

  context->setErrorResult(nullptr);
  swift_continuation_resume(continuation);
}

void AsyncTask::completeFuture(AsyncContext *context) {
  using Status = FutureFragment::Status;
  using WaitQueueItem = FutureFragment::WaitQueueItem;
  SWIFT_TASK_DEBUG_LOG("complete future = %p", this);
  assert(isFuture());
  auto fragment = futureFragment();

  // If an error was thrown, save it in the future fragment.
  auto asyncContextPrefix = reinterpret_cast<FutureAsyncContextPrefix *>(
      reinterpret_cast<char *>(context) - sizeof(FutureAsyncContextPrefix));
  bool hadErrorResult = false;
  auto errorObject = asyncContextPrefix->errorResult;
  fragment->getError() = errorObject;
  if (errorObject) {
    hadErrorResult = true;
  }

  _swift_tsan_release(static_cast<Job *>(this));

  // Update the status to signal completion.
  auto newQueueHead = WaitQueueItem::get(
    hadErrorResult ? Status::Error : Status::Success,
    nullptr
  );
  auto queueHead = fragment->waitQueue.exchange(
      newQueueHead, std::memory_order_acquire);
  assert(queueHead.getStatus() == Status::Executing);

  // If this is task group child, notify the parent group about the completion.
  if (hasGroupChildFragment()) {
    // then we must offer into the parent group that we completed,
    // so it may `next()` poll completed child tasks in completion order.
    auto group = groupChildFragment()->getGroup();
    group->offer(this, context);
  }

  // Schedule every waiting task on the executor.
  auto waitingTask = queueHead.getTask();

  if (!waitingTask)
    SWIFT_TASK_DEBUG_LOG("task %p had no waiting tasks", this);

  while (waitingTask) {
    // Find the next waiting task before we invalidate it by resuming
    // the task.
    auto nextWaitingTask = waitingTask->getNextWaitingTask();

    SWIFT_TASK_DEBUG_LOG("waking task %p from future of task %p", waitingTask,
                         this);

    // Fill in the return context.
    auto waitingContext =
      static_cast<TaskFutureWaitAsyncContext *>(waitingTask->ResumeContext);
    if (hadErrorResult) {
      waitingContext->fillWithError(fragment);
    } else {
      waitingContext->fillWithSuccess(fragment);
    }

    _swift_tsan_acquire(static_cast<Job *>(waitingTask));

    // Enqueue the waiter on the global executor.
    // TODO: allow waiters to fill in a suggested executor
    swift_task_enqueueGlobal(waitingTask);

    // Move to the next task.
    waitingTask = nextWaitingTask;
  }
}

SWIFT_CC(swift)
static void destroyJob(SWIFT_CONTEXT HeapObject *obj) {
  assert(false && "A non-task job should never be destroyed as heap metadata.");
}

AsyncTask::~AsyncTask() {
  flagAsCompleted();

  // For a future, destroy the result.
  if (isFuture()) {
    futureFragment()->destroy();
  }

  Private.destroy();
}

void AsyncTask::setTaskId() {
  static std::atomic<uint64_t> NextId(1);

  // We want the 32-bit Job::Id to be non-zero, so loop if we happen upon zero.
  uint64_t Fetched;
  do {
    Fetched = NextId.fetch_add(1, std::memory_order_relaxed);
    Id = Fetched & 0xffffffff;
  } while (Id == 0);

  _private().Id = (Fetched >> 32) & 0xffffffff;
}

SWIFT_CC(swift)
static void destroyTask(SWIFT_CONTEXT HeapObject *obj) {
  auto task = static_cast<AsyncTask*>(obj);
  task->~AsyncTask();

  // The task execution itself should always hold a reference to it, so
  // if we get here, we know the task has finished running, which means
  // swift_task_complete should have been run, which will have torn down
  // the task-local allocator.  There's actually nothing else to clean up
  // here.

  SWIFT_TASK_DEBUG_LOG("destroy task %p", task);
  free(task);
}

static ExecutorRef executorForEnqueuedJob(Job *job) {
#if !SWIFT_CONCURRENCY_ENABLE_DISPATCH
  return ExecutorRef::generic();
#else
  void *jobQueue = job->SchedulerPrivate[Job::DispatchQueueIndex];
  if (jobQueue == DISPATCH_QUEUE_GLOBAL_EXECUTOR)
    return ExecutorRef::generic();
  else
    return ExecutorRef::forOrdinary(reinterpret_cast<HeapObject*>(jobQueue),
                    _swift_task_getDispatchQueueSerialExecutorWitnessTable());
#endif
}

static void jobInvoke(void *obj, void *unused, uint32_t flags) {
  (void)unused;
  Job *job = reinterpret_cast<Job *>(obj);

  swift_job_run(job, executorForEnqueuedJob(job));
}

// Magic constant to identify Swift Job vtables to Dispatch.
static const unsigned long dispatchSwiftObjectType = 1;

FullMetadata<DispatchClassMetadata> swift::jobHeapMetadata = {
  {
    {
      &destroyJob
    },
    {
      /*value witness table*/ nullptr
    }
  },
  {
    MetadataKind::Job,
    dispatchSwiftObjectType,
    jobInvoke
  }
};

/// Heap metadata for an asynchronous task.
static FullMetadata<DispatchClassMetadata> taskHeapMetadata = {
  {
    {
      &destroyTask
    },
    {
      /*value witness table*/ nullptr
    }
  },
  {
    MetadataKind::Task,
    dispatchSwiftObjectType,
    jobInvoke
  }
};

const void *const swift::_swift_concurrency_debug_jobMetadata =
    static_cast<Metadata *>(&jobHeapMetadata);
const void *const swift::_swift_concurrency_debug_asyncTaskMetadata =
    static_cast<Metadata *>(&taskHeapMetadata);

static void completeTaskImpl(AsyncTask *task,
                             AsyncContext *context,
                             SwiftError *error) {
  assert(task && "completing task, but there is no active task registered");

  // Store the error result.
  auto asyncContextPrefix = reinterpret_cast<AsyncContextPrefix *>(
      reinterpret_cast<char *>(context) - sizeof(AsyncContextPrefix));
  asyncContextPrefix->errorResult = error;

  task->Private.complete(task);

  SWIFT_TASK_DEBUG_LOG("task %p completed", task);

  // Complete the future.
  // Warning: This deallocates the task in case it's an async let task.
  // The task must not be accessed afterwards.
  if (task->isFuture()) {
    task->completeFuture(context);
  }

  // TODO: set something in the status?
  // if (task->hasChildFragment()) {
    // TODO: notify the parent somehow?
    // TODO: remove this task from the child-task chain?
  // }
}

/// The function that we put in the context of a simple task
/// to handle the final return.
SWIFT_CC(swiftasync)
static void completeTask(SWIFT_ASYNC_CONTEXT AsyncContext *context,
                         SWIFT_CONTEXT SwiftError *error) {
  // Set that there's no longer a running task in the current thread.
  auto task = _swift_task_clearCurrent();
  assert(task && "completing task, but there is no active task registered");

  completeTaskImpl(task, context, error);
}

/// The function that we put in the context of a simple task
/// to handle the final return.
SWIFT_CC(swiftasync)
static void completeTaskAndRelease(SWIFT_ASYNC_CONTEXT AsyncContext *context,
                                   SWIFT_CONTEXT SwiftError *error) {
  // Set that there's no longer a running task in the current thread.
  auto task = _swift_task_clearCurrent();
  assert(task && "completing task, but there is no active task registered");

  completeTaskImpl(task, context, error);

  // Release the task, balancing the retain that a running task has on itself.
  // If it was a group child task, it will remain until the group returns it.
  swift_release(task);
}

/// The function that we put in the context of a simple task
/// to handle the final return from a closure.
SWIFT_CC(swiftasync)
static void completeTaskWithClosure(SWIFT_ASYNC_CONTEXT AsyncContext *context,
                                    SWIFT_CONTEXT SwiftError *error) {
  // Release the closure context.
  auto asyncContextPrefix = reinterpret_cast<AsyncContextPrefix *>(
      reinterpret_cast<char *>(context) - sizeof(AsyncContextPrefix));

  swift_release((HeapObject *)asyncContextPrefix->closureContext);
  
  // Clean up the rest of the task.
  return completeTaskAndRelease(context, error);
}

SWIFT_CC(swiftasync)
static void non_future_adapter(SWIFT_ASYNC_CONTEXT AsyncContext *_context) {
  auto asyncContextPrefix = reinterpret_cast<AsyncContextPrefix *>(
      reinterpret_cast<char *>(_context) - sizeof(AsyncContextPrefix));
  return asyncContextPrefix->asyncEntryPoint(
      _context, asyncContextPrefix->closureContext);
}

SWIFT_CC(swiftasync)
static void future_adapter(SWIFT_ASYNC_CONTEXT AsyncContext *_context) {
  auto asyncContextPrefix = reinterpret_cast<FutureAsyncContextPrefix *>(
      reinterpret_cast<char *>(_context) - sizeof(FutureAsyncContextPrefix));
  return asyncContextPrefix->asyncEntryPoint(
      asyncContextPrefix->indirectResult, _context,
      asyncContextPrefix->closureContext);
}

SWIFT_CC(swiftasync)
static void task_wait_throwing_resume_adapter(SWIFT_ASYNC_CONTEXT AsyncContext *_context) {

  auto context = static_cast<TaskFutureWaitAsyncContext *>(_context);
  auto resumeWithError =
      reinterpret_cast<AsyncVoidClosureEntryPoint *>(context->ResumeParent);
  return resumeWithError(context->Parent, context->errorResult);
}

SWIFT_CC(swiftasync)
static void
task_future_wait_resume_adapter(SWIFT_ASYNC_CONTEXT AsyncContext *_context) {
  return _context->ResumeParent(_context->Parent);
}

/// Implementation of task creation.
SWIFT_CC(swift)
static AsyncTaskAndContext swift_task_create_commonImpl(
    size_t rawTaskCreateFlags,
    TaskOptionRecord *options,
    const Metadata *futureResultType,
    FutureAsyncSignature::FunctionType *function, void *closureContext,
    size_t initialContextSize) {
  TaskCreateFlags taskCreateFlags(rawTaskCreateFlags);

  // Propagate task-creation flags to job flags as appropriate.
  JobFlags jobFlags(JobKind::Task, taskCreateFlags.getPriority());
  jobFlags.task_setIsChildTask(taskCreateFlags.isChildTask());
  if (futureResultType) {
    jobFlags.task_setIsFuture(true);
    assert(initialContextSize >= sizeof(FutureAsyncContext));
  }

  // Collect the options we know about.
  ExecutorRef executor = ExecutorRef::generic();
  TaskGroup *group = nullptr;
  AsyncLet *asyncLet = nullptr;
  bool hasAsyncLetResultBuffer = false;
  for (auto option = options; option; option = option->getParent()) {
    switch (option->getKind()) {
    case TaskOptionRecordKind::Executor:
      executor = cast<ExecutorTaskOptionRecord>(option)->getExecutor();
      break;

    case TaskOptionRecordKind::TaskGroup:
      group = cast<TaskGroupTaskOptionRecord>(option)->getGroup();
      assert(group && "Missing group");
      jobFlags.task_setIsGroupChildTask(true);
      break;

    case TaskOptionRecordKind::AsyncLet:
      asyncLet = cast<AsyncLetTaskOptionRecord>(option)->getAsyncLet();
      assert(asyncLet && "Missing async let storage");
      jobFlags.task_setIsAsyncLetTask(true);
      jobFlags.task_setIsChildTask(true);
      break;

    case TaskOptionRecordKind::AsyncLetWithBuffer:
      auto *aletRecord = cast<AsyncLetWithBufferTaskOptionRecord>(option);
      asyncLet = aletRecord->getAsyncLet();
      // TODO: Actually digest the result buffer into the async let task
      // context, so that we can emplace the eventual result there instead
      // of in a FutureFragment.
      hasAsyncLetResultBuffer = true;
      assert(asyncLet && "Missing async let storage");
        
      jobFlags.task_setIsAsyncLetTask(true);
      jobFlags.task_setIsChildTask(true);
      break;
    }
  }

  // Add to the task group, if requested.
  if (taskCreateFlags.addPendingGroupTaskUnconditionally()) {
    assert(group && "Missing group");
    swift_taskGroup_addPending(group, /*unconditionally=*/true);
  }

  AsyncTask *parent = nullptr;
  if (jobFlags.task_isChildTask()) {
    parent = swift_task_getCurrent();
    assert(parent != nullptr && "creating a child task with no active task");
  }

  // Inherit the priority of the currently-executing task if unspecified and
  // we want to inherit.
  if (jobFlags.getPriority() == JobPriority::Unspecified &&
      (jobFlags.task_isChildTask() || taskCreateFlags.inheritContext())) {
    AsyncTask *currentTask = parent;
    if (!currentTask)
      currentTask = swift_task_getCurrent();

    if (currentTask)
      jobFlags.setPriority(currentTask->getPriority());
    else if (taskCreateFlags.inheritContext())
      jobFlags.setPriority(swift_task_getCurrentThreadPriority());
  }

  // Adjust user-interactive priorities down to user-initiated.
  if (jobFlags.getPriority() == JobPriority::UserInteractive)
    jobFlags.setPriority(JobPriority::UserInitiated);

  // If there is still no job priority, use the default priority.
  if (jobFlags.getPriority() == JobPriority::Unspecified)
    jobFlags.setPriority(JobPriority::Default);

  // Figure out the size of the header.
  size_t headerSize = sizeof(AsyncTask);
  if (parent) {
    headerSize += sizeof(AsyncTask::ChildFragment);
  }
  if (group) {
    headerSize += sizeof(AsyncTask::GroupChildFragment);
  }
  if (futureResultType) {
    headerSize += FutureFragment::fragmentSize(headerSize, futureResultType);
    // Add the future async context prefix.
    headerSize += sizeof(FutureAsyncContextPrefix);
  } else {
    // Add the async context prefix.
    headerSize += sizeof(AsyncContextPrefix);
  }

  headerSize = llvm::alignTo(headerSize, llvm::Align(alignof(AsyncContext)));

  // Allocate the initial context together with the job.
  // This means that we never get rid of this allocation.
  size_t amountToAllocate = headerSize + initialContextSize;

  assert(amountToAllocate % MaximumAlignment == 0);

  unsigned initialSlabSize = 512;

  void *allocation = nullptr;
  if (asyncLet) {
    assert(parent);
    
    // If there isn't enough room in the fixed async let allocation to
    // set up the initial context, then we'll have to allocate more space
    // from the parent.
    if (asyncLet->getSizeOfPreallocatedSpace() < amountToAllocate) {
      hasAsyncLetResultBuffer = false;
    }
    
    // DEPRECATED. This is separated from the above condition because we
    // also have to handle an older async let ABI that did not provide
    // space for the initial slab in the compiler-generated preallocation.
    if (!hasAsyncLetResultBuffer) {
      allocation = _swift_task_alloc_specific(parent,
                                          amountToAllocate + initialSlabSize);
    } else {
      allocation = asyncLet->getPreallocatedSpace();
      assert(asyncLet->getSizeOfPreallocatedSpace() >= amountToAllocate
             && "async let does not preallocate enough space for child task");
      initialSlabSize = asyncLet->getSizeOfPreallocatedSpace()
                          - amountToAllocate;
    }
  } else {
    allocation = malloc(amountToAllocate);
  }
  SWIFT_TASK_DEBUG_LOG("allocate task %p, parent = %p, slab %u", allocation,
                       parent, initialSlabSize);

  AsyncContext *initialContext =
    reinterpret_cast<AsyncContext*>(
      reinterpret_cast<char*>(allocation) + headerSize);

  //  We can't just use `function` because it uses the new async function entry
  //  ABI -- passing parameters, closure context, indirect result addresses
  //  directly -- but AsyncTask->ResumeTask expects the signature to be
  //  `void (*, *, swiftasync *)`.
  //  Instead we use an adapter. This adaptor should use the storage prefixed to
  //  the async context to get at the parameters.
  //  See e.g. FutureAsyncContextPrefix.

  if (!futureResultType) {
    auto asyncContextPrefix = reinterpret_cast<AsyncContextPrefix *>(
        reinterpret_cast<char *>(allocation) + headerSize -
        sizeof(AsyncContextPrefix));
    asyncContextPrefix->asyncEntryPoint =
        reinterpret_cast<AsyncVoidClosureEntryPoint *>(function);
    asyncContextPrefix->closureContext = closureContext;
    function = non_future_adapter;
    assert(sizeof(AsyncContextPrefix) == 3 * sizeof(void *));
  } else {
    auto asyncContextPrefix = reinterpret_cast<FutureAsyncContextPrefix *>(
        reinterpret_cast<char *>(allocation) + headerSize -
        sizeof(FutureAsyncContextPrefix));
    asyncContextPrefix->asyncEntryPoint =
        reinterpret_cast<AsyncGenericClosureEntryPoint *>(function);
    function = future_adapter;
    asyncContextPrefix->closureContext = closureContext;
    assert(sizeof(FutureAsyncContextPrefix) == 4 * sizeof(void *));
  }

  // Initialize the task so that resuming it will run the given
  // function on the initial context.
  AsyncTask *task = nullptr;
  bool captureCurrentVoucher = taskCreateFlags.copyTaskLocals() || jobFlags.task_isChildTask();
  if (asyncLet) {
    // Initialize the refcount bits to "immortal", so that
    // ARC operations don't have any effect on the task.
    task = new(allocation) AsyncTask(&taskHeapMetadata,
                             InlineRefCounts::Immortal, jobFlags,
                             function, initialContext,
                             captureCurrentVoucher);
  } else {
    task = new(allocation) AsyncTask(&taskHeapMetadata, jobFlags,
                                    function, initialContext,
                                    captureCurrentVoucher);
  }

  // Initialize the child fragment if applicable.
  if (parent) {
    auto childFragment = task->childFragment();
    new (childFragment) AsyncTask::ChildFragment(parent);
  }

  // Initialize the group child fragment if applicable.
  if (group) {
    auto groupChildFragment = task->groupChildFragment();
    new (groupChildFragment) AsyncTask::GroupChildFragment(group);
  }
  
  // Initialize the future fragment if applicable.
  if (futureResultType) {
    assert(task->isFuture());
    auto futureFragment = task->futureFragment();
    new (futureFragment) FutureFragment(futureResultType);

    // Set up the context for the future so there is no error, and a successful
    // result will be written into the future fragment's storage.
    auto futureAsyncContextPrefix =
        reinterpret_cast<FutureAsyncContextPrefix *>(
            reinterpret_cast<char *>(allocation) + headerSize -
            sizeof(FutureAsyncContextPrefix));
    futureAsyncContextPrefix->indirectResult = futureFragment->getStoragePtr();
  }

  SWIFT_TASK_DEBUG_LOG("creating task %p with parent %p", task, parent);

  // Initialize the task-local allocator.
  initialContext->ResumeParent = reinterpret_cast<TaskContinuationFunction *>(
                            asyncLet       ? &completeTask
                          : closureContext ? &completeTaskWithClosure
                                           : &completeTaskAndRelease);
  if (asyncLet && initialSlabSize > 0) {
    assert(parent);
    void *initialSlab = (char*)allocation + amountToAllocate;
    task->Private.initializeWithSlab(task, initialSlab, initialSlabSize);
  } else {
    task->Private.initialize(task);
  }

  // Perform additional linking between parent and child task.
  if (parent) {
    // If the parent was already cancelled, we carry this flag forward to the child.
    //
    // In a task group we would not have allowed the `add` to create a child anymore,
    // however better safe than sorry and `async let` are not expressed as task groups,
    // so they may have been spawned in any case still.
    if (swift_task_isCancelled(parent) ||
        (group && group->isCancelled()))
      swift_task_cancel(task);

    // Initialize task locals with a link to the parent task.
    task->_private().Local.initializeLinkParent(task, parent);
  }

  // Configure the initial context.
  //
  // FIXME: if we store a null pointer here using the standard ABI for
  // signed null pointers, then we'll have to authenticate context pointers
  // as if they might be null, even though the only time they ever might
  // be is the final hop.  Store a signed null instead.
  initialContext->Parent = nullptr;
  initialContext->Flags = AsyncContextKind::Ordinary;

  // Attach to the group, if needed.
  if (group) {
    swift_taskGroup_attachChild(group, task);
  }

  // If we're supposed to copy task locals, do so now.
  if (taskCreateFlags.copyTaskLocals()) {
    swift_task_localsCopyTo(task);
  }

  // Push the async let task status record.
  if (asyncLet) {
    asyncLet_addImpl(task, asyncLet, !hasAsyncLetResultBuffer);
  }

  // If we're supposed to enqueue the task, do so now.
  if (taskCreateFlags.enqueueJob()) {
    swift_retain(task);
    swift_task_enqueue(task, executor);
  }

  return {task, initialContext};
}

/// Extract the entry point address and initial context size from an async closure value.
template<typename AsyncSignature, uint16_t AuthDiscriminator>
SWIFT_ALWAYS_INLINE // so this doesn't hang out as a ptrauth gadget
std::pair<typename AsyncSignature::FunctionType *, size_t>
getAsyncClosureEntryPointAndContextSize(void *function,
                                        HeapObject *functionContext) {
  auto fnPtr =
      reinterpret_cast<const AsyncFunctionPointer<AsyncSignature> *>(function);
#if SWIFT_PTRAUTH
  fnPtr = (const AsyncFunctionPointer<AsyncSignature> *)ptrauth_auth_data(
      (void *)fnPtr, ptrauth_key_process_independent_data, AuthDiscriminator);
#endif
  return {reinterpret_cast<typename AsyncSignature::FunctionType *>(
              fnPtr->Function.get()),
          fnPtr->ExpectedContextSize};
}

SWIFT_CC(swift)
AsyncTaskAndContext swift::swift_task_create(
    size_t taskCreateFlags,
    TaskOptionRecord *options,
    const Metadata *futureResultType,
    void *closureEntry, HeapObject *closureContext) {
  FutureAsyncSignature::FunctionType *taskEntry;
  size_t initialContextSize;
  std::tie(taskEntry, initialContextSize)
    = getAsyncClosureEntryPointAndContextSize<
      FutureAsyncSignature,
      SpecialPointerAuthDiscriminators::AsyncFutureFunction
    >(closureEntry, closureContext);

  return swift_task_create_common(
      taskCreateFlags, options, futureResultType, taskEntry, closureContext,
      initialContextSize);
}

#ifdef __ARM_ARCH_7K__
__attribute__((noinline))
SWIFT_CC(swiftasync) static void workaround_function_swift_task_future_waitImpl(
    OpaqueValue *result, SWIFT_ASYNC_CONTEXT AsyncContext *callerContext,
    AsyncTask *task, TaskContinuationFunction resumeFunction,
    AsyncContext *callContext) {
  // Make sure we don't eliminate calls to this function.
  asm volatile("" // Do nothing.
               :  // Output list, empty.
               : "r"(result), "r"(callerContext), "r"(task) // Input list.
               : // Clobber list, empty.
  );
  return;
}
#endif

SWIFT_CC(swiftasync)
static void swift_task_future_waitImpl(
  OpaqueValue *result,
  SWIFT_ASYNC_CONTEXT AsyncContext *callerContext,
  AsyncTask *task,
  TaskContinuationFunction *resumeFn,
  AsyncContext *callContext) {
  // Suspend the waiting task.
  auto waitingTask = swift_task_getCurrent();
  waitingTask->ResumeTask = task_future_wait_resume_adapter;
  waitingTask->ResumeContext = callContext;

  // Wait on the future.
  assert(task->isFuture());

  switch (task->waitFuture(waitingTask, callContext, resumeFn, callerContext,
                           result)) {
  case FutureFragment::Status::Executing:
    // The waiting task has been queued on the future.
#ifdef __ARM_ARCH_7K__
    return workaround_function_swift_task_future_waitImpl(
        result, callerContext, task, resumeFn, callContext);
#else
    return;
#endif

  case FutureFragment::Status::Success: {
    // Run the task with a successful result.
    auto future = task->futureFragment();
    future->getResultType()->vw_initializeWithCopy(result,
                                                   future->getStoragePtr());
    return resumeFn(callerContext);
  }

  case FutureFragment::Status::Error:
    swift_Concurrency_fatalError(0, "future reported an error, but wait cannot throw");
  }
}

#ifdef __ARM_ARCH_7K__
__attribute__((noinline))
SWIFT_CC(swiftasync) static void workaround_function_swift_task_future_wait_throwingImpl(
    OpaqueValue *result, SWIFT_ASYNC_CONTEXT AsyncContext *callerContext,
    AsyncTask *task, ThrowingTaskFutureWaitContinuationFunction resumeFunction,
    AsyncContext *callContext) {
  // Make sure we don't eliminate calls to this function.
  asm volatile("" // Do nothing.
               :  // Output list, empty.
               : "r"(result), "r"(callerContext), "r"(task) // Input list.
               : // Clobber list, empty.
  );
  return;
}
#endif

SWIFT_CC(swiftasync)
void swift_task_future_wait_throwingImpl(
    OpaqueValue *result, SWIFT_ASYNC_CONTEXT AsyncContext *callerContext,
    AsyncTask *task,
    ThrowingTaskFutureWaitContinuationFunction *resumeFunction,
    AsyncContext *callContext) {
  auto waitingTask = swift_task_getCurrent();
  // Suspend the waiting task.
  waitingTask->ResumeTask = task_wait_throwing_resume_adapter;
  waitingTask->ResumeContext = callContext;

  auto resumeFn = reinterpret_cast<TaskContinuationFunction *>(resumeFunction);

  // Wait on the future.
  assert(task->isFuture());

  switch (task->waitFuture(waitingTask, callContext, resumeFn, callerContext,
                           result)) {
  case FutureFragment::Status::Executing:
    // The waiting task has been queued on the future.
#ifdef __ARM_ARCH_7K__
    return workaround_function_swift_task_future_wait_throwingImpl(
        result, callerContext, task, resumeFunction, callContext);
#else
    return;
#endif

  case FutureFragment::Status::Success: {
    auto future = task->futureFragment();
    future->getResultType()->vw_initializeWithCopy(result,
                                                   future->getStoragePtr());
    return resumeFunction(callerContext, nullptr /*error*/);
  }

  case FutureFragment::Status::Error: {
    // Run the task with an error result.
    auto future = task->futureFragment();
    auto error = future->getError();
    swift_errorRetain(error);
    return resumeFunction(callerContext, error);
  }
  }
}

size_t swift::swift_task_getJobFlags(AsyncTask *task) {
  return task->Flags.getOpaqueValue();
}

SWIFT_CC(swift)
static AsyncTask *swift_task_suspendImpl() {
  auto task = _swift_task_clearCurrent();
  task->flagAsSuspended();
  return task;
}

SWIFT_CC(swift)
static AsyncTask *swift_continuation_initImpl(ContinuationAsyncContext *context,
                                              AsyncContinuationFlags flags) {
  context->Flags = AsyncContextKind::Continuation;
  if (flags.canThrow()) context->Flags.setCanThrow(true);
  if (flags.isExecutorSwitchForced())
    context->Flags.continuation_setIsExecutorSwitchForced(true);
  context->ErrorResult = nullptr;

  // Set the current executor as the target executor unless there's
  // an executor override.
  if (!flags.hasExecutorOverride())
    context->ResumeToExecutor = ExecutorRef::generic();

  // We can initialize this with a relaxed store because resumption
  // must happen-after this call.
  context->AwaitSynchronization.store(flags.isPreawaited()
                                        ? ContinuationStatus::Awaited
                                        : ContinuationStatus::Pending, 
                                      std::memory_order_relaxed);

  AsyncTask *task;

  // A preawait immediately suspends the task.
  if (flags.isPreawaited()) {
    task = _swift_task_clearCurrent();
    assert(task && "initializing a continuation with no current task");
    task->flagAsSuspended();
  } else {
    task = swift_task_getCurrent();
    assert(task && "initializing a continuation with no current task");
  }

  task->ResumeContext = context;
  task->ResumeTask = context->ResumeParent;

  return task;
}

SWIFT_CC(swiftasync)
static void swift_continuation_awaitImpl(ContinuationAsyncContext *context) {
#ifndef NDEBUG
  auto task = swift_task_getCurrent();
  assert(task && "awaiting continuation without a task");
  assert(task->ResumeContext == context);
  assert(task->ResumeTask == context->ResumeParent);
#endif

  auto &sync = context->AwaitSynchronization;

  auto oldStatus = sync.load(std::memory_order_acquire);
  assert((oldStatus == ContinuationStatus::Pending ||
          oldStatus == ContinuationStatus::Resumed) &&
         "awaiting a corrupt or already-awaited continuation");

  // If the status is already Resumed, we can resume immediately.
  // Comparing against Pending may be very slightly more compact.
  if (oldStatus != ContinuationStatus::Pending) {
    if (context->isExecutorSwitchForced())
      return swift_task_switch(context, context->ResumeParent,
                               context->ResumeToExecutor);
    return context->ResumeParent(context);
  }

  // Load the current task (we alreaady did this in assertions builds).
#ifdef NDEBUG
  auto task = swift_task_getCurrent();
#endif

  // Flag the task as suspended.
  task->flagAsSuspended();

  // Try to transition to Awaited.
  bool success =
    sync.compare_exchange_strong(oldStatus, ContinuationStatus::Awaited,
                                 /*success*/ std::memory_order_release,
                                 /*failure*/ std::memory_order_acquire);

  // If that succeeded, we have nothing to do.
  if (success) {
    _swift_task_clearCurrent();
    return;
  }

  // If it failed, it should be because someone concurrently resumed
  // (note that the compare-exchange above is strong).
  assert(oldStatus == ContinuationStatus::Resumed &&
         "continuation was concurrently corrupted or awaited");

  // Restore the running state of the task and resume it.
  task->flagAsRunning();
  if (context->isExecutorSwitchForced())
    return swift_task_switch(context, context->ResumeParent,
                             context->ResumeToExecutor);
  return context->ResumeParent(context);
}

static void resumeTaskAfterContinuation(AsyncTask *task,
                                        ContinuationAsyncContext *context) {
  auto &sync = context->AwaitSynchronization;
  auto status = sync.load(std::memory_order_acquire);
  assert(status != ContinuationStatus::Resumed &&
         "continuation was already resumed");

  // Make sure TSan knows that the resume call happens-before the task
  // restarting.
  _swift_tsan_release(static_cast<Job *>(task));

  // The status should be either Pending or Awaited.  If it's Awaited,
  // which is probably the most likely option, then we should immediately
  // enqueue; we don't need to update the state because there shouldn't
  // be a racing attempt to resume the continuation.  If it's Pending,
  // we need to set it to Resumed; if that fails (with a strong cmpxchg),
  // it should be because the original thread concurrently set it to
  // Awaited, and so we need to enqueue.
  if (status == ContinuationStatus::Pending &&
      sync.compare_exchange_strong(status, ContinuationStatus::Resumed,
                                   /*success*/ std::memory_order_release,
                                   /*failure*/ std::memory_order_relaxed)) {
    return;
  }
  assert(status == ContinuationStatus::Awaited &&
         "detected concurrent attempt to resume continuation");

  // TODO: maybe in some mode we should set the status to Resumed here
  // to make a stronger best-effort attempt to catch racing attempts to
  // resume the continuation?

  swift_task_enqueue(task, context->ResumeToExecutor);
}

SWIFT_CC(swift)
static void swift_continuation_resumeImpl(AsyncTask *task) {
  auto context = cast<ContinuationAsyncContext>(task->ResumeContext);
  resumeTaskAfterContinuation(task, context);
}

SWIFT_CC(swift)
static void swift_continuation_throwingResumeImpl(AsyncTask *task) {
  auto context = cast<ContinuationAsyncContext>(task->ResumeContext);
  resumeTaskAfterContinuation(task, context);
}


SWIFT_CC(swift)
static void swift_continuation_throwingResumeWithErrorImpl(AsyncTask *task,
                                                /* +1 */ SwiftError *error) {
  auto context = cast<ContinuationAsyncContext>(task->ResumeContext);
  context->ErrorResult = error;
  resumeTaskAfterContinuation(task, context);
}

bool swift::swift_task_isCancelled(AsyncTask *task) {
  return task->isCancelled();
}

SWIFT_CC(swift)
static CancellationNotificationStatusRecord*
swift_task_addCancellationHandlerImpl(
    CancellationNotificationStatusRecord::FunctionType handler,
    void *context) {
  void *allocation =
      swift_task_alloc(sizeof(CancellationNotificationStatusRecord));
  auto unsigned_handler = swift_auth_code(handler, 3848);
  auto *record = new (allocation)
      CancellationNotificationStatusRecord(unsigned_handler, context);

  if (swift_task_addStatusRecord(record))
    return record;

  // else, the task was already cancelled, so while the record was added,
  // we must run it immediately here since no other task will trigger it.
  record->run();
  return record;
}

SWIFT_CC(swift)
static void swift_task_removeCancellationHandlerImpl(
    CancellationNotificationStatusRecord *record) {
  swift_task_removeStatusRecord(record);
  swift_task_dealloc(record);
}

SWIFT_CC(swift)
static NullaryContinuationJob*
swift_task_createNullaryContinuationJobImpl(
    size_t priority,
    AsyncTask *continuation) {
  void *allocation =
      swift_task_alloc(sizeof(NullaryContinuationJob));
  auto *job =
      new (allocation) NullaryContinuationJob(
        swift_task_getCurrent(), static_cast<JobPriority>(priority),
        continuation);

  return job;
}

SWIFT_CC(swift)
void swift::swift_continuation_logFailedCheck(const char *message) {
  swift_reportError(0, message);
}

SWIFT_RUNTIME_ATTRIBUTE_NORETURN
SWIFT_CC(swift)
static void swift_task_asyncMainDrainQueueImpl() {
#if SWIFT_CONCURRENCY_COOPERATIVE_GLOBAL_EXECUTOR
  bool Finished = false;
  swift_task_donateThreadToGlobalExecutorUntil([](void *context) {
    return *reinterpret_cast<bool*>(context);
  }, &Finished);
#elif !SWIFT_CONCURRENCY_ENABLE_DISPATCH
  // FIXME: consider implementing a concurrent global main queue for
  // these environments?
  swift_reportError(0, "operation unsupported without libdispatch: "
                       "swift_task_asyncMainDrainQueue");
#else
#if defined(_WIN32)
  static void(FAR *pfndispatch_main)(void) = NULL;

  if (pfndispatch_main)
    return pfndispatch_main();

  HMODULE hModule = LoadLibraryW(L"dispatch.dll");
  if (hModule == NULL)
    swift_reportError(0, "unable to load dispatch.dll");

  pfndispatch_main =
      reinterpret_cast<void (FAR *)(void)>(GetProcAddress(hModule,
                                                          "dispatch_main"));
  if (pfndispatch_main == NULL)
    swift_reportError(0, "unable to locate dispatch_main in dispatch.dll");

  pfndispatch_main();
  exit(0);
#else
  // CFRunLoop is not available on non-Darwin targets.  Foundation has an
  // implementation, but CoreFoundation is not meant to be exposed.  We can only
  // assume the existence of `CFRunLoopRun` on Darwin platforms, where the
  // system provides an implementation of CoreFoundation.
#if defined(__APPLE__)
  auto runLoop =
      reinterpret_cast<void (*)(void)>(dlsym(RTLD_DEFAULT, "CFRunLoopRun"));
  if (runLoop) {
    runLoop();
    exit(0);
  }
#endif

    dispatch_main();
#endif
#endif
}

#define OVERRIDE_TASK COMPATIBILITY_OVERRIDE
#include COMPATIBILITY_OVERRIDE_INCLUDE_PATH
