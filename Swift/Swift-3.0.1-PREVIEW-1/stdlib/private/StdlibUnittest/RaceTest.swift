//===--- RaceTest.swift ---------------------------------------------------===//
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
/// This file implements support for race tests.
///
/// Race test harness executes the given operation in multiple threads over a
/// set of shared data, trying to ensure that executions overlap in time.
///
/// The name "race test" does not imply that the race actually happens in the
/// harness or in the operation being tested.  The harness contains all the
/// necessary synchronization for its own data, and for publishing test data to
/// threads.  But if the operation under test is, in fact, racy, it should be
/// easier to discover the bug in this environment.
///
/// Every execution of a race test is called a trial.  During a single trial
/// the operation under test is executed multiple times in each thread over
/// different data items (`RaceData` instances).  Different threads process
/// data in different order.  Choosing an appropriate balance between the
/// number of threads and data items, the harness uses the birthday paradox to
/// increase the probability of "collisions" between threads.
///
/// After performing the operation under test, the thread should observe the
/// data in a test-dependent way to detect if presence of other concurrent
/// actions disturbed the result.  The observation should be as short as
/// possible, and the results should be returned as `Observation`.  Evaluation
/// (cross-checking) of observations is deferred until the end of the trial.
///
//===----------------------------------------------------------------------===//

import SwiftPrivate
import SwiftPrivatePthreadExtras
#if os(OSX) || os(iOS)
import Darwin
#elseif os(Linux) || os(FreeBSD) || os(PS4) || os(Android)
import Glibc
#endif

#if _runtime(_ObjC)
import ObjectiveC
#else
func autoreleasepool(invoking code: () -> Void) {
  // Native runtime does not have autorelease pools.  Execute the code
  // directly.
  code()
}
#endif

/// Race tests that need a fresh set of data for every trial should implement
/// this protocol.
///
/// All racing threads execute the same operation, `thread1`.
///
/// Types conforming to this protocol should be structs.  (The type
/// should be a struct to reduce unnecessary reference counting during
/// the test.)  The types should be stateless.
public protocol RaceTestWithPerTrialData {

  /// Input for threads.
  ///
  /// This type should be a class.  (The harness will not pass struct instances
  /// between threads correctly.)
  associatedtype RaceData : AnyObject

  /// Type of thread-local data.
  ///
  /// Thread-local data is newly created for every trial.
  associatedtype ThreadLocalData

  /// Results of the observation made after performing an operation.
  associatedtype Observation

  init()

  /// Creates a fresh instance of `RaceData`.
  func makeRaceData() -> RaceData

  /// Creates a fresh instance of `ThreadLocalData`.
  func makeThreadLocalData() -> ThreadLocalData

  /// Performs the operation under test and makes an observation.
  func thread1(
    _ raceData: RaceData, _ threadLocalData: inout ThreadLocalData) -> Observation

  /// Evaluates the observations made by all threads for a particular instance
  /// of `RaceData`.
  func evaluateObservations(
    _ observations: [Observation],
    _ sink: (RaceTestObservationEvaluation) -> Void)
}

/// The result of evaluating observations.
///
/// Case payloads can carry test-specific data.  Results will be grouped
/// according to it.
public enum RaceTestObservationEvaluation : Equatable, CustomStringConvertible {
  /// Normal 'pass'.
  case pass

  /// An unusual 'pass'.
  case passInteresting(String)

  /// A failure.
  case failure
  case failureInteresting(String)

  public var description: String {
    switch self {
    case .pass:
      return "Pass"

    case .passInteresting(let s):
      return "Pass(\(s))"

    case .failure:
      return "Failure"

    case .failureInteresting(let s):
      return "Failure(\(s))"
    }
  }
}

public func == (
  lhs: RaceTestObservationEvaluation, rhs: RaceTestObservationEvaluation
) -> Bool {
  switch (lhs, rhs) {
  case (.pass, .pass),
       (.failure, .failure):
    return true

  case (.passInteresting(let s1), .passInteresting(let s2)):
    return s1 == s2

  default:
    return false
  }
}

/// An observation result that consists of one `UInt`.
public struct Observation1UInt : Equatable, CustomStringConvertible {
  public var data1: UInt

  public init(_ data1: UInt) {
    self.data1 = data1
  }

  public var description: String {
    return "(\(data1))"
  }
}

public func == (lhs: Observation1UInt, rhs: Observation1UInt) -> Bool {
  return lhs.data1 == rhs.data1
}

/// An observation result that consists of four `UInt`s.
public struct Observation4UInt : Equatable, CustomStringConvertible {
  public var data1: UInt
  public var data2: UInt
  public var data3: UInt
  public var data4: UInt

  public init(_ data1: UInt, _ data2: UInt, _ data3: UInt, _ data4: UInt) {
    self.data1 = data1
    self.data2 = data2
    self.data3 = data3
    self.data4 = data4
  }

  public var description: String {
    return "(\(data1), \(data2), \(data3), \(data4))"
  }
}

public func == (lhs: Observation4UInt, rhs: Observation4UInt) -> Bool {
  return
    lhs.data1 == rhs.data1 &&
    lhs.data2 == rhs.data2 &&
    lhs.data3 == rhs.data3 &&
    lhs.data4 == rhs.data4
}

/// An observation result that consists of three `Int`s.
public struct Observation3Int : Equatable, CustomStringConvertible {
  public var data1: Int
  public var data2: Int
  public var data3: Int

  public init(_ data1: Int, _ data2: Int, _ data3: Int) {
    self.data1 = data1
    self.data2 = data2
    self.data3 = data3
  }

  public var description: String {
    return "(\(data1), \(data2), \(data3))"
  }
}

public func == (lhs: Observation3Int, rhs: Observation3Int) -> Bool {
  return
    lhs.data1 == rhs.data1 &&
    lhs.data2 == rhs.data2 &&
    lhs.data3 == rhs.data3
}

/// An observation result that consists of four `Int`s.
public struct Observation4Int : Equatable, CustomStringConvertible {
  public var data1: Int
  public var data2: Int
  public var data3: Int
  public var data4: Int

  public init(_ data1: Int, _ data2: Int, _ data3: Int, _ data4: Int) {
    self.data1 = data1
    self.data2 = data2
    self.data3 = data3
    self.data4 = data4
  }

  public var description: String {
    return "(\(data1), \(data2), \(data3), \(data4))"
  }
}

public func == (lhs: Observation4Int, rhs: Observation4Int) -> Bool {
  return
    lhs.data1 == rhs.data1 &&
    lhs.data2 == rhs.data2 &&
    lhs.data3 == rhs.data3 &&
    lhs.data4 == rhs.data4
}

/// An observation result that consists of five `Int`s.
public struct Observation5Int : Equatable, CustomStringConvertible {
  public var data1: Int
  public var data2: Int
  public var data3: Int
  public var data4: Int
  public var data5: Int

  public init(
    _ data1: Int, _ data2: Int, _ data3: Int, _ data4: Int, _ data5: Int
  ) {
    self.data1 = data1
    self.data2 = data2
    self.data3 = data3
    self.data4 = data4
    self.data5 = data5
  }

  public var description: String {
    return "(\(data1), \(data2), \(data3), \(data4), \(data5))"
  }
}

public func == (lhs: Observation5Int, rhs: Observation5Int) -> Bool {
  return
    lhs.data1 == rhs.data1 &&
    lhs.data2 == rhs.data2 &&
    lhs.data3 == rhs.data3 &&
    lhs.data4 == rhs.data4 &&
    lhs.data5 == rhs.data5
}

/// An observation result that consists of nine `Int`s.
public struct Observation9Int : Equatable, CustomStringConvertible {
  public var data1: Int
  public var data2: Int
  public var data3: Int
  public var data4: Int
  public var data5: Int
  public var data6: Int
  public var data7: Int
  public var data8: Int
  public var data9: Int

  public init(
    _ data1: Int, _ data2: Int, _ data3: Int, _ data4: Int,
    _ data5: Int, _ data6: Int, _ data7: Int, _ data8: Int,
    _ data9: Int
  ) {
    self.data1 = data1
    self.data2 = data2
    self.data3 = data3
    self.data4 = data4
    self.data5 = data5
    self.data6 = data6
    self.data7 = data7
    self.data8 = data8
    self.data9 = data9
  }

  public var description: String {
    return "(\(data1), \(data2), \(data3), \(data4), \(data5), \(data6), \(data7), \(data8), \(data9))"
  }
}

public func == (lhs: Observation9Int, rhs: Observation9Int) -> Bool {
  return
    lhs.data1 == rhs.data1 &&
    lhs.data2 == rhs.data2 &&
    lhs.data3 == rhs.data3 &&
    lhs.data4 == rhs.data4 &&
    lhs.data5 == rhs.data5 &&
    lhs.data6 == rhs.data6 &&
    lhs.data7 == rhs.data7 &&
    lhs.data8 == rhs.data8 &&
    lhs.data9 == rhs.data9
}

/// A helper that is useful to implement
/// `RaceTestWithPerTrialData.evaluateObservations()` in race tests.
public func evaluateObservationsAllEqual<T : Equatable>(_ observations: [T])
  -> RaceTestObservationEvaluation {
  let first = observations.first!
  for x in observations {
    if x != first {
      return .failure
    }
  }
  return .pass
}

struct _RaceTestAggregatedEvaluations : CustomStringConvertible {
  var passCount: Int = 0
  var passInterestingCount = [String: Int]()
  var failureCount: Int = 0
  var failureInterestingCount = [String: Int]()

  init() {}

  mutating func addEvaluation(_ evaluation: RaceTestObservationEvaluation) {
    switch evaluation {
    case .pass:
      passCount += 1

    case .passInteresting(let s):
      if passInterestingCount[s] == nil {
        passInterestingCount[s] = 0
      }
      passInterestingCount[s] = passInterestingCount[s]! + 1

    case .failure:
      failureCount += 1

    case .failureInteresting(let s):
      if failureInterestingCount[s] == nil {
        failureInterestingCount[s] = 0
      }
      failureInterestingCount[s] = failureInterestingCount[s]! + 1
    }
  }

  var isFailed: Bool {
    return failureCount != 0 || !failureInterestingCount.isEmpty
  }

  var description: String {
    var result = ""
    result += "Pass: \(passCount) times\n"
    for desc in passInterestingCount.keys.sorted() {
      let count = passInterestingCount[desc]!
      result += "Pass \(desc): \(count) times\n"
    }
    result += "Failure: \(failureCount) times\n"
    for desc in failureInterestingCount.keys.sorted() {
      let count = failureInterestingCount[desc]!
      result += "Failure \(desc): \(count) times\n"
    }
    return result
  }
}

// FIXME: protect this class against false sharing.
class _RaceTestWorkerState<RT : RaceTestWithPerTrialData> {
  // FIXME: protect every element of 'raceData' against false sharing.
  var raceData: [RT.RaceData] = []
  var raceDataShuffle: [Int] = []
  var observations: [RT.Observation] = []
}

class _RaceTestSharedState<RT : RaceTestWithPerTrialData> {
  var racingThreadCount: Int

  var trialBarrier: _stdlib_Barrier
  var trialSpinBarrier: _stdlib_AtomicInt = _stdlib_AtomicInt()

  var raceData: [RT.RaceData] = []
  var workerStates: [_RaceTestWorkerState<RT>] = []
  var aggregatedEvaluations: _RaceTestAggregatedEvaluations =
    _RaceTestAggregatedEvaluations()

  init(racingThreadCount: Int) {
    self.racingThreadCount = racingThreadCount
    self.trialBarrier = _stdlib_Barrier(threadCount: racingThreadCount + 1)

    self.workerStates.reserveCapacity(racingThreadCount)
    for _ in 0..<racingThreadCount {
      self.workerStates.append(_RaceTestWorkerState<RT>())
    }
  }
}

func _masterThreadOneTrial<RT : RaceTestWithPerTrialData>(
  _ sharedState: _RaceTestSharedState<RT>
) {
  let racingThreadCount = sharedState.racingThreadCount
  let raceDataCount = racingThreadCount * racingThreadCount
  let rt = RT()

  sharedState.raceData.removeAll(keepingCapacity: true)

  sharedState.raceData.append(contentsOf: (0..<raceDataCount).lazy.map { i in
    rt.makeRaceData()
  })

  let identityShuffle = Array(0..<sharedState.raceData.count)
  sharedState.workerStates.removeAll(keepingCapacity: true)
  
  sharedState.workerStates.append(contentsOf: (0..<racingThreadCount).lazy.map {
    i in
    let workerState = _RaceTestWorkerState<RT>()

    // Shuffle the data so that threads process it in different order.
    let shuffle = randomShuffle(identityShuffle)
    workerState.raceData = scatter(sharedState.raceData, shuffle)
    workerState.raceDataShuffle = shuffle

    workerState.observations = []
    workerState.observations.reserveCapacity(sharedState.raceData.count)

    return workerState
  })

  sharedState.trialSpinBarrier.store(0)
  sharedState.trialBarrier.wait()
  // Race happens.
  sharedState.trialBarrier.wait()

  // Collect and compare results.
  for i in 0..<racingThreadCount {
    let shuffle = sharedState.workerStates[i].raceDataShuffle
    sharedState.workerStates[i].raceData =
      gather(sharedState.workerStates[i].raceData, shuffle)
    sharedState.workerStates[i].observations =
      gather(sharedState.workerStates[i].observations, shuffle)
  }
  if true {
    // FIXME: why doesn't the bracket syntax work?
    // <rdar://problem/18305718> Array sugar syntax does not work when used
    // with associated types
    var observations: [RT.Observation] = []
    observations.reserveCapacity(racingThreadCount)
    for i in 0..<raceDataCount {
      for j in 0..<racingThreadCount {
        observations.append(sharedState.workerStates[j].observations[i])
      }

      let sink = { sharedState.aggregatedEvaluations.addEvaluation($0) }
      rt.evaluateObservations(observations, sink)
      observations.removeAll(keepingCapacity: true)
    }
  }
}

func _workerThreadOneTrial<RT : RaceTestWithPerTrialData>(
  _ tid: Int, _ sharedState: _RaceTestSharedState<RT>
) {
  sharedState.trialBarrier.wait()
  let racingThreadCount = sharedState.racingThreadCount
  let workerState = sharedState.workerStates[tid]
  let rt = RT()
  var threadLocalData = rt.makeThreadLocalData()
  if true {
    let trialSpinBarrier = sharedState.trialSpinBarrier
    _ = trialSpinBarrier.fetchAndAdd(1)
    while trialSpinBarrier.load() < racingThreadCount {}
  }
  // Perform racy operations.
  // Warning: do not add any synchronization in this loop, including
  // any implicit reference counting of shared data.
  for raceData in workerState.raceData {
    workerState.observations.append(rt.thread1(raceData, &threadLocalData))
  }
  sharedState.trialBarrier.wait()
}

public func runRaceTest<RT : RaceTestWithPerTrialData>(
  _: RT.Type,
  trials: Int,
  threads: Int? = nil
) {
  let racingThreadCount = threads ?? max(2, _stdlib_getHardwareConcurrency())
  let sharedState = _RaceTestSharedState<RT>(racingThreadCount: racingThreadCount)

  let masterThreadBody: () -> Void = {
    () -> Void in
    for _ in 0..<trials {
      autoreleasepool {
        _masterThreadOneTrial(sharedState)
      }
    }
  }

  let racingThreadBody: (Int) -> Void = {
    (tid: Int) -> Void in
    for _ in 0..<trials {
      _workerThreadOneTrial(tid, sharedState)
    }
  }

  var allTids = [pthread_t]()

  // Create the master thread.
  if true {
    let (ret, tid) = _stdlib_pthread_create_block(
      nil, masterThreadBody, ())
    expectEqual(0, ret)
    allTids.append(tid!)
  }

  // Create racing threads.
  for i in 0..<racingThreadCount {
    let (ret, tid) = _stdlib_pthread_create_block(
      nil, racingThreadBody, i)
    expectEqual(0, ret)
    allTids.append(tid!)
  }

  // Join all threads.
  for tid in allTids {
    let (ret, _) = _stdlib_pthread_join(tid, Void.self)
    expectEqual(0, ret)
  }

  let aggregatedEvaluations = sharedState.aggregatedEvaluations
  expectFalse(aggregatedEvaluations.isFailed)
  print(aggregatedEvaluations)
}

internal func _divideRoundUp(_ lhs: Int, _ rhs: Int) -> Int {
  return (lhs + rhs) / rhs
}

public func runRaceTest<RT : RaceTestWithPerTrialData>(
  _ test: RT.Type,
  operations: Int,
  threads: Int? = nil
) {
  let racingThreadCount = threads ?? max(2, _stdlib_getHardwareConcurrency())

  // Each trial runs threads^2 operations.
  let operationsPerTrial = racingThreadCount * racingThreadCount
  let trials = _divideRoundUp(operations, operationsPerTrial)
  runRaceTest(test, trials: trials, threads: threads)
}

public func consumeCPU(units amountOfWork: Int) {
  for _ in 0..<amountOfWork {
    let scale = 16
    for _ in 0..<scale {
      _blackHole(42)
    }
  }
}

internal struct ClosureBasedRaceTest : RaceTestWithPerTrialData {
  static var thread: () -> () = {}

  class RaceData {}
  typealias ThreadLocalData = Void
  typealias Observation = Void

  func makeRaceData() -> RaceData { return RaceData() }
  func makeThreadLocalData() -> Void { return Void() }

  func thread1(
    _ raceData: RaceData, _ threadLocalData: inout ThreadLocalData
  ) {
    ClosureBasedRaceTest.thread()
  }

  func evaluateObservations(
    _ observations: [Observation],
    _ sink: (RaceTestObservationEvaluation) -> Void
  ) {}
}

public func runRaceTest(
  trials: Int, threads: Int? = nil, invoking body: @escaping () -> ()
) {
  ClosureBasedRaceTest.thread = body
  runRaceTest(ClosureBasedRaceTest.self, trials: trials, threads: threads)
}

