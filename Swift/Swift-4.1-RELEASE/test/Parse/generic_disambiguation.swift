// RUN: %target-typecheck-verify-swift

struct A<B> { // expected-note{{generic type 'A' declared here}}
  init(x:Int) {}
  static func c() {}

  struct C<D> {
    static func e() {}
  }

  struct F {}
}
struct B {}
struct D {}

protocol Runcible {}
protocol Fungible {}

func meta<T>(_ m: T.Type) {}
func meta2<T>(_ m: T.Type, _ x: Int) {}

func generic<T>(_ x: T) {}

var a, b, c, d : Int

_ = a < b
_ = (a < b, c > d)
// Parses as generic because of lparen after '>'
(a < b, c > (d)) // expected-error{{cannot specialize a non-generic definition}}
// expected-note@-1 {{while parsing this '<' as a type parameter bracket}}
// Parses as generic because of lparen after '>'
(a<b, c>(d)) // expected-error{{cannot specialize a non-generic definition}}
// expected-note@-1 {{while parsing this '<' as a type parameter bracket}}
_ = a>(b)
_ = a > (b)

generic<Int>(0) // expected-error{{cannot explicitly specialize a generic function}} expected-note{{while parsing this '<' as a type parameter bracket}}

A<B>.c()
A<A<B>>.c()
A<A<B>.F>.c()
A<(A<B>) -> B>.c()
A<[[Int]]>.c()
A<[[A<B>]]>.c()
A<(Int, UnicodeScalar)>.c()
A<(a:Int, b:UnicodeScalar)>.c()
A<Runcible & Fungible>.c()
A<@convention(c) () -> Int32>.c()
A<(@autoclosure @escaping () -> Int, Int) -> Void>.c()
_ = [@convention(block) ()  -> Int]().count
_ = [String: (@escaping (A<B>) -> Int) -> Void]().keys

A<B>(x: 0) // expected-warning{{unused}}

meta(A<B>.self)

meta2(A<B>.self, 0)

// FIXME: Nested generic types. Need to be able to express $T0<A, B, C> in the
// typechecker.
/*
A<B>.C<D>.e()

A<B>.C<D>(0)

meta(A<B>.C<D>.self)
meta2(A<B>.C<D>.self, 0)
 */

// TODO: parse empty <> list
//A<>.c() // e/xpected-error{{xxx}}

A<B, D>.c() // expected-error{{generic type 'A' specialized with too many type parameters (got 2, but expected 1)}}

A<B?>(x: 0) // parses as type // expected-warning{{unused}}
_ = a < b ? c : d

A<(B) throws -> D>(x: 0) // expected-warning{{unused}}
