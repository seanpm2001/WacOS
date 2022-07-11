// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend -emit-module -emit-module-path %t/OtherActors.swiftmodule -module-name OtherActors %S/Inputs/OtherActors.swift -disable-availability-checking
// RUN: %target-typecheck-verify-swift -I %t  -disable-availability-checking -warn-concurrency
// REQUIRES: concurrency

import OtherActors // expected-remark{{add '@preconcurrency' to suppress 'Sendable'-related warnings from module 'OtherActors'}}{{1-1=@preconcurrency }}

let immutableGlobal: String = "hello"
var mutableGlobal: String = "can't touch this" // expected-note 5{{var declared here}}

@available(SwiftStdlib 5.1, *)
func globalFunc() { }
@available(SwiftStdlib 5.1, *)
func acceptClosure<T>(_: () -> T) { }
@available(SwiftStdlib 5.1, *)
func acceptConcurrentClosure<T>(_: @Sendable () -> T) { }
@available(SwiftStdlib 5.1, *)
func acceptEscapingClosure<T>(_: @escaping () -> T) { }
@available(SwiftStdlib 5.1, *)
func acceptEscapingClosure<T>(_: @escaping (String) -> ()) async -> T? { nil }

@available(SwiftStdlib 5.1, *)
@discardableResult func acceptAsyncClosure<T>(_: () async -> T) -> T { }
@available(SwiftStdlib 5.1, *)
func acceptEscapingAsyncClosure<T>(_: @escaping () async -> T) { }
@available(SwiftStdlib 5.1, *)
func acceptInout<T>(_: inout T) {}


// ----------------------------------------------------------------------
// Actor state isolation restrictions
// ----------------------------------------------------------------------
@available(SwiftStdlib 5.1, *)
actor MySuperActor {
  var superState: Int = 25 // expected-note {{mutation of this property is only permitted within the actor}}


  func superMethod() { // expected-note {{calls to instance method 'superMethod()' from outside of its actor context are implicitly asynchronous}}
    self.superState += 5
  }

  func superAsyncMethod() async { }

  subscript (index: Int) -> String {
    "\(index)"
  }
}

class Point { // expected-note{{class 'Point' does not conform to the 'Sendable' protocol}}
  var x : Int = 0
  var y : Int = 0
}

@available(SwiftStdlib 5.1, *)
actor MyActor: MySuperActor { // expected-error{{actor types do not support inheritance}}
  let immutable: Int = 17
  // expected-note@+2 2{{property declared here}}
  // expected-note@+1 6{{mutation of this property is only permitted within the actor}}
  var mutable: Int = 71

  // expected-note@+2 3 {{mutation of this property is only permitted within the actor}}
  // expected-note@+1 4{{property declared here}}
  var text: [String] = []

  let point : Point = Point()

  @MainActor
  var name : String = "koala" // expected-note{{property declared here}}

  func accessProp() -> String {
    return self.name // expected-error{{property 'name' isolated to global actor 'MainActor' can not be referenced from actor 'MyActor' in a synchronous context}}
  }

  static func synchronousClass() { }
  static func synchronousStatic() { }

  func synchronous() -> String { text.first ?? "nothing" } // expected-note 9{{calls to instance method 'synchronous()' from outside of its actor context are implicitly asynchronous}}
  func asynchronous() async -> String {
    super.superState += 4
    return synchronous()
  }
}

@available(SwiftStdlib 5.1, *)
actor Camera {
  func accessProp(act : MyActor) async -> String {
    return await act.name
  }
}

@available(SwiftStdlib 5.1, *)
func checkAsyncPropertyAccess() async {
  let act = MyActor()
  let _ : Int = await act.mutable + act.mutable
  act.mutable += 1  // expected-error {{actor-isolated property 'mutable' can not be mutated from a non-isolated context}}

  act.superState += 1 // expected-error {{actor-isolated property 'superState' can not be mutated from a non-isolated context}}

  act.text[0].append("hello") // expected-error{{actor-isolated property 'text' can not be mutated from a non-isolated context}}

  // this is not the same as the above, because Array is a value type
  var arr = await act.text
  arr[0].append("hello")

  act.text.append("no") // expected-error{{actor-isolated property 'text' can not be mutated from a non-isolated context}}

  act.text[0] += "hello" // expected-error{{actor-isolated property 'text' can not be mutated from a non-isolated context}}

  _ = act.point  // expected-warning{{non-sendable type 'Point' in asynchronous access to actor-isolated property 'point' cannot cross actor boundary}}
}

@available(SwiftStdlib 5.1, *)
extension MyActor {
  nonisolated var actorIndependentVar: Int {
    get { 5 }
    set { }
  }

  nonisolated func actorIndependentFunc(otherActor: MyActor) -> Int {
    _ = immutable
    _ = mutable // expected-error{{actor-isolated property 'mutable' can not be referenced from a non-isolated}}
    _ = text[0] // expected-error{{actor-isolated property 'text' can not be referenced from a non-isolated context}}
    _ = synchronous() // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced from a non-isolated context}}

    // nonisolated
    _ = actorIndependentFunc(otherActor: self)
    _ = actorIndependentVar

    actorIndependentVar = 17
    _ = self.actorIndependentFunc(otherActor: self)
    _ = self.actorIndependentVar
    self.actorIndependentVar = 17

    // nonisolated on another actor
    _ = otherActor.actorIndependentFunc(otherActor: self)
    _ = otherActor.actorIndependentVar
    otherActor.actorIndependentVar = 17

    // async promotion
    _ = synchronous() // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced from a non-isolated context}}

    // Global actors
    syncGlobalActorFunc() /// expected-error{{call to global actor 'SomeGlobalActor'-isolated global function 'syncGlobalActorFunc()' in a synchronous nonisolated context}}
    _ = syncGlobalActorFunc

    // Global data is okay if it is immutable.
    _ = immutableGlobal
    _ = mutableGlobal // expected-warning{{reference to var 'mutableGlobal' is not concurrency-safe because it involves shared mutable state}}

    // Partial application
    _ = synchronous  // expected-error{{actor-isolated instance method 'synchronous()' can not be partially applied}}
    _ = super.superMethod // expected-error{{actor-isolated instance method 'superMethod()' can not be referenced from a non-isolated context}}
    acceptClosure(synchronous) // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced from a non-isolated context}}
    acceptClosure(self.synchronous) // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced from a non-isolated context}}
    acceptClosure(otherActor.synchronous) // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced from a non-isolated context}}
    acceptEscapingClosure(synchronous) // expected-error{{actor-isolated instance method 'synchronous()' can not be partially applied}}
    acceptEscapingClosure(self.synchronous) // expected-error{{actor-isolated instance method 'synchronous()' can not be partially applied}}
    acceptEscapingClosure(otherActor.synchronous) // expected-error{{actor-isolated instance method 'synchronous()' can not be partially applied}}

    return 5
  }

  func testAsynchronous(otherActor: MyActor) async {
    _ = immutable
    _ = mutable
    mutable = 0
    _ = synchronous()
    _ = text[0]
    acceptInout(&mutable)

    // Accesses on 'self' are okay.
    _ = self.immutable
    _ = self.mutable
    self.mutable = 0
    _ = self.synchronous()
    _ = await self.asynchronous()
    _ = self.text[0]
    acceptInout(&self.mutable)
    _ = self[0]

    // Accesses on 'super' are okay.
    _ = super.superState
    super.superState = 0
    acceptInout(&super.superState)
    super.superMethod()
    await super.superAsyncMethod()
    _ = super[0]

    // Accesses on other actors can only reference immutable data synchronously,
    // otherwise the access is treated as async
    _ = otherActor.immutable // okay
    _ = otherActor.mutable  // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{property access is 'async'}}
    _ = await otherActor.mutable
    otherActor.mutable = 0  // expected-error{{actor-isolated property 'mutable' can not be mutated on a non-isolated actor instance}}
    acceptInout(&otherActor.mutable)  // expected-error{{actor-isolated property 'mutable' can not be used 'inout' on a non-isolated actor instance}}
    // expected-error@+2{{actor-isolated property 'mutable' can not be mutated on a non-isolated actor instance}}
    // expected-warning@+1{{no 'async' operations occur within 'await' expression}}
    await otherActor.mutable = 0

    _ = otherActor.synchronous()
    // expected-error@-1{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-2{{calls to instance method 'synchronous()' from outside of its actor context are implicitly asynchronous}}
    _ = await otherActor.asynchronous()
    _ = otherActor.text[0]
    // expected-error@-1{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-2{{property access is 'async'}}
    _ = await otherActor.text[0] // okay

    // Global data is okay if it is immutable.
    _ = immutableGlobal
    _ = mutableGlobal // expected-warning{{reference to var 'mutableGlobal' is not concurrency-safe because it involves shared mutable state}}

    // Global functions are not actually safe, but we allow them for now.
    globalFunc()

    // Class methods are okay.
    Self.synchronousClass()
    Self.synchronousStatic()

    // Global actors
    syncGlobalActorFunc() // expected-error{{expression is 'async' but is not marked with 'await'}}{{5-5=await }}
    // expected-note@-1{{calls to global function 'syncGlobalActorFunc()' from outside of its actor context are implicitly asynchronous}}

    await asyncGlobalActorFunc()

    // Closures.
    let localConstant = 17
    var localVar = 17

    // Non-escaping closures are okay.
    acceptClosure {
      _ = text[0]
      _ = self.synchronous()
      _ = localVar
      _ = localConstant
    }

    // Concurrent closures might run... concurrently.
    var otherLocalVar = 12
    acceptConcurrentClosure { [otherLocalVar] in
      defer {
        _ = otherLocalVar
      }

      _ = self.text[0] // expected-error{{actor-isolated property 'text' can not be referenced from a Sendable closure}}
      _ = self.mutable // expected-error{{actor-isolated property 'mutable' can not be referenced from a Sendable closure}}
      self.mutable = 0 // expected-error{{actor-isolated property 'mutable' can not be mutated from a Sendable closure}}
      acceptInout(&self.mutable) // expected-error{{actor-isolated property 'mutable' can not be used 'inout' from a Sendable closure}}
      _ = self.immutable
      _ = self.synchronous() // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced from a Sendable closure}}
      _ = localVar // expected-error{{reference to captured var 'localVar' in concurrently-executing code}}
      localVar = 25 // expected-error{{mutation of captured var 'localVar' in concurrently-executing code}}
      _ = localConstant

      _ = otherLocalVar
    }
    otherLocalVar = 17

    acceptConcurrentClosure { [weak self, otherLocalVar] in
      defer {
        _ = self?.actorIndependentVar
      }

      _ = otherLocalVar
    }

    // Escaping closures are still actor-isolated
    acceptEscapingClosure {
      _ = self.text[0]
      _ = self.mutable
      self.mutable = 0
      acceptInout(&self.mutable)
      _ = self.immutable
      _ = self.synchronous()
      _ = localVar
      _ = localConstant
    }

    // Local functions might run concurrently.
    @Sendable func localFn1() {
      _ = self.text[0] // expected-error{{actor-isolated property 'text' can not be referenced from a Sendable function}}
      _ = self.synchronous() // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced from a Sendable function}}
      _ = localVar // expected-error{{reference to captured var 'localVar' in concurrently-executing code}}
      localVar = 25 // expected-error{{mutation of captured var 'localVar' in concurrently-executing code}}
      _ = localConstant
    }

    @Sendable func localFn2() {
      acceptClosure {
        _ = text[0]  // expected-error{{actor-isolated property 'text' can not be referenced from a non-isolated context}}
        _ = self.synchronous() // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced from a non-isolated context}}
        _ = localVar // expected-error{{reference to captured var 'localVar' in concurrently-executing code}}
        localVar = 25 // expected-error{{mutation of captured var 'localVar' in concurrently-executing code}}
        _ = localConstant
      }
    }

    acceptEscapingClosure {
      localFn1()
      localFn2()
    }

    localVar = 0

    // Partial application
    _ = synchronous
    _ = super.superMethod
    acceptClosure(synchronous)
    acceptClosure(self.synchronous)
    acceptClosure(otherActor.synchronous) // expected-error{{actor-isolated instance method 'synchronous()' can not be referenced on a non-isolated actor instance}}
    acceptEscapingClosure(synchronous)
    acceptEscapingClosure(self.synchronous)
    acceptEscapingClosure(otherActor.synchronous) // expected-error{{actor-isolated instance method 'synchronous()' can not be partially applied}}

    acceptAsyncClosure(self.asynchronous)
    acceptEscapingAsyncClosure(self.asynchronous)
  }
}

// ----------------------------------------------------------------------
// Global actor isolation restrictions
// ----------------------------------------------------------------------
@available(SwiftStdlib 5.1, *)
actor SomeActor { }

@globalActor
@available(SwiftStdlib 5.1, *)
struct SomeGlobalActor {
  static let shared = SomeActor()
}

@globalActor
@available(SwiftStdlib 5.1, *)
struct SomeOtherGlobalActor {
  static let shared = SomeActor()
}

@globalActor
@available(SwiftStdlib 5.1, *)
struct GenericGlobalActor<T> {
  static var shared: SomeActor { SomeActor() }
}

@available(SwiftStdlib 5.1, *)
@SomeGlobalActor func onions() {} // expected-note{{calls to global function 'onions()' from outside of its actor context are implicitly asynchronous}}

@available(SwiftStdlib 5.1, *)
@MainActor func beets() { onions() } // expected-error{{call to global actor 'SomeGlobalActor'-isolated global function 'onions()' in a synchronous main actor-isolated context}}
// expected-note@-1{{calls to global function 'beets()' from outside of its actor context are implicitly asynchronous}}

@available(SwiftStdlib 5.1, *)
actor Crystal {
  // expected-note@+2 {{property declared here}}
  // expected-note@+1 2 {{mutation of this property is only permitted within the actor}}
  @SomeGlobalActor var globActorVar : Int = 0

  // expected-note@+1 {{mutation of this property is only permitted within the actor}}
  @SomeGlobalActor var globActorProp : Int {
    get { return 0 }
    set {}
  }

  @SomeGlobalActor func foo(_ x : inout Int) {}

  func referToGlobProps() async {
    _ = await globActorVar + globActorProp

    globActorProp = 20 // expected-error {{property 'globActorProp' isolated to global actor 'SomeGlobalActor' can not be mutated from actor 'Crystal'}}

    globActorVar = 30 // expected-error {{property 'globActorVar' isolated to global actor 'SomeGlobalActor' can not be mutated from actor 'Crystal'}}

    // expected-error@+2 {{property 'globActorVar' isolated to global actor 'SomeGlobalActor' can not be used 'inout' from actor 'Crystal'}}
    // expected-error@+1 {{actor-isolated property 'globActorVar' cannot be passed 'inout' to implicitly 'async' function call}}
    await self.foo(&globActorVar)

    _ = self.foo
  }
}

@available(SwiftStdlib 5.1, *)
@SomeGlobalActor func syncGlobalActorFunc() { syncGlobalActorFunc() } // expected-note {{calls to global function 'syncGlobalActorFunc()' from outside of its actor context are implicitly asynchronous}}
@available(SwiftStdlib 5.1, *)
@SomeGlobalActor func asyncGlobalActorFunc() async { await asyncGlobalActorFunc() }

@available(SwiftStdlib 5.1, *)
@SomeOtherGlobalActor func syncOtherGlobalActorFunc() { }

@available(SwiftStdlib 5.1, *)
@SomeOtherGlobalActor func asyncOtherGlobalActorFunc() async {
  await syncGlobalActorFunc()
  await asyncGlobalActorFunc()
}

@available(SwiftStdlib 5.1, *)
func testGlobalActorClosures() {
  let _: Int = acceptAsyncClosure { @SomeGlobalActor in
    syncGlobalActorFunc()
    syncOtherGlobalActorFunc() // expected-error{{expression is 'async' but is not marked with 'await'}}{{5-5=await }}
    // expected-note@-1{{calls to global function 'syncOtherGlobalActorFunc()' from outside of its actor context are implicitly asynchronous}}

    await syncOtherGlobalActorFunc()
    return 17
  }

  acceptConcurrentClosure { @SomeGlobalActor in 5 } // expected-warning{{converting function value of type '@SomeGlobalActor @Sendable () -> Int' to '@Sendable () -> Int' loses global actor 'SomeGlobalActor'}}
}

@available(SwiftStdlib 5.1, *)
extension MyActor {
  @SomeGlobalActor func onGlobalActor(otherActor: MyActor) async {
    // Access to other functions in this actor are okay.
    syncGlobalActorFunc()
    await asyncGlobalActorFunc()

    // Other global actors are ok if marked with 'await'
    await syncOtherGlobalActorFunc()
    await asyncOtherGlobalActorFunc()

    _ = immutable
    _ = mutable // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{property access is 'async'}}
    _ = await mutable
    _ = synchronous() // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{calls to instance method 'synchronous()' from outside of its actor context are implicitly asynchronous}}
    _ = await synchronous()
    _ = text[0] // expected-error{{expression is 'async' but is not marked with 'await'}}
    // expected-note@-1{{property access is 'async'}}

    _ = await text[0]

    // Accesses on 'self' are only okay for immutable and asynchronous, because
    // we are outside of the actor instance.
    _ = self.immutable
    _ = self.synchronous() // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{calls to instance method 'synchronous()' from outside of its actor context are implicitly asynchronous}}
    _ = await self.synchronous()

    _ = await self.asynchronous()
    _ = self.text[0] // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{property access is 'async'}}
    _ = self[0] // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{subscript access is 'async'}}
    _ = await self.text[0]
    _ = await self[0]

    // Accesses on 'super' are not okay without 'await'; we're outside of the actor.
    _ = super.superState // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{property access is 'async'}}
    _ = await super.superState
    super.superMethod() // expected-error{{expression is 'async' but is not marked with 'await'}}{{5-5=await }}
  // expected-note@-1{{calls to instance method 'superMethod()' from outside of its actor context are implicitly asynchronous}}

    await super.superMethod()
    await super.superAsyncMethod()
    _ = super[0] // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{subscript access is 'async'}}
    _ = await super[0]

    // Accesses on other actors can only reference immutable data or
    // call asynchronous methods
    _ = otherActor.immutable // okay
    _ = otherActor.synchronous() // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{calls to instance method 'synchronous()' from outside of its actor context are implicitly asynchronous}}
    _ = otherActor.synchronous  // expected-error{{actor-isolated instance method 'synchronous()' can not be partially applied}}
    _ = await otherActor.asynchronous()
    _ = otherActor.text[0] // expected-error{{expression is 'async' but is not marked with 'await'}}{{9-9=await }}
    // expected-note@-1{{property access is 'async'}}
    _ = await otherActor.text[0]
  }
}

func testBadImplicitGlobalActorClosureCall() async {
  { @MainActor in  }() // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-note@-1{{calls function of type '@MainActor () -> ()' from outside of its actor context are implicitly asynchronous}}
}


@available(SwiftStdlib 5.1, *)
struct GenericStruct<T> {
  @GenericGlobalActor<T> func f() { } // expected-note {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}

  @GenericGlobalActor<T> func g() {
    f() // okay
  }

  @GenericGlobalActor<String> func h() {
    f() // expected-error{{call to global actor 'GenericGlobalActor<T>'-isolated instance method 'f()' in a synchronous global actor 'GenericGlobalActor<String>'-isolated context}}
    let fn = f // expected-note{{calls to let 'fn' from outside of its actor context are implicitly asynchronous}}
    fn() // expected-error{{call to global actor 'GenericGlobalActor<T>'-isolated let 'fn' in a synchronous global actor 'GenericGlobalActor<String>'-isolated context}}
  }
}

@available(SwiftStdlib 5.1, *)
extension GenericStruct where T == String {
  @GenericGlobalActor<T>
  func h2() {
    f()
    g()
    h()
  }
}

@SomeGlobalActor
var number: Int = 42 // expected-note {{var declared here}}

// expected-note@+1 {{add '@SomeGlobalActor' to make global function 'badNumberUser()' part of global actor 'SomeGlobalActor'}}
func badNumberUser() {
  //expected-error@+1{{var 'number' isolated to global actor 'SomeGlobalActor' can not be referenced from this synchronous context}}
  print("The protected number is: \(number)")
}

@available(SwiftStdlib 5.1, *)
func asyncBadNumberUser() async {
  print("The protected number is: \(await number)")
}

// ----------------------------------------------------------------------
// Non-actor code isolation restrictions
// ----------------------------------------------------------------------
@available(SwiftStdlib 5.1, *)
func testGlobalRestrictions(actor: MyActor) async {
  let _ = MyActor()

  // references to sync methods must be fully applied.
  _ = actor.synchronous // expected-error{{actor-isolated instance method 'synchronous()' can not be partially applied}}
  _ = actor.asynchronous

  // any kind of method can be called from outside the actor, so long as it's marked with 'await'
  _ = actor.synchronous() // expected-error{{expression is 'async' but is not marked with 'await'}}{{7-7=await }}
  // expected-note@-1{{calls to instance method 'synchronous()' from outside of its actor context are implicitly asynchronous}}
  _ = actor.asynchronous() // expected-error{{expression is 'async' but is not marked with 'await'}}{{7-7=await }}
  // expected-note@-1{{call is 'async'}}

  _ = await actor.synchronous()
  _ = await actor.asynchronous()

  // stored and computed properties can be accessed. Only immutable stored properties can be accessed without 'await'
  _ = actor.immutable
  _ = await actor.immutable // expected-warning {{no 'async' operations occur within 'await' expression}}
  _ = actor.mutable  // expected-error{{expression is 'async' but is not marked with 'await'}}{{7-7=await }}
  // expected-note@-1{{property access is 'async'}}
  _ = await actor.mutable
  _ = actor.text[0] // expected-error{{expression is 'async' but is not marked with 'await'}}{{7-7=await }}
  // expected-note@-1{{property access is 'async'}}
  _ = await actor.text[0]
  _ = actor[0] // expected-error{{expression is 'async' but is not marked with 'await'}}{{7-7=await }}
  // expected-note@-1{{subscript access is 'async'}}
  _ = await actor[0]

  // nonisolated declarations are permitted.
  _ = actor.actorIndependentFunc(otherActor: actor)
  _ = actor.actorIndependentVar
  actor.actorIndependentVar = 5

  // Operations on non-instances are permitted.
  MyActor.synchronousStatic()
  MyActor.synchronousClass()

  // Global mutable state cannot be accessed.
  _ = mutableGlobal // expected-warning{{reference to var 'mutableGlobal' is not concurrency-safe because it involves shared mutable state}}

  // Local mutable variables cannot be accessed from concurrently-executing
  // code.
  var i = 17
  acceptConcurrentClosure {
    _ = i // expected-error{{reference to captured var 'i' in concurrently-executing code}}
    i = 42 // expected-error{{mutation of captured var 'i' in concurrently-executing code}}
  }
  print(i)

  acceptConcurrentClosure { [i] in
    _ = i
  }

  print("\(number)") //expected-error {{expression is 'async' but is not marked with 'await'}}{{12-12=await }}
  //expected-note@-1{{property access is 'async'}}

}

@available(SwiftStdlib 5.1, *)
func f() {
  acceptConcurrentClosure {
    _ = mutableGlobal // expected-warning{{reference to var 'mutableGlobal' is not concurrency-safe because it involves shared mutable state}}
  }

  @Sendable func g() {
    _ = mutableGlobal // expected-warning{{reference to var 'mutableGlobal' is not concurrency-safe because it involves shared mutable state}}
  }
}

// ----------------------------------------------------------------------
// Local function isolation restrictions
// ----------------------------------------------------------------------
@available(SwiftStdlib 5.1, *)
actor AnActorWithClosures {
  var counter: Int = 0 // expected-note 2 {{mutation of this property is only permitted within the actor}}
  func exec() {
    acceptEscapingClosure { [unowned self] in
      self.counter += 1

      acceptEscapingClosure {
        self.counter += 1

        acceptEscapingClosure { [self] in
          self.counter += 1
        }

        acceptConcurrentClosure { [self] in
          self.counter += 1 // expected-error{{actor-isolated property 'counter' can not be mutated from a Sendable closure}}

          acceptEscapingClosure {
            self.counter += 1 // expected-error{{actor-isolated property 'counter' can not be mutated from a non-isolated context}}
          }
        }
      }
    }
  }
}

// ----------------------------------------------------------------------
// Local function isolation restrictions
// ----------------------------------------------------------------------
@available(SwiftStdlib 5.1, *)
func checkLocalFunctions() async {
  var i = 0
  var j = 0

  func local1() {
    i = 17
  }

  func local2() { // expected-error{{concurrently-executed local function 'local2()' must be marked as '@Sendable'}}{{3-3=@Sendable }}
    j = 42
  }

  // Okay to call locally.
  local1()
  local2()

  // non-sendable closures don't cause problems.
  acceptClosure {
    local1()
    local2()
  }

  // Escaping closures can make the local function execute concurrently.
  acceptConcurrentClosure {
    local2() // expected-warning{{capture of 'local2()' with non-sendable type '() -> ()' in a `@Sendable` closure}}
    // expected-note@-1{{a function type must be marked '@Sendable' to conform to 'Sendable'}}
  }

  print(i)
  print(j)

  var k = 17
  func local4() {
    acceptConcurrentClosure {
      local3() // expected-warning{{capture of 'local3()' with non-sendable type '() -> ()' in a `@Sendable` closure}}
      // expected-note@-1{{a function type must be marked '@Sendable' to conform to 'Sendable'}}
    }
  }

  func local3() { // expected-error{{concurrently-executed local function 'local3()' must be marked as '@Sendable'}}
    k = 25 // expected-error{{mutation of captured var 'k' in concurrently-executing code}}
  }

  print(k)
}

@available(SwiftStdlib 5.1, *)
actor LocalFunctionIsolatedActor {
  func a() -> Bool { // expected-note{{calls to instance method 'a()' from outside of its actor context are implicitly asynchronous}}
    return true
  }

  func b() -> Bool {
    func c() -> Bool {
      return true && a() // okay, c is isolated
    }
    return c()
  }

  func b2() -> Bool {
    @Sendable func c() -> Bool {
      return true && a() // expected-error{{actor-isolated instance method 'a()' can not be referenced from a non-isolated context}}
    }
    return c()
  }
}

// ----------------------------------------------------------------------
// Lazy properties with initializers referencing 'self'
// ----------------------------------------------------------------------

@available(SwiftStdlib 5.1, *)
actor LazyActor {
    var v: Int = 0
    // expected-note@-1 6 {{property declared here}}

    let l: Int = 0

    lazy var l11: Int = { v }()
    lazy var l12: Int = v
    lazy var l13: Int = { self.v }()
    lazy var l14: Int = self.v
    lazy var l15: Int = { [unowned self] in self.v }() // expected-error{{actor-isolated property 'v' can not be referenced from a non-isolated context}}

    lazy var l21: Int = { l }()
    lazy var l22: Int = l
    lazy var l23: Int = { self.l }()
    lazy var l24: Int = self.l
    lazy var l25: Int = { [unowned self] in self.l }()

    nonisolated lazy var l31: Int = { v }()
    // expected-error@-1 {{actor-isolated property 'v' can not be referenced from a non-isolated context}}
    nonisolated lazy var l32: Int = v
    // expected-error@-1 {{actor-isolated property 'v' can not be referenced from a non-isolated context}}
    nonisolated lazy var l33: Int = { self.v }()
    // expected-error@-1 {{actor-isolated property 'v' can not be referenced from a non-isolated context}}
    nonisolated lazy var l34: Int = self.v
    // expected-error@-1 {{actor-isolated property 'v' can not be referenced from a non-isolated context}}
    nonisolated lazy var l35: Int = { [unowned self] in self.v }()
    // expected-error@-1 {{actor-isolated property 'v' can not be referenced from a non-isolated context}}

    nonisolated lazy var l41: Int = { l }()
    nonisolated lazy var l42: Int = l
    nonisolated lazy var l43: Int = { self.l }()
    nonisolated lazy var l44: Int = self.l
    nonisolated lazy var l45: Int = { [unowned self] in self.l }()
}

// Infer global actors from context only for instance members.
@available(SwiftStdlib 5.1, *)
@MainActor
class SomeClassInActor {
  enum ID: String { case best }

  func inActor() { } // expected-note{{calls to instance method 'inActor()' from outside of its actor context are implicitly asynchronous}}
}

@available(SwiftStdlib 5.1, *)
extension SomeClassInActor.ID {
  func f(_ object: SomeClassInActor) { // expected-note{{add '@MainActor' to make instance method 'f' part of global actor 'MainActor'}}
    object.inActor() // expected-error{{call to main actor-isolated instance method 'inActor()' in a synchronous nonisolated context}}
  }
}

// ----------------------------------------------------------------------
// Initializers (through typechecking only)
// ----------------------------------------------------------------------
@available(SwiftStdlib 5.1, *)
actor SomeActorWithInits {
  var mutableState: Int = 17
  var otherMutableState: Int

  init(i1: Bool) {
    self.mutableState = 42
    self.otherMutableState = 17

    self.isolated()
    self.nonisolated()
  }

  init(i2: Bool) async {
    self.mutableState = 0
    self.otherMutableState = 1

    self.isolated()
    self.nonisolated()
  }

  convenience init(i3: Bool) {
    self.init(i1: i3)
    self.isolated()     // expected-error{{actor-isolated instance method 'isolated()' can not be referenced from a non-isolated context}}
    self.nonisolated()
  }

  convenience init(i4: Bool) async {
    self.init(i1: i4)
    await self.isolated()
    self.nonisolated()
  }

  @MainActor init(i5: Bool) {
    self.mutableState = 42
    self.otherMutableState = 17

    self.isolated()
    self.nonisolated()
  }

  @MainActor init(i6: Bool) async {
    self.mutableState = 42
    self.otherMutableState = 17

    self.isolated()
    self.nonisolated()
  }

  @MainActor convenience init(i7: Bool) {
    self.init(i1: i7)
    self.isolated()     // expected-error{{actor-isolated instance method 'isolated()' can not be referenced from the main actor}}
    self.nonisolated()
  }

  @MainActor convenience init(i8: Bool) async {
    self.init(i1: i8)
    await self.isolated()
    self.nonisolated()
  }


  func isolated() { } // expected-note 2 {{calls to instance method 'isolated()' from outside of its actor context are implicitly asynchronous}}
  nonisolated func nonisolated() {}
}

@available(SwiftStdlib 5.1, *)
@MainActor
class SomeClassWithInits {
  var mutableState: Int = 17
  var otherMutableState: Int

  static var shared = SomeClassWithInits() // expected-note 2{{static property declared here}}

  init() { // expected-note{{calls to initializer 'init()' from outside of its actor context are implicitly asynchronous}}
    self.mutableState = 42
    self.otherMutableState = 17

    self.isolated()
  }

  deinit {
    print(mutableState) // Okay, we're actor-isolated
    print(SomeClassWithInits.shared) // expected-error{{static property 'shared' isolated to global actor 'MainActor' can not be referenced from this synchronous context}}
    beets() //expected-error{{call to main actor-isolated global function 'beets()' in a synchronous nonisolated context}}
  }

  func isolated() { }

  static func staticIsolated() { // expected-note{{calls to static method 'staticIsolated()' from outside of its actor context are implicitly asynchronous}}
    _ = SomeClassWithInits.shared
  }

  func hasDetached() {
    Task.detached {
      // okay
      await self.isolated()
      self.isolated()
      // expected-error@-1{{expression is 'async' but is not marked with 'await'}}{{7-7=await }}
      // expected-note@-2{{calls to instance method 'isolated()' from outside of its actor context are implicitly asynchronous}}

      print(await self.mutableState)
    }
  }
}

@available(SwiftStdlib 5.1, *)
func outsideSomeClassWithInits() { // expected-note 3 {{add '@MainActor' to make global function 'outsideSomeClassWithInits()' part of global actor 'MainActor'}}
  _ = SomeClassWithInits() // expected-error{{call to main actor-isolated initializer 'init()' in a synchronous nonisolated context}}
  _ = SomeClassWithInits.shared // expected-error{{static property 'shared' isolated to global actor 'MainActor' can not be referenced from this synchronous context}}
  SomeClassWithInits.staticIsolated() // expected-error{{call to main actor-isolated static method 'staticIsolated()' in a synchronous nonisolated context}}
}

// ----------------------------------------------------------------------
// nonisolated let and cross-module let
// ----------------------------------------------------------------------
func testCrossModuleLets(actor: OtherModuleActor) async {
  _ = actor.a         // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-note@-1{{property access is 'async'}}
  _ = await actor.a   // okay
  _ = actor.b         // okay
  _ = actor.c // expected-error{{expression is 'async' but is not marked with 'await'}}
  // expected-note@-1{{property access is 'async'}}
  // expected-warning@-2{{non-sendable type 'SomeClass' in implicitly asynchronous access to actor-isolated property 'c' cannot cross actor boundary}}
  _ = await actor.c // expected-warning{{non-sendable type 'SomeClass' in implicitly asynchronous access to actor-isolated property 'c' cannot cross actor boundary}}
  _ = await actor.d // okay
}

func testCrossModuleAsIsolated(actor: isolated OtherModuleActor) {
  _ = actor.a
  _ = actor.b
  _ = actor.c
  _ = actor.d
}

extension OtherModuleActor {
  func testCrossModuleInExtension() {
    _ = self.a
    _ = self.b
    _ = self.c
    _ = self.d
  }
}


// ----------------------------------------------------------------------
// Actor protocols.
// ----------------------------------------------------------------------

@available(SwiftStdlib 5.1, *)
actor A: Actor { // ok
}

@available(SwiftStdlib 5.1, *)
class C: Actor, UnsafeSendable {
  // expected-error@-1{{non-actor type 'C' cannot conform to the 'Actor' protocol}}
  // expected-warning@-2{{'UnsafeSendable' is deprecated: Use @unchecked Sendable instead}}
  nonisolated var unownedExecutor: UnownedSerialExecutor {
    fatalError()
  }
}

@available(SwiftStdlib 5.1, *)
protocol P: Actor {
  func f()
}

@available(SwiftStdlib 5.1, *)
extension P {
  func g() { f() }
}

@available(SwiftStdlib 5.1, *)
actor MyActorP: P {
  func f() { }

  func h() { g() }
}

@available(SwiftStdlib 5.1, *)
protocol SP {
  static func s()
}

@available(SwiftStdlib 5.1, *)
actor ASP: SP {
  static func s() { }
}

@available(SwiftStdlib 5.1, *)
protocol SPD {
  static func sd()
}
@available(SwiftStdlib 5.1, *)
extension SPD {
  static func sd() { }
}

@available(SwiftStdlib 5.1, *)
actor ASPD: SPD {
}

@available(SwiftStdlib 5.1, *)
func testCrossActorProtocol<T: P>(t: T) async {
  await t.f()
  await t.g()
  t.f()
  // expected-error@-1{{expression is 'async' but is not marked with 'await'}}{{3-3=await }}
  // expected-note@-2{{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
  t.g()
  // expected-error@-1{{expression is 'async' but is not marked with 'await'}}{{3-3=await }}
  // expected-note@-2{{calls to instance method 'g()' from outside of its actor context are implicitly asynchronous}}
  ASP.s()
  ASPD.sd()
}

@available(SwiftStdlib 5.1, *)
protocol Server {
  func send<Message: Codable & Sendable>(message: Message) async throws -> String
}

@available(SwiftStdlib 5.1, *)
actor MyServer : Server {
  // okay, asynchronously accessed from clients of the protocol
  func send<Message: Codable & Sendable>(message: Message) throws -> String { "" }
}

// ----------------------------------------------------------------------
// @_inheritActorContext
// ----------------------------------------------------------------------
func acceptAsyncSendableClosure<T>(_: @Sendable () async -> T) { }
func acceptAsyncSendableClosureInheriting<T>(@_inheritActorContext _: @Sendable () async -> T) { }

@available(SwiftStdlib 5.1, *)
extension MyActor {
  func testSendableAndInheriting() {
    acceptAsyncSendableClosure {
      synchronous() // expected-error{{expression is 'async' but is not marked with 'await'}}
      // expected-note@-1{{calls to instance method 'synchronous()' from outside of its actor context are implicitly asynchronous}}
    }

    acceptAsyncSendableClosure {
      await synchronous() // ok
    }

    acceptAsyncSendableClosureInheriting {
      synchronous() // okay
    }

    acceptAsyncSendableClosureInheriting {
      await synchronous() // expected-warning{{no 'async' operations occur within 'await' expression}}
    }
  }
}

@available(SwiftStdlib 5.1, *)
@MainActor // expected-note {{'GloballyIsolatedProto' is isolated to global actor 'MainActor' here}}
protocol GloballyIsolatedProto {
}

// rdar://75849035 - trying to conform an actor to a global-actor isolated protocol should result in an error
func test_conforming_actor_to_global_actor_protocol() {
  @available(SwiftStdlib 5.1, *)
  actor MyValue : GloballyIsolatedProto {}
  // expected-error@-1 {{actor 'MyValue' cannot conform to global actor isolated protocol 'GloballyIsolatedProto'}}
}

func test_invalid_reference_to_actor_member_without_a_call_note() {
  actor A {
    func partial() { }
  }

  actor Test {
    func returnPartial(other: A) async -> () async -> () {
      let a = other.partial
      // expected-error@-1 {{actor-isolated instance method 'partial()' can not be partially applied}}
      return a
    }
  }
}

// Actor isolation and "defer"
actor Counter {
  var counter: Int = 0

  func next() -> Int {
    defer {
      counter = counter + 1
    }

    return counter
  }

  func localNext() -> Int {
    func doIt() {
      counter = counter + 1
    }
    doIt()

    return counter
  }
}

/// Superclass checks for global actor-qualified class types.
class C2 { }

@SomeGlobalActor
class C3: C2 { } // it's okay to add a global actor to a nonisolated class.

@GenericGlobalActor<U>
class GenericSuper<U> { }

@GenericGlobalActor<[T]>
class GenericSub1<T>: GenericSuper<[T]> { }

@GenericGlobalActor<T>
class GenericSub2<T>: GenericSuper<[T]> { } // expected-error{{global actor 'GenericGlobalActor<T>'-isolated class 'GenericSub2' has different actor isolation from global actor 'GenericGlobalActor<U>'-isolated superclass 'GenericSuper'}}

/// Diagnostics for `nonisolated` on an actor initializer.
actor Butterfly {
  nonisolated init() {} // expected-warning {{'nonisolated' on an actor's synchronous initializer is invalid; this is an error in Swift 6}} {{3-15=}}

  nonisolated init(async: Void) async {}

  nonisolated convenience init(icecream: Void) { // expected-warning {{'nonisolated' on an actor's convenience initializer is redundant; this is an error in Swift 6}} {{3-15=}}
    self.init()
  }

  nonisolated convenience init(cookies: Void) async { // expected-warning {{'nonisolated' on an actor's convenience initializer is redundant; this is an error in Swift 6}} {{3-15=}}
    self.init()
  }
}

// expected-note@+1 2 {{calls to global function 'takeIsolated' from outside of its actor context are implicitly asynchronous}}
func takeIsolated(_ val: isolated SelfParamIsolationNonMethod) {}
func take(_ val: SelfParamIsolationNonMethod) {}

actor SelfParamIsolationNonMethod {
  init(s0: Void) {
    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosureInheriting { self.f() }

    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosure { self.f() }

    // expected-error@+1 {{call to actor-isolated global function 'takeIsolated' in a synchronous nonisolated context}}
    takeIsolated(self)

    take(self)
  }

  @MainActor init(s1: Void) {
    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosureInheriting { self.f() }

    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosure { self.f() }
  }

  init(a1: Void) async {
    acceptAsyncSendableClosureInheriting { self.f() }

    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosure { self.f() }

    takeIsolated(self)

    take(self)
  }

  @MainActor init(a2: Void) async {
    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosureInheriting { self.f() }

    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosure { self.f() }
  }

  nonisolated init(a3: Void) async {
    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosureInheriting { self.f() }

    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosure { self.f() }
  }

  deinit {
    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosureInheriting { self.f() }

    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosure { self.f() }

    // expected-error@+1 {{call to actor-isolated global function 'takeIsolated' in a synchronous nonisolated context}}
    takeIsolated(self)

    take(self)
  }

  func f() {}
}

@MainActor
final class MainActorInit: Sendable {
  init() {
    acceptAsyncSendableClosureInheriting { self.f() }

    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosure { self.f() }
  }

  deinit {
    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosureInheriting { self.f() }

    // expected-note@+2 {{calls to instance method 'f()' from outside of its actor context are implicitly asynchronous}}
    // expected-error@+1 {{expression is 'async' but is not marked with 'await'}}
    acceptAsyncSendableClosure { self.f() }
  }

  func f() {}
}
