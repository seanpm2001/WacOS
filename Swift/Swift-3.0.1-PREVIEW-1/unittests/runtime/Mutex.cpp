//===--- Mutex.cpp - Mutex and ReadWriteLock Tests ------------------------===//
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

#include "swift/Runtime/Mutex.h"
#include "gtest/gtest.h"
#include <atomic>
#include <chrono>
#include <map>
#include <random>
#include <thread>

using namespace swift;

// When true many of the threaded tests log activity to help triage issues.
static bool trace = false;

template <typename ThreadBody, typename AfterSpinRelease>
void threadedExecute(int threadCount, ThreadBody threadBody,
                     AfterSpinRelease afterSpinRelease) {

  std::vector<std::thread> threads;

  // Block the threads we are about to create.
  std::atomic<bool> spinWait(true);
  std::atomic<int> readyCount(0);
  std::atomic<int> activeCount(0);

  for (int i = 0; i < threadCount; ++i) {
    threads.push_back(std::thread([&, i] {
      readyCount++;

      while (spinWait) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      activeCount++;

      threadBody(i);
    }));
  }

  while (readyCount < threadCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Allow our threads to fight for the lock.
  spinWait = false;

  while (activeCount < threadCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  afterSpinRelease();

  // Wait until all of our threads have finished.
  for (auto &thread : threads) {
    thread.join();
  }
}

template <typename ThreadBody>
void threadedExecute(int threadCount, ThreadBody threadBody) {
  threadedExecute(threadCount, threadBody, [] {});
}

template <typename M, typename C, typename ConsumerBody, typename ProducerBody>
void threadedExecute(M &mutex, C &condition, bool &doneCondition,
                     ConsumerBody consumerBody, ProducerBody producerBody) {

  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;

  // Block the threads we are about to create.
  std::atomic<bool> spinWait(true);

  for (int i = 1; i <= 8; ++i) {
    consumers.push_back(std::thread([&, i] {
      while (spinWait) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      consumerBody(i);

      if (trace)
        printf("### Consumer[%d] thread exiting.\n", i);
    }));
  }

  for (int i = 1; i <= 5; ++i) {
    producers.push_back(std::thread([&, i] {
      while (spinWait) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      producerBody(i);

      if (trace)
        printf("### Producer[%d] thread exiting.\n", i);
    }));
  }

  // Poor mans attempt to get as many threads ready as possible before
  // dropping spinWait, it doesn't have to be perfect.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Allow our threads to fight for the lock.
  spinWait = false;

  // Wait until all of our producer threads have finished.
  for (auto &thread : producers) {
    thread.join();
  }

  // Inform consumers that producers are done.
  mutex.withLockThenNotifyAll(condition, [&] {
    if (trace)
      printf("### Informing consumers we are done.\n");
    doneCondition = true;
  });

  // Wait for consumers to finish.
  for (auto &thread : consumers) {
    thread.join();
  }
}

// -----------------------------------------------------------------------------

template <typename M> void basicLockableThreaded(M &mutex) {
  int count1 = 0;
  int count2 = 0;

  threadedExecute(10, [&](int) {
    for (int j = 0; j < 50; ++j) {
      mutex.lock();
      auto count = count2;
      count1++;
      count2 = count + 1;
      mutex.unlock();
    }
  });

  ASSERT_EQ(count1, 500);
  ASSERT_EQ(count2, 500);
}

TEST(MutexTest, BasicLockableThreaded) {
  Mutex mutex(/* checked = */ true);
  basicLockableThreaded(mutex);
}

TEST(StaticMutexTest, BasicLockableThreaded) {
  static StaticMutex mutex;
  basicLockableThreaded(mutex);
}

TEST(StaticUnsafeMutexTest, BasicLockableThreaded) {
  static StaticUnsafeMutex mutex;
  basicLockableThreaded(mutex);
}

template <typename M> void lockableThreaded(M &mutex) {
  mutex.lock();
  threadedExecute(5, [&](int) { ASSERT_FALSE(mutex.try_lock()); });
  mutex.unlock();
  threadedExecute(1, [&](int) {
    ASSERT_TRUE(mutex.try_lock());
    mutex.unlock();
  });

  int count1 = 0;
  int count2 = 0;
  threadedExecute(10, [&](int) {
    for (int j = 0; j < 50; ++j) {
      if (mutex.try_lock()) {
        auto count = count2;
        count1++;
        count2 = count + 1;
        mutex.unlock();
      } else {
        j--;
      }
    }
  });

  ASSERT_EQ(count1, 500);
  ASSERT_EQ(count2, 500);
}

TEST(MutexTest, LockableThreaded) {
  Mutex mutex(/* checked = */ true);
  lockableThreaded(mutex);
}

TEST(StaticMutexTest, LockableThreaded) {
  static StaticMutex Mutex;
  lockableThreaded(Mutex);
}

template <typename SL, typename M> void scopedLockThreaded(M &mutex) {
  int count1 = 0;
  int count2 = 0;

  threadedExecute(10, [&](int) {
    for (int j = 0; j < 50; ++j) {
      SL guard(mutex);
      auto count = count2;
      count1++;
      count2 = count + 1;
    }
  });

  ASSERT_EQ(count1, 500);
  ASSERT_EQ(count2, 500);
}

TEST(MutexTest, ScopedLockThreaded) {
  Mutex mutex(/* checked = */ true);
  scopedLockThreaded<ScopedLock>(mutex);
}

TEST(StaticMutexTest, ScopedLockThreaded) {
  static StaticMutex Mutex;
  scopedLockThreaded<StaticScopedLock>(Mutex);
}

template <typename SL, typename SU, typename M>
void scopedUnlockUnderScopedLockThreaded(M &mutex) {
  int count1 = 0;
  int count2 = 0;
  int badCount = 0;

  threadedExecute(10, [&](int) {
    for (int j = 0; j < 50; ++j) {
      SL guard(mutex);
      {
        SU unguard(mutex);
        badCount++;
      }
      auto count = count2;
      count1++;
      count2 = count + 1;
    }
  });

  ASSERT_EQ(count1, 500);
  ASSERT_EQ(count2, 500);
}

TEST(MutexTest, ScopedUnlockUnderScopedLockThreaded) {
  Mutex mutex(/* checked = */ true);
  scopedUnlockUnderScopedLockThreaded<ScopedLock, ScopedUnlock>(mutex);
}

TEST(StaticMutexTest, ScopedUnlockUnderScopedLockThreaded) {
  static StaticMutex Mutex;
  scopedUnlockUnderScopedLockThreaded<StaticScopedLock, StaticScopedUnlock>(
      Mutex);
}

template <typename M> void criticalSectionThreaded(M &mutex) {
  int count1 = 0;
  int count2 = 0;

  threadedExecute(10, [&](int) {
    for (int j = 0; j < 50; ++j) {
      mutex.withLock([&] {
        auto count = count2;
        count1++;
        count2 = count + 1;
      });
    }
  });

  ASSERT_EQ(count1, 500);
  ASSERT_EQ(count2, 500);
}

TEST(MutexTest, CriticalSectionThreaded) {
  Mutex mutex(/* checked = */ true);
  criticalSectionThreaded(mutex);
}

TEST(StaticMutexTest, CriticalSectionThreaded) {
  static StaticMutex Mutex;
  criticalSectionThreaded(Mutex);
}

template <typename SL, typename SU, typename M, typename C>
void conditionThreaded(M &mutex, C &condition) {
  bool doneCondition = false;
  int count = 200;

  threadedExecute(
      mutex, condition, doneCondition,
      [&](int index) {
        SL guard(mutex);
        while (true) {
          if (count > 50) {
            count -= 1;
            {
              // To give other consumers a chance.
              SU unguard(mutex);
            }
            if (trace)
              printf("Consumer[%d] count = %d.\n", index, count);
            continue; // keep trying to consume before waiting again.
          } else if (doneCondition && count == 50) {
            if (trace)
              printf("Consumer[%d] count == %d and done!\n", index, count);
            break;
          }
          mutex.wait(condition);
        }
      },
      [&](int index) {
        for (int j = 0; j < 10; j++) {
          mutex.lock();
          count += index;
          if (trace)
            printf("Producer[%d] count = %d.\n", index, count);
          condition.notifyOne();
          mutex.unlock();
        }
        if (trace)
          printf("Producer[%d] done!\n", index);
      });

  ASSERT_EQ(count, 50);
}

TEST(MutexTest, ConditionThreaded) {
  Mutex mutex(/* checked = */ true);
  ConditionVariable condition;
  conditionThreaded<ScopedLock, ScopedUnlock>(mutex, condition);
}

TEST(StaticMutexTest, ConditionThreaded) {
  static StaticMutex mutex;
  static StaticConditionVariable condition;
  conditionThreaded<StaticScopedLock, StaticScopedUnlock>(mutex, condition);
}

template <typename SU, typename M, typename C>
void conditionLockOrWaitLockThenNotifyThreaded(M &mutex, C &condition) {
  bool doneCondition = false;
  int count = 200;

  threadedExecute(
      mutex, condition, doneCondition,
      [&](int index) {
        mutex.withLockOrWait(condition, [&, index] {
          while (true) {
            if (count > 50) {
              count -= 1;
              {
                // To give other consumers a chance.
                SU unguard(mutex);
              }
              if (trace)
                printf("Consumer[%d] count = %d.\n", index, count);
              continue; // keep trying to consume before waiting again.
            } else if (doneCondition && count == 50) {
              if (trace)
                printf("Consumer[%d] count == %d and done!\n", index, count);
              return true;
            }
            return false;
          }
        });
      },
      [&](int index) {
        for (int j = 0; j < 10; j++) {
          mutex.withLockThenNotifyOne(condition, [&, index] {
            count += index;
            if (trace)
              printf("Producer[%d] count = %d.\n", index, count);
          });
        }
        if (trace)
          printf("Producer[%d] done!\n", index);
      });

  ASSERT_EQ(count, 50);
}

TEST(MutexTest, ConditionLockOrWaitLockThenNotifyThreaded) {
  Mutex mutex(/* checked = */ true);
  ConditionVariable condition;
  conditionLockOrWaitLockThenNotifyThreaded<ScopedUnlock>(mutex, condition);
}

TEST(StaticMutexTest, ConditionLockOrWaitLockThenNotifyThreaded) {
  static StaticMutex mutex;
  static StaticConditionVariable condition;
  conditionLockOrWaitLockThenNotifyThreaded<StaticScopedUnlock>(mutex,
                                                                condition);
}

template <typename SRL, bool Locking, typename RW>
void scopedReadThreaded(RW &lock) {
  const int threadCount = 10;

  std::set<int> writerHistory;
  std::vector<std::set<int>> readerHistory;
  readerHistory.assign(threadCount, std::set<int>());

  int protectedValue = 0;
  writerHistory.insert(protectedValue);

  threadedExecute(threadCount,
                  [&](int index) {
                    if (Locking) {
                      for (int i = 0; i < 50; ++i) {
                        {
                          SRL guard(lock);
                          readerHistory[index].insert(protectedValue);
                        }
                        std::this_thread::yield();
                      }
                    } else {
                      lock.readLock();
                      for (int i = 0; i < 50; ++i) {
                        readerHistory[index].insert(protectedValue);

                        {
                          SRL unguard(lock);
                          std::this_thread::yield();
                        }
                      }
                      lock.readUnlock();
                    }
                  },
                  [&] {
                    for (int i = 0; i < 25; ++i) {
                      lock.writeLock();
                      protectedValue += i;
                      writerHistory.insert(protectedValue);
                      lock.writeUnlock();
                    }
                  });

  for (auto &history : readerHistory) {
    for (auto value : history) {
      ASSERT_EQ(writerHistory.count(value), 1U);
    }
  }
}

TEST(ReadWriteLockTest, ScopedReadLockThreaded) {
  ReadWriteLock lock;
  scopedReadThreaded<ScopedReadLock, true>(lock);
}

TEST(StaticReadWriteLockTest, ScopedReadLockThreaded) {
  static StaticReadWriteLock lock;
  scopedReadThreaded<StaticScopedReadLock, true>(lock);
}

TEST(ReadWriteLockTest, ScopedReadUnlockThreaded) {
  ReadWriteLock lock;
  scopedReadThreaded<ScopedReadUnlock, false>(lock);
}

TEST(StaticReadWriteLockTest, ScopedReadUnlockThreaded) {
  static StaticReadWriteLock lock;
  scopedReadThreaded<StaticScopedReadUnlock, false>(lock);
}

template <typename SWL, bool Locking, typename RW>
void scopedWriteLockThreaded(RW &lock) {
  const int threadCount = 10;

  std::set<int> readerHistory;
  std::vector<std::set<int>> writerHistory;
  writerHistory.assign(threadCount, std::set<int>());

  int protectedValue = 0;
  readerHistory.insert(protectedValue);

  threadedExecute(threadCount,
                  [&](int index) {
                    if (Locking) {
                      for (int i = 0; i < 20; ++i) {
                        {
                          SWL guard(lock);
                          protectedValue += index * i;
                          writerHistory[index].insert(protectedValue);
                        }
                        std::this_thread::yield();
                      }
                    } else {
                      lock.writeLock();
                      for (int i = 0; i < 20; ++i) {
                        protectedValue += index * i;
                        writerHistory[index].insert(protectedValue);
                        {
                          SWL unguard(lock);
                          std::this_thread::yield();
                        }
                      }
                      lock.writeUnlock();
                    }
                  },
                  [&] {
                    for (int i = 0; i < 100; ++i) {
                      lock.readLock();
                      readerHistory.insert(protectedValue);
                      lock.readUnlock();
                    }
                  });

  std::set<int> mergedHistory;
  for (auto &history : writerHistory) {
    mergedHistory.insert(history.begin(), history.end());
  }

  for (auto value : readerHistory) {
    ASSERT_EQ(mergedHistory.count(value), 1U);
  }
}

TEST(ReadWriteLockTest, ScopedWriteLockThreaded) {
  ReadWriteLock lock;
  scopedWriteLockThreaded<ScopedWriteLock, true>(lock);
}

TEST(StaticReadWriteLockTest, ScopedWriteLockThreaded) {
  static StaticReadWriteLock lock;
  scopedWriteLockThreaded<StaticScopedWriteLock, true>(lock);
}

TEST(ReadWriteLockTest, ScopedWriteUnlockThreaded) {
  ReadWriteLock lock;
  scopedWriteLockThreaded<ScopedWriteUnlock, false>(lock);
}

TEST(StaticReadWriteLockTest, ScopedWriteUnlockThreaded) {
  static StaticReadWriteLock lock;
  scopedWriteLockThreaded<StaticScopedWriteUnlock, false>(lock);
}

template <typename RW> void readLockWhileReadLockedThreaded(RW &lock) {
  lock.readLock();

  std::vector<bool> results;
  results.assign(10, false);

  std::atomic<bool> done(false);
  threadedExecute(10,
                  [&](int index) {
                    while (!done) {
                      lock.withReadLock([&] {
                        results[index] = true;
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(5));
                      });
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                  },
                  [&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done = true;
                  });

  lock.readUnlock();

  for (auto result : results) {
    ASSERT_TRUE(result);
  }
}

TEST(ReadWriteLockTest, ReadLockWhileReadLockedThreaded) {
  ReadWriteLock lock;
  readLockWhileReadLockedThreaded(lock);
}

TEST(StaticReadWriteLockTest, ReadLockWhileReadLockedThreaded) {
  static StaticReadWriteLock lock;
  readLockWhileReadLockedThreaded(lock);
}

template <typename RW> void readLockWhileWriteLockedThreaded(RW &lock) {
  lock.writeLock();

  std::vector<int> results;
  results.assign(10, 0);

  std::atomic<bool> done(false);
  threadedExecute(10,
                  [&](int index) {
                    while (!done) {
                      lock.withReadLock([&] {
                        results[index] += 1;
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(5));
                      });
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                  },
                  [&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done = true;
                    lock.writeUnlock();
                  });

  for (auto result : results) {
    ASSERT_EQ(result, 1);
  }
}

TEST(ReadWriteLockTest, ReadLockWhileWriteLockedThreaded) {
  ReadWriteLock lock;
  readLockWhileWriteLockedThreaded(lock);
}

TEST(StaticReadWriteLockTest, ReadLockWhileWriteLockedThreaded) {
  static StaticReadWriteLock lock;
  readLockWhileWriteLockedThreaded(lock);
}

template <typename RW> void writeLockWhileReadLockedThreaded(RW &lock) {
  lock.readLock();

  const int threadCount = 10;

  std::vector<int> results;
  results.assign(threadCount, 0);

  std::atomic<bool> done(false);
  threadedExecute(threadCount,
                  [&](int index) {
                    while (!done) {
                      lock.withWriteLock([&] {
                        results[index] += 1;
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(5));
                      });
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                  },
                  [&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done = true;
                    lock.readUnlock();
                  });

  for (auto result : results) {
    ASSERT_EQ(result, 1);
  }
}

TEST(ReadWriteLockTest, WriteLockWhileReadLockedThreaded) {
  ReadWriteLock lock;
  writeLockWhileReadLockedThreaded(lock);
}

TEST(StaticReadWriteLockTest, WriteLockWhileReadLockedThreaded) {
  static StaticReadWriteLock lock;
  writeLockWhileReadLockedThreaded(lock);
}

template <typename RW> void writeLockWhileWriteLockedThreaded(RW &lock) {
  lock.writeLock();

  const int threadCount = 10;

  std::vector<int> results;
  results.assign(threadCount, 0);

  std::atomic<bool> done(false);
  threadedExecute(threadCount,
                  [&](int index) {
                    while (!done) {
                      lock.withWriteLock([&] {
                        results[index] += 1;
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(5));
                      });
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                  },
                  [&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done = true;
                    lock.writeUnlock();
                  });

  for (auto result : results) {
    ASSERT_EQ(result, 1);
  }
}

TEST(ReadWriteLockTest, WriteLockWhileWriteLockedThreaded) {
  ReadWriteLock lock;
  writeLockWhileWriteLockedThreaded(lock);
}

TEST(StaticReadWriteLockTest, WriteLockWhileWriteLockedThreaded) {
  static StaticReadWriteLock lock;
  writeLockWhileWriteLockedThreaded(lock);
}

template <typename RW> void tryReadLockWhileWriteLockedThreaded(RW &lock) {
  lock.writeLock();

  std::atomic<bool> done(false);
  threadedExecute(10,
                  [&](int) {
                    while (!done) {
                      ASSERT_FALSE(lock.try_readLock());
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                  },
                  [&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done = true;
                  });

  lock.writeUnlock();
}

TEST(ReadWriteLockTest, TryReadLockWhileWriteLockedThreaded) {
  ReadWriteLock lock;
  tryReadLockWhileWriteLockedThreaded(lock);
}

TEST(StaticReadWriteLockTest, TryReadLockWhileWriteLockedThreaded) {
  static StaticReadWriteLock lock;
  tryReadLockWhileWriteLockedThreaded(lock);
}

template <typename RW> void tryReadLockWhileReadLockedThreaded(RW &lock) {
  lock.readLock();

  const int threadCount = 10;

  std::vector<bool> results;
  results.assign(threadCount, false);

  std::atomic<bool> done(false);
  threadedExecute(threadCount,
                  [&](int index) {
                    while (!done) {
                      ASSERT_TRUE(lock.try_readLock());
                      results[index] = true;
                      std::this_thread::sleep_for(std::chrono::milliseconds(5));
                      lock.readUnlock();
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                  },
                  [&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done = true;
                  });

  lock.readUnlock();

  for (auto result : results) {
    ASSERT_TRUE(result);
  }
}

TEST(ReadWriteLockTest, TryReadLockWhileReadLockedThreaded) {
  ReadWriteLock lock;
  tryReadLockWhileReadLockedThreaded(lock);
}

TEST(StaticReadWriteLockTest, TryReadLockWhileReadLockedThreaded) {
  static StaticReadWriteLock lock;
  tryReadLockWhileReadLockedThreaded(lock);
}

template <typename RW> void tryWriteLockWhileWriteLockedThreaded(RW &lock) {
  lock.writeLock();

  std::atomic<bool> done(false);
  threadedExecute(10,
                  [&](int) {
                    while (!done) {
                      ASSERT_FALSE(lock.try_writeLock());
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                  },
                  [&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done = true;
                  });

  lock.writeUnlock();
}

TEST(ReadWriteLockTest, TryWriteLockWhileWriteLockedThreaded) {
  ReadWriteLock lock;
  tryWriteLockWhileWriteLockedThreaded(lock);
}

TEST(StaticReadWriteLockTest, TryWriteLockWhileWriteLockedThreaded) {
  static StaticReadWriteLock lock;
  tryWriteLockWhileWriteLockedThreaded(lock);
}

template <typename RW> void tryWriteLockWhileReadLockedThreaded(RW &lock) {
  lock.readLock();

  std::atomic<bool> done(false);
  threadedExecute(10,
                  [&](int) {
                    while (!done) {
                      ASSERT_FALSE(lock.try_writeLock());
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                  },
                  [&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    done = true;
                  });

  lock.readUnlock();
}

TEST(ReadWriteLockTest, TryWriteLockWhileReadLockedThreaded) {
  ReadWriteLock lock;
  tryWriteLockWhileReadLockedThreaded(lock);
}

TEST(StaticReadWriteLockTest, TryWriteLockWhileReadLockedThreaded) {
  static StaticReadWriteLock lock;
  tryWriteLockWhileReadLockedThreaded(lock);
}

template <typename RW> void readWriteLockCacheExampleThreaded(RW &lock) {
  std::map<uint8_t, uint32_t> cache;
  std::vector<std::thread> workers;
  std::vector<std::set<uint8_t>> workerHistory;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, UINT8_MAX);

  workerHistory.push_back(std::set<uint8_t>());
  for (int i = 0; i < 16; i++) {
    uint8_t key = dis(gen);
    cache[key] = 0;
    workerHistory[0].insert(key);

    if (trace)
      printf("WarmUp create for key = %d, value = %d.\n", key, 0);
  }

  // Block the threads we are about to create.
  const int threadCount = 20;
  std::atomic<bool> spinWait(true);
  std::atomic<int> readyCount(0);

  for (int i = 1; i <= threadCount; ++i) {
    workerHistory.push_back(std::set<uint8_t>());
    workers.push_back(std::thread([&, i] {
      readyCount++;

      // Block ourself until we are released to start working.
      while (spinWait) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      for (int j = 0; j < 50; j++) {
        uint8_t key = dis(gen);
        bool found = false;

        auto cacheLookupSection = [&] {
          auto value = cache.find(key);
          if (value == cache.end()) {
            if (trace)
              printf("Worker[%d] miss for key = %d.\n", i, key);
            found = false; // cache miss, need to grab write lock
          }
          if (trace)
            printf("Worker[%d] HIT for key = %d, value = %d.\n", i, key,
                   value->second);
          found = true; // cache hit, no need to grab write lock
        };

        lock.withReadLock(cacheLookupSection);
        if (found) {
          continue;
        }

        lock.withWriteLock([&] {
          cacheLookupSection();
          if (!found) {
            if (trace)
              printf("Worker[%d] create for key = %d, value = %d.\n", i, key,
                     i);
            cache[key] = i;
            workerHistory[i].insert(key);
          }
        });
      }

      if (trace)
        printf("### Worker[%d] thread exiting.\n", i);
    }));
  }

  while (readyCount < threadCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Allow our threads to fight for the lock.
  spinWait = false;

  // Wait until all of our workers threads have finished.
  for (auto &thread : workers) {
    thread.join();
  }

  for (auto &entry : cache) {
    if (trace)
      printf("### Cache dump key = %d, value = %d.\n", entry.first,
             entry.second);
    ASSERT_EQ(workerHistory[entry.second].count(entry.first), 1U);
  }
}

TEST(ReadWriteLockTest, ReadWriteLockCacheExampleThreaded) {
  ReadWriteLock lock;
  readWriteLockCacheExampleThreaded(lock);
}

TEST(StaticReadWriteLockTest, ReadWriteLockCacheExampleThreaded) {
  static StaticReadWriteLock lock;
  readWriteLockCacheExampleThreaded(lock);
}
