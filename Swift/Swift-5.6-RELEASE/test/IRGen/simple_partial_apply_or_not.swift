// RUN: %target-swift-emit-ir -module-name test %s | %FileCheck %s
// RUN: %target-run-simple-swift %s | %FileCheck %s --check-prefix=CHECK-EXEC

// REQUIRES: executable_test

@propertyWrapper
struct State<T> {
  private class Reference {
    var value: T
    init(value: T) { self.value = value }
  }

  private let ref: Reference

  init(wrappedValue: T) {
    ref = Reference(value: wrappedValue)
  }

  var wrappedValue: T {
    get { ref.value }
    nonmutating set { ref.value = newValue }
  }
}

struct S {
  @State var value: Int = 1

  init() {
    value = 10 // CRASH
  }
}

print("Hello!")
let s = S()
print(s)

// We need to call a partial apply thunk instead of directly calling the method
// because the ABI of closure requires swiftself in the context parameter but
// the method of this self type (struct S) does not.

// CHECK: define {{.*}}swiftcc %T4test5StateV9Reference33_C903A018FCE7355FD30EF8324850EB90LLCySi_G* @"$s4test1SVACycfC"()
// CHECK:  call swiftcc void {{.*}}"$s4test1SV5valueSivsTA
// CHECK:  ret %T4test5StateV9Reference33_C903A018FCE7355FD30EF8324850EB90LLCySi_G

// This used to crash.

// CHECK-EXEC: Hello!
// CHECK-EXEC: S(_value: main.State<Swift.Int>(ref: main.State<Swift.Int>.(unknown context at {{.*}}).Reference))
