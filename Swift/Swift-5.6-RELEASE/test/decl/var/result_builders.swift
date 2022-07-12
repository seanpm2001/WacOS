// RUN: %target-typecheck-verify-swift

@resultBuilder // expected-error {{'@resultBuilder' attribute cannot be applied to this declaration}}
var globalBuilder: Int

@resultBuilder // expected-error {{'@resultBuilder' attribute cannot be applied to this declaration}}
func globalBuilderFunction() -> Int { return 0 }

@resultBuilder
struct Maker {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}

@resultBuilder
class Inventor {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}

@Maker // expected-error {{result builder attribute 'Maker' can only be applied to a parameter, function, or computed property}}
typealias typename = Inventor

@Maker // expected-error {{result builder attribute 'Maker' can only be applied to a variable if it defines a getter}}
var global: Int

// FIXME: should this be allowed?
@Maker
var globalWithEmptyImplicitGetter: Int {}
// expected-error@-1 {{computed property must have accessors specified}}
// expected-error@-3 {{result builder attribute 'Maker' can only be applied to a variable if it defines a getter}}

@Maker
var globalWithEmptyExplicitGetter: Int { get {} }  // expected-error{{type 'Maker' has no member 'buildBlock'}}

@Maker
var globalWithSingleGetter: Int { 0 } // expected-error {{ype 'Maker' has no member 'buildBlock'}}

@Maker
var globalWithMultiGetter: Int { 0; 0 } // expected-error {{ype 'Maker' has no member 'buildBlock'}}

@Maker
func globalFunction() {} // expected-error {{ype 'Maker' has no member 'buildBlock'}}

@Maker
func globalFunctionWithFunctionParam(fn: () -> ()) {}  // expected-error {{ype 'Maker' has no member 'buildBlock'}}

func makerParam(@Maker
                fn: () -> ()) {}

// FIXME: these diagnostics are reversed?
func makerParamRedundant(@Maker // expected-error {{only one result builder attribute can be attached to a parameter}}
                         @Maker // expected-note {{previous result builder specified here}}
                         fn: () -> ()) {}

func makerParamConflict(@Maker // expected-error {{only one result builder attribute can be attached to a parameter}}
                        @Inventor // expected-note {{previous result builder specified here}}
                        fn: () -> ()) {}

func makerParamMissing1(@Missing // expected-error {{unknown attribute 'Missing'}}
                        @Maker
                        fn: () -> ()) {}

func makerParamMissing2(@Maker
                        @Missing // expected-error {{unknown attribute 'Missing'}}
                        fn: () -> ()) {}

func makerParamExtra(@Maker(5) // expected-error {{result builder attributes cannot have arguments}}
                     fn: () -> ()) {}

func makerParamAutoclosure(@Maker // expected-error {{result builder attribute 'Maker' cannot be applied to an autoclosure parameter}}
                           fn: @autoclosure () -> ()) {}

@resultBuilder
struct GenericMaker<T> {} // expected-note {{generic type 'GenericMaker' declared here}} expected-error {{result builder must provide at least one static 'buildBlock' method}}

struct GenericContainer<T> {  // expected-note {{generic type 'GenericContainer' declared here}}
  @resultBuilder
  struct Maker {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}
}

func makeParamUnbound(@GenericMaker // expected-error {{reference to generic type 'GenericMaker' requires arguments}}
                      fn: () -> ()) {}

func makeParamBound(@GenericMaker<Int>
                    fn: () -> ()) {}

func makeParamNestedUnbound(@GenericContainer.Maker // expected-error {{reference to generic type 'GenericContainer' requires arguments}}
                            fn: () -> ()) {}

func makeParamNestedBound(@GenericContainer<Int>.Maker
                          fn: () -> ()) {}


protocol P { }

@resultBuilder
struct ConstrainedGenericMaker<T: P> {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}


struct WithinGeneric<U> {
  func makeParamBoundInContext(@GenericMaker<U> fn: () -> ()) {}

  // expected-error@+1{{type 'U' does not conform to protocol 'P'}}
  func makeParamBoundInContextBad(@ConstrainedGenericMaker<U>
    fn: () -> ()) {}
}

@resultBuilder
struct ValidBuilder1 {
  static func buildBlock(_ exprs: Any...) -> Int { return exprs.count }
}

protocol BuilderFuncHelper {}

extension BuilderFuncHelper {
  static func buildBlock(_ exprs: Any...) -> Int { return exprs.count }
}

@resultBuilder
struct ValidBuilder2: BuilderFuncHelper {}

class BuilderFuncBase {
  static func buildBlock(_ exprs: Any...) -> Int { return exprs.count }
}

@resultBuilder
class ValidBuilder3: BuilderFuncBase {}

@resultBuilder
struct ValidBuilder4 {}
extension ValidBuilder4 {
    static func buildBlock(_ exprs: Any...) -> Int { return exprs.count }
}

@resultBuilder
struct ValidBuilder5 {
    static func buildBlock() -> Int { 0 }
}

@resultBuilder
struct InvalidBuilder1 {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}

@resultBuilder
struct InvalidBuilder2 { // expected-error {{result builder must provide at least one static 'buildBlock' method}}
  func buildBlock(_ exprs: Any...) -> Int { return exprs.count } // expected-note {{did you mean to make instance method 'buildBlock' static?}} {{3-3=static }}
}

@resultBuilder
struct InvalidBuilder3 { // expected-error {{result builder must provide at least one static 'buildBlock' method}}
  var buildBlock: (Any...) -> Int = { return $0.count } // expected-note {{potential match 'buildBlock' is not a static method}}
}

@resultBuilder
struct InvalidBuilder4 {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}
extension InvalidBuilder4 {
  func buildBlock(_ exprs: Any...) -> Int { return exprs.count } // expected-note {{did you mean to make instance method 'buildBlock' static?}} {{3-3=static }}
}

protocol InvalidBuilderHelper {}
extension InvalidBuilderHelper {
  func buildBlock(_ exprs: Any...) -> Int { return exprs.count } // expected-note {{potential match 'buildBlock' is not a static method}}
}

@resultBuilder
struct InvalidBuilder5: InvalidBuilderHelper {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}

@resultBuilder
struct InvalidBuilder6 { // expected-error {{result builder must provide at least one static 'buildBlock' method}}
    static var buildBlock: Int = 0 // expected-note {{potential match 'buildBlock' is not a static method}}
}

struct Callable {
    func callAsFunction(_ exprs: Any...) -> Int { return exprs.count }
}

@resultBuilder
struct InvalidBuilder7 { // expected-error {{result builder must provide at least one static 'buildBlock' method}}
    static var buildBlock = Callable() // expected-note {{potential match 'buildBlock' is not a static method}}
}

class BuilderVarBase {
  static var buildBlock: (Any...) -> Int = { return $0.count } // expected-note {{potential match 'buildBlock' is not a static method}}
}

@resultBuilder
class InvalidBuilder8: BuilderVarBase {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}

protocol BuilderVarHelper {}

extension BuilderVarHelper {
  static var buildBlock: (Any...) -> Int { { return $0.count } } // expected-note {{potential match 'buildBlock' is not a static method}}
}

@resultBuilder
struct InvalidBuilder9: BuilderVarHelper {} // expected-error {{result builder must provide at least one static 'buildBlock' method}}

@resultBuilder
struct InvalidBuilder10 { // expected-error {{result builder must provide at least one static 'buildBlock' method}}
  static var buildBlock: (Any...) -> Int = { return $0.count } // expected-note {{potential match 'buildBlock' is not a static method}}
}

@resultBuilder
enum InvalidBuilder11 { // expected-error {{result builder must provide at least one static 'buildBlock' method}}
    case buildBlock(Any) // expected-note {{enum case 'buildBlock' cannot be used to satisfy the result builder requirement}}
}

struct S {
  @ValidBuilder1 var v1: Int { 1 }
  @ValidBuilder2 var v2: Int { 1 }
  @ValidBuilder3 var v3: Int { 1 }
  @ValidBuilder4 var v4: Int { 1 }
  @ValidBuilder5 func v5() -> Int {}
  @InvalidBuilder1 var i1: Int { 1 } // expected-error {{type 'InvalidBuilder1' has no member 'buildBlock'}}
  @InvalidBuilder2 var i2: Int { 1 } // expected-error {{instance member 'buildBlock' cannot be used on type 'InvalidBuilder2'; did you mean to use a value of this type instead?}}
  @InvalidBuilder3 var i3: Int { 1 } // expected-error {{instance member 'buildBlock' cannot be used on type 'InvalidBuilder3'; did you mean to use a value of this type instead?}}
  @InvalidBuilder4 var i4: Int { 1 } // expected-error {{instance member 'buildBlock' cannot be used on type 'InvalidBuilder4'; did you mean to use a value of this type instead?}}
  @InvalidBuilder5 var i5: Int { 1 } // expected-error {{instance member 'buildBlock' cannot be used on type 'InvalidBuilder5'; did you mean to use a value of this type instead?}}
  @InvalidBuilder6 var i6: Int { 1 } // expected-error {{cannot call value of non-function type 'Int'}}
  @InvalidBuilder7 var i7: Int { 1 }
  @InvalidBuilder8 var i8: Int { 1 }
  @InvalidBuilder9 var i9: Int { 1 }
  @InvalidBuilder10 var i10: Int { 1 }
  @InvalidBuilder11 var i11: InvalidBuilder11 { 1 }
}
