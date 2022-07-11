// RUN: %target-parse-verify-swift

import StdlibUnittest
import StdlibCollectionUnittest

//
// Check that Collection.SubSequence is constrained to Collection.
//

// expected-error@+3 {{type 'CollectionWithBadSubSequence' does not conform to protocol 'Collection'}}
// expected-error@+2 {{type 'CollectionWithBadSubSequence' does not conform to protocol 'IndexableBase'}}
// expected-error@+1 {{type 'CollectionWithBadSubSequence' does not conform to protocol 'Sequence'}}
struct CollectionWithBadSubSequence : Collection {
  var startIndex: MinimalIndex {
    fatalError("unreachable")
  }

  var endIndex: MinimalIndex {
    fatalError("unreachable")
  }

  subscript(i: MinimalIndex) -> OpaqueValue<Int> {
    fatalError("unreachable")
  }

  // expected-note@+3 {{possibly intended match 'CollectionWithBadSubSequence.SubSequence' (aka 'OpaqueValue<Int8>') does not conform to 'IndexableBase'}}
  // expected-note@+2 {{possibly intended match}}
  // expected-note@+1 {{possibly intended match}}
  typealias SubSequence = OpaqueValue<Int8>
}

func useCollectionTypeSubSequenceIndex<C : Collection>(_ c: C)
  where C.SubSequence.Index == C.Index {}

func useCollectionTypeSubSequenceGeneratorElement<C : Collection>(_ c: C)
  where C.SubSequence.Iterator.Element == C.Iterator.Element {}

func sortResultIgnored<
  S : Sequence,
  MC : MutableCollection
>(_ sequence: S, mutableCollection: MC, array: [Int])
  where S.Iterator.Element : Comparable, MC.Iterator.Element : Comparable {
  var sequence = sequence // expected-warning {{variable 'sequence' was never mutated; consider changing to 'let' constant}}
  var mutableCollection = mutableCollection // expected-warning {{variable 'mutableCollection' was never mutated; consider changing to 'let' constant}}
  var array = array // expected-warning {{variable 'array' was never mutated; consider changing to 'let' constant}}

  sequence.sorted() // expected-warning {{result of call to 'sorted()' is unused}}
  sequence.sorted { $0 < $1 } // expected-warning {{result of call to 'sorted(by:)' is unused}}

  mutableCollection.sorted() // expected-warning {{result of call to 'sorted()' is unused}}
  mutableCollection.sorted { $0 < $1 } // expected-warning {{result of call to 'sorted(by:)' is unused}}

  array.sorted() // expected-warning {{result of call to 'sorted()' is unused}}
  array.sorted { $0 < $1 } // expected-warning {{result of call to 'sorted(by:)' is unused}}
}

// expected-warning@+1 {{'Indexable' is deprecated: it will be removed in Swift 4.0.  Please use 'Collection' instead}}
struct GoodIndexable : Indexable { 
  func index(after i: Int) -> Int { return i + 1 }
  var startIndex: Int { return 0 }
  var endIndex: Int { return 0 }

  subscript(pos: Int) -> Int { return 0 }
  subscript(bounds: Range<Int>) -> [Int] { return [] }
}


// expected-warning@+2 {{'Indexable' is deprecated: it will be removed in Swift 4.0.  Please use 'Collection' instead}}
// expected-error@+1 {{type 'BadIndexable1' does not conform to protocol 'IndexableBase'}}
struct BadIndexable1 : Indexable {
  func index(after i: Int) -> Int { return i + 1 }
  var startIndex: Int { return 0 }
  var endIndex: Int { return 0 }

  subscript(pos: Int) -> Int { return 0 }

  // Missing 'subscript(_:) -> SubSequence'.
}

// expected-warning@+2 {{'Indexable' is deprecated: it will be removed in Swift 4.0.  Please use 'Collection' instead}}
// expected-error@+1 {{type 'BadIndexable2' does not conform to protocol 'IndexableBase'}}
struct BadIndexable2 : Indexable {
  var startIndex: Int { return 0 }
  var endIndex: Int { return 0 }

  subscript(pos: Int) -> Int { return 0 }
  subscript(bounds: Range<Int>) -> [Int] { return [] }
  // Missing index(after:) -> Int
}

// expected-warning@+1 {{'BidirectionalIndexable' is deprecated: it will be removed in Swift 4.0.  Please use 'BidirectionalCollection' instead}}
struct GoodBidirectionalIndexable1 : BidirectionalIndexable {
  var startIndex: Int { return 0 }
  var endIndex: Int { return 0 }
  func index(after i: Int) -> Int { return i + 1 }
  func index(before i: Int) -> Int { return i - 1 }

  subscript(pos: Int) -> Int { return 0 }
  subscript(bounds: Range<Int>) -> [Int] { return [] }
}

// We'd like to see: {{type 'BadBidirectionalIndexable' does not conform to protocol 'BidirectionalIndexable'}}
// But the compiler doesn't generate that error.
// expected-warning@+1 {{'BidirectionalIndexable' is deprecated: it will be removed in Swift 4.0.  Please use 'BidirectionalCollection' instead}}
struct BadBidirectionalIndexable : BidirectionalIndexable {
  var startIndex: Int { return 0 }
  var endIndex: Int { return 0 }

  subscript(pos: Int) -> Int { return 0 }
  subscript(bounds: Range<Int>) -> [Int] { return [] }

  // This is a poor error message; it would be better to get a message
  // that index(before:) was missing.
  //
  // expected-error@+1 {{'index(after:)' has different argument names from those required by protocol 'BidirectionalIndexable' ('index(before:)'}}
  func index(after i: Int) -> Int { return 0 }
}
