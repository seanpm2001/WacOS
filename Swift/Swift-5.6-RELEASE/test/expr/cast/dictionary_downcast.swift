// RUN: %target-typecheck-verify-swift

class C : Hashable {
	var x = 0

  func hash(into hasher: inout Hasher) {
    hasher.combine(x)
  }
}

func == (x: C, y: C) -> Bool { return true }


class D : C {}

// Unrelated to the classes above.
class U : Hashable {
  func hash(into hasher: inout Hasher) {}
}

func == (x: U, y: U) -> Bool { return true }

// Test dictionary forced downcasts
var dictCC = Dictionary<C, C>()
var dictCD = dictCC as! Dictionary<C, D>
var dictDC = dictCC as! Dictionary<D, C>
var dictDD = dictCC as! Dictionary<D, D>

// Test dictionary conditional downcasts
if let _ = dictCC as? Dictionary<C, D> { }
if let _ = dictCC as? Dictionary<D, C> { }
if let _ = dictCC as? Dictionary<D, D> { }

// Test dictionary downcasts to unrelated types.
dictCC as Dictionary<D, U> // expected-error{{cannot convert value of type '[C : C]' to type 'Dictionary<D, U>' in coercion}}
// expected-note@-1 {{arguments to generic parameter 'Key' ('C' and 'D') are expected to be equal}}
// expected-note@-2 {{arguments to generic parameter 'Value' ('C' and 'U') are expected to be equal}}
dictCC as Dictionary<U, D> // expected-error{{cannot convert value of type '[C : C]' to type 'Dictionary<U, D>' in coercion}}
// expected-note@-1 {{arguments to generic parameter 'Key' ('C' and 'U') are expected to be equal}}
// expected-note@-2 {{arguments to generic parameter 'Value' ('C' and 'D') are expected to be equal}}
dictCC as Dictionary<U, U> // expected-error{{cannot convert value of type '[C : C]' to type 'Dictionary<U, U>' in coercion}}
// expected-note@-1 {{arguments to generic parameter 'Key' ('C' and 'U') are expected to be equal}}
// expected-note@-2 {{arguments to generic parameter 'Value' ('C' and 'U') are expected to be equal}}

// Test dictionary conditional downcasts to unrelated types
if let _ = dictCC as? Dictionary<D, U> { } // Ok
if let _ = dictCC as? Dictionary<U, D> { } // Ok
if let _ = dictCC as? Dictionary<U, U> { } // Ok

