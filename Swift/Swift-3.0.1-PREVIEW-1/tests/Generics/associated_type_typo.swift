// RUN: %target-parse-verify-swift

// RUN: not %target-swift-frontend -parse -debug-generic-signatures %s > %t.dump 2>&1 
// RUN: %FileCheck -check-prefix CHECK-GENERIC %s < %t.dump

protocol P1 {
  associatedtype Assoc
}

protocol P2 {
  associatedtype AssocP2 : P1
}

protocol P3 { }
protocol P4 { }

// expected-error@+1{{'T' does not have a member type named 'assoc'; did you mean 'Assoc'?}}{{30-35=Assoc}}
func typoAssoc1<T : P1>(x: T.assoc) { } 

// expected-error@+2{{'T' does not have a member type named 'assoc'; did you mean 'Assoc'?}}{{43-48=Assoc}}
// expected-error@+1{{'U' does not have a member type named 'assoc'; did you mean 'Assoc'?}}{{54-59=Assoc}}
func typoAssoc2<T : P1, U : P1>() where T.assoc == U.assoc {}

// CHECK-GENERIC-LABEL: .typoAssoc2
// CHECK-GENERIC: Generic signature: <T, U where T : P1, U : P1, T.Assoc == U.Assoc>

// expected-error@+3{{'T.AssocP2' does not have a member type named 'assoc'; did you mean 'Assoc'?}}{{42-47=Assoc}}
// expected-error@+2{{'U.AssocP2' does not have a member type named 'assoc'; did you mean 'Assoc'?}}{{19-24=Assoc}}
func typoAssoc3<T : P2, U : P2>()
  where U.AssocP2.assoc : P3,  T.AssocP2.assoc : P4,
        T.AssocP2 == U.AssocP2 {}

// expected-error@+2{{'T' does not have a member type named 'Assocp2'; did you mean 'AssocP2'?}}{{39-46=AssocP2}}
// expected-error@+1{{'T.AssocP2' does not have a member type named 'assoc'; did you mean 'Assoc'?}}{{47-52=Assoc}}
func typoAssoc4<T : P2>(_: T) where T.Assocp2.assoc : P3 {}


// CHECK-GENERIC-LABEL: .typoAssoc4@
// CHECK-GENERIC-NEXT: Requirements:
// CHECK-GENERIC-NEXT:   T witness marker
// CHECK-GENERIC-NEXT:   T : P2 [explicit
// CHECK-GENERIC-NEXT:   T[.P2].AssocP2 witness marker
// CHECK-GENERIC-NEXT:   T[.P2].AssocP2 : P1 [protocol
// CHECK-GENERIC-NEXT:   T[.P2].AssocP2[.P1].Assoc witness marker
// CHECK-GENERIC-NEXT:   T[.P2].AssocP2[.P1].Assoc : P3 [explicit
// CHECK-GENERIC-NEXT: Generic signature


// <rdar://problem/19620340>

func typoFunc1<T : P1>(x: TypoType) { // expected-error{{use of undeclared type 'TypoType'}}
  let _: T.Assoc -> () = { let _ = $0 } // expected-error{{'Assoc' is not a member type of 'T'}}
}

func typoFunc2<T : P1>(x: TypoType, y: T) { // expected-error{{use of undeclared type 'TypoType'}}
  let _: T.Assoc -> () = { let _ = $0 } // expected-error{{'Assoc' is not a member type of 'T'}}
}

func typoFunc3<T : P1>(x: TypoType, y: (T.Assoc) -> ()) { // expected-error{{use of undeclared type 'TypoType'}}
}
