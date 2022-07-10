// RUN: %target-parse-verify-swift

class ThisBase1 {
  init() { }

  var baseInstanceVar: Int

  var baseProp : Int {
    get {
      return 42
    }
    set {}
  }

  func baseFunc0() {} // expected-note 2 {{'baseFunc0()' declared here}}
  func baseFunc1(_ a: Int) {}

  subscript(i: Int) -> Double {
    get {
      return Double(i)
    }
    set {
      baseInstanceVar = i
    }
  }

  class var baseStaticVar: Int = 42 // expected-error {{class stored properties not supported}}

  class var baseStaticProp: Int {
    get {
      return 42
    }
    set {}
  }

  class func baseStaticFunc0() {}

  struct BaseNestedStruct {} // expected-note {{did you mean 'BaseNestedStruct'?}}
  class BaseNestedClass {
    init() { }
  }
  enum BaseNestedUnion {
    case BaseUnionX(Int)
  }

  typealias BaseNestedTypealias = Int // expected-note {{did you mean 'BaseNestedTypealias'?}}
}

class ThisDerived1 : ThisBase1 {
  override init() { super.init() }

  var derivedInstanceVar: Int

  var derivedProp : Int {
    get {
      return 42
    }
    set {}
  }

  func derivedFunc0() {}  // expected-note {{'derivedFunc0()' declared here}}
  func derivedFunc1(_ a: Int) {}

  subscript(i: Double) -> Int {
    get {
      return Int(i)
    }
    set {
      baseInstanceVar = Int(i)
    }
  }

  class var derivedStaticVar: Int = 42// expected-error {{class stored properties not supported}}

  class var derivedStaticProp: Int {
    get {
      return 42
    }
    set {}
  }

  class func derivedStaticFunc0() {}

  struct DerivedNestedStruct {}
  class DerivedNestedClass {
    init() { }
  }
  enum DerivedNestedUnion { // expected-note {{did you mean 'DerivedNestedUnion'?}}
    case DerivedUnionX(Int)
  }

  typealias DerivedNestedTypealias = Int

  func testSelf1() {
    self.baseInstanceVar = 42
    self.baseProp = 42
    self.baseFunc0()
    self.baseFunc1(42)
    self[0] = 42.0
    self.baseStaticVar = 42 // expected-error {{static member 'baseStaticVar' cannot be used on instance of type 'ThisDerived1'}}
    self.baseStaticProp = 42 // expected-error {{static member 'baseStaticProp' cannot be used on instance of type 'ThisDerived1'}}
    self.baseStaticFunc0() // expected-error {{static member 'baseStaticFunc0' cannot be used on instance of type 'ThisDerived1'}}

    self.baseExtProp = 42
    self.baseExtFunc0()
    self.baseExtStaticVar = 42
    self.baseExtStaticProp = 42
    self.baseExtStaticFunc0() // expected-error {{static member 'baseExtStaticFunc0' cannot be used on instance of type 'ThisDerived1'}}

    var bs1 : BaseNestedStruct
    var bc1 : BaseNestedClass
    var bo1 : BaseNestedUnion = .BaseUnionX(42)
    var bt1 : BaseNestedTypealias
    var bs2 = self.BaseNestedStruct() // expected-error{{static member 'BaseNestedStruct' cannot be used on instance of type 'ThisDerived1'}}
    var bc2 = self.BaseNestedClass() // expected-error{{static member 'BaseNestedClass' cannot be used on instance of type 'ThisDerived1'}}
    var bo2 = self.BaseUnionX(24) // expected-error {{value of type 'ThisDerived1' has no member 'BaseUnionX'}}
    var bo3 = self.BaseNestedUnion.BaseUnionX(24) // expected-error{{static member 'BaseNestedUnion' cannot be used on instance of type 'ThisDerived1'}}
    var bt2 = self.BaseNestedTypealias(42) // expected-error{{static member 'BaseNestedTypealias' cannot be used on instance of type 'ThisDerived1'}}

    var bes1 : BaseExtNestedStruct
    var bec1 : BaseExtNestedClass
    var beo1 : BaseExtNestedUnion = .BaseExtUnionX(42)
    var bet1 : BaseExtNestedTypealias
    var bes2 = self.BaseExtNestedStruct() // expected-error{{static member 'BaseExtNestedStruct' cannot be used on instance of type 'ThisDerived1'}}
    var bec2 = self.BaseExtNestedClass() // expected-error{{static member 'BaseExtNestedClass' cannot be used on instance of type 'ThisDerived1'}}
    var beo2 = self.BaseExtUnionX(24) // expected-error {{value of type 'ThisDerived1' has no member 'BaseExtUnionX'}}
    var beo3 = self.BaseExtNestedUnion.BaseExtUnionX(24) // expected-error{{static member 'BaseExtNestedUnion' cannot be used on instance of type 'ThisDerived1'}}
    var bet2 = self.BaseExtNestedTypealias(42) // expected-error{{static member 'BaseExtNestedTypealias' cannot be used on instance of type 'ThisDerived1'}}

    self.derivedInstanceVar = 42
    self.derivedProp = 42
    self.derivedFunc0()
    self.derivedStaticVar = 42 // expected-error {{static member 'derivedStaticVar' cannot be used on instance of type 'ThisDerived1'}}
    self.derivedStaticProp = 42 // expected-error {{static member 'derivedStaticProp' cannot be used on instance of type 'ThisDerived1'}}
    self.derivedStaticFunc0() // expected-error {{static member 'derivedStaticFunc0' cannot be used on instance of type 'ThisDerived1'}}

    self.derivedExtProp = 42
    self.derivedExtFunc0()
    self.derivedExtStaticVar = 42
    self.derivedExtStaticProp = 42
    self.derivedExtStaticFunc0() // expected-error {{static member 'derivedExtStaticFunc0' cannot be used on instance of type 'ThisDerived1'}}

    var ds1 : DerivedNestedStruct
    var dc1 : DerivedNestedClass
    var do1 : DerivedNestedUnion = .DerivedUnionX(42)
    var dt1 : DerivedNestedTypealias
    var ds2 = self.DerivedNestedStruct() // expected-error{{static member 'DerivedNestedStruct' cannot be used on instance of type 'ThisDerived1'}}
    var dc2 = self.DerivedNestedClass() // expected-error{{static member 'DerivedNestedClass' cannot be used on instance of type 'ThisDerived1'}}
    var do2 = self.DerivedUnionX(24) // expected-error {{value of type 'ThisDerived1' has no member 'DerivedUnionX'}}
    var do3 = self.DerivedNestedUnion.DerivedUnionX(24) // expected-error{{static member 'DerivedNestedUnion' cannot be used on instance of type 'ThisDerived1'}}
    var dt2 = self.DerivedNestedTypealias(42) // expected-error{{static member 'DerivedNestedTypealias' cannot be used on instance of type 'ThisDerived1'}}

    var des1 : DerivedExtNestedStruct
    var dec1 : DerivedExtNestedClass
    var deo1 : DerivedExtNestedUnion = .DerivedExtUnionX(42)
    var det1 : DerivedExtNestedTypealias
    var des2 = self.DerivedExtNestedStruct() // expected-error{{static member 'DerivedExtNestedStruct' cannot be used on instance of type 'ThisDerived1'}}
    var dec2 = self.DerivedExtNestedClass() // expected-error{{static member 'DerivedExtNestedClass' cannot be used on instance of type 'ThisDerived1'}}
    var deo2 = self.DerivedExtUnionX(24) // expected-error {{value of type 'ThisDerived1' has no member 'DerivedExtUnionX'}}
    var deo3 = self.DerivedExtNestedUnion.DerivedExtUnionX(24) // expected-error{{static member 'DerivedExtNestedUnion' cannot be used on instance of type 'ThisDerived1'}}
    var det2 = self.DerivedExtNestedTypealias(42) // expected-error{{static member 'DerivedExtNestedTypealias' cannot be used on instance of type 'ThisDerived1'}}

    self.Type // expected-error {{value of type 'ThisDerived1' has no member 'Type'}}
  }

  func testSuper1() {
    super.baseInstanceVar = 42
    super.baseProp = 42
    super.baseFunc0()
    super.baseFunc1(42)
    super[0] = 42.0
    super.baseStaticVar = 42 // expected-error {{static member 'baseStaticVar' cannot be used on instance of type 'ThisBase1'}}
    super.baseStaticProp = 42 // expected-error {{static member 'baseStaticProp' cannot be used on instance of type 'ThisBase1'}}
    super.baseStaticFunc0() // expected-error {{static member 'baseStaticFunc0' cannot be used on instance of type 'ThisBase1'}}

    super.baseExtProp = 42
    super.baseExtFunc0()
    super.baseExtStaticVar = 42
    super.baseExtStaticProp = 42
    super.baseExtStaticFunc0() // expected-error {{static member 'baseExtStaticFunc0' cannot be used on instance of type 'ThisBase1'}}

    var bs2 = super.BaseNestedStruct() // expected-error{{static member 'BaseNestedStruct' cannot be used on instance of type 'ThisBase1'}}
    var bc2 = super.BaseNestedClass() // expected-error{{static member 'BaseNestedClass' cannot be used on instance of type 'ThisBase1'}}
    var bo2 = super.BaseUnionX(24) // expected-error {{value of type 'ThisBase1' has no member 'BaseUnionX'}}
    var bo3 = super.BaseNestedUnion.BaseUnionX(24) // expected-error{{static member 'BaseNestedUnion' cannot be used on instance of type 'ThisBase1'}}
    var bt2 = super.BaseNestedTypealias(42) // expected-error{{static member 'BaseNestedTypealias' cannot be used on instance of type 'ThisBase1'}}

    var bes2 = super.BaseExtNestedStruct() // expected-error{{static member 'BaseExtNestedStruct' cannot be used on instance of type 'ThisBase1'}}
    var bec2 = super.BaseExtNestedClass() // expected-error{{static member 'BaseExtNestedClass' cannot be used on instance of type 'ThisBase1'}}
    var beo2 = super.BaseExtUnionX(24) // expected-error {{value of type 'ThisBase1' has no member 'BaseExtUnionX'}}
    var beo3 = super.BaseExtNestedUnion.BaseExtUnionX(24) // expected-error{{static member 'BaseExtNestedUnion' cannot be used on instance of type 'ThisBase1'}}
    var bet2 = super.BaseExtNestedTypealias(42) // expected-error{{static member 'BaseExtNestedTypealias' cannot be used on instance of type 'ThisBase1'}}

    super.derivedInstanceVar = 42 // expected-error {{value of type 'ThisBase1' has no member 'derivedInstanceVar'}}
    super.derivedProp = 42 // expected-error {{value of type 'ThisBase1' has no member 'derivedProp'}}
    super.derivedFunc0() // expected-error {{value of type 'ThisBase1' has no member 'derivedFunc0'}}
    super.derivedStaticVar = 42 // expected-error {{value of type 'ThisBase1' has no member 'derivedStaticVar'}}
    super.derivedStaticProp = 42 // expected-error {{value of type 'ThisBase1' has no member 'derivedStaticProp'}}
    super.derivedStaticFunc0() // expected-error {{value of type 'ThisBase1' has no member 'derivedStaticFunc0'}}

    super.derivedExtProp = 42 // expected-error {{value of type 'ThisBase1' has no member 'derivedExtProp'}}
    super.derivedExtFunc0() // expected-error {{value of type 'ThisBase1' has no member 'derivedExtFunc0'}}
    super.derivedExtStaticVar = 42 // expected-error {{value of type 'ThisBase1' has no member 'derivedExtStaticVar'}}
    super.derivedExtStaticProp = 42 // expected-error {{value of type 'ThisBase1' has no member 'derivedExtStaticProp'}}
    super.derivedExtStaticFunc0() // expected-error {{value of type 'ThisBase1' has no member 'derivedExtStaticFunc0'}}

    var ds2 = super.DerivedNestedStruct() // expected-error {{value of type 'ThisBase1' has no member 'DerivedNestedStruct'}}
    var dc2 = super.DerivedNestedClass() // expected-error {{value of type 'ThisBase1' has no member 'DerivedNestedClass'}}
    var do2 = super.DerivedUnionX(24) // expected-error {{value of type 'ThisBase1' has no member 'DerivedUnionX'}}
    var do3 = super.DerivedNestedUnion.DerivedUnionX(24) // expected-error {{value of type 'ThisBase1' has no member 'DerivedNestedUnion'}}
    var dt2 = super.DerivedNestedTypealias(42) // expected-error {{value of type 'ThisBase1' has no member 'DerivedNestedTypealias'}}

    var des2 = super.DerivedExtNestedStruct() // expected-error {{value of type 'ThisBase1' has no member 'DerivedExtNestedStruct'}}
    var dec2 = super.DerivedExtNestedClass() // expected-error {{value of type 'ThisBase1' has no member 'DerivedExtNestedClass'}}
    var deo2 = super.DerivedExtUnionX(24) // expected-error {{value of type 'ThisBase1' has no member 'DerivedExtUnionX'}}
    var deo3 = super.DerivedExtNestedUnion.DerivedExtUnionX(24) // expected-error {{value of type 'ThisBase1' has no member 'DerivedExtNestedUnion'}}
    var det2 = super.DerivedExtNestedTypealias(42) // expected-error {{value of type 'ThisBase1' has no member 'DerivedExtNestedTypealias'}}

    super.Type // expected-error {{value of type 'ThisBase1' has no member 'Type'}}
  }

  class func staticTestSelf1() {
    self.baseInstanceVar = 42 // expected-error {{member 'baseInstanceVar' cannot be used on type 'ThisDerived1'}}
    self.baseProp = 42 // expected-error {{member 'baseProp' cannot be used on type 'ThisDerived1'}}
    self.baseFunc0() // expected-error {{missing argument}}
    self.baseFunc0(ThisBase1())() // expected-error {{'(ThisBase1) -> () -> ()' is not convertible to '(ThisDerived1) -> () -> ()'}}
    
    self.baseFunc0(ThisDerived1())()
    self.baseFunc1(42) // expected-error {{cannot convert value of type 'Int' to expected argument type 'ThisBase1'}}
    self.baseFunc1(ThisBase1())(42) // expected-error {{'(ThisBase1) -> (Int) -> ()' is not convertible to '(ThisDerived1) -> (Int) -> ()'}}
    self.baseFunc1(ThisDerived1())(42)
    self[0] = 42.0 // expected-error {{instance member 'subscript' cannot be used on type 'ThisDerived1'}}
    self.baseStaticVar = 42
    self.baseStaticProp = 42
    self.baseStaticFunc0()

    self.baseExtProp = 42 // expected-error {{member 'baseExtProp' cannot be used on type 'ThisDerived1'}}
    self.baseExtFunc0() // expected-error {{missing argument}}
    self.baseExtStaticVar = 42
    self.baseExtStaticProp = 42 // expected-error {{member 'baseExtStaticProp' cannot be used on type 'ThisDerived1'}}
    self.baseExtStaticFunc0()

    var bs1 : BaseNestedStruct
    var bc1 : BaseNestedClass
    var bo1 : BaseNestedUnion = .BaseUnionX(42)
    var bt1 : BaseNestedTypealias
    var bs2 = self.BaseNestedStruct()
    var bc2 = self.BaseNestedClass()
    var bo2 = self.BaseUnionX(24) // expected-error {{type 'ThisDerived1' has no member 'BaseUnionX'}}
    var bo3 = self.BaseNestedUnion.BaseUnionX(24)
    var bt2 = self.BaseNestedTypealias()

    self.derivedInstanceVar = 42 // expected-error {{member 'derivedInstanceVar' cannot be used on type 'ThisDerived1'}}
    self.derivedProp = 42 // expected-error {{member 'derivedProp' cannot be used on type 'ThisDerived1'}}
    self.derivedFunc0() // expected-error {{missing argument}}
    self.derivedFunc0(ThisBase1())() // expected-error {{cannot convert value of type 'ThisBase1' to expected argument type 'ThisDerived1'}}
    self.derivedFunc0(ThisDerived1())()
    self.derivedStaticVar = 42
    self.derivedStaticProp = 42
    self.derivedStaticFunc0()

    self.derivedExtProp = 42 // expected-error {{member 'derivedExtProp' cannot be used on type 'ThisDerived1'}}
    self.derivedExtFunc0() // expected-error {{missing argument}}
    self.derivedExtStaticVar = 42
    self.derivedExtStaticProp = 42 // expected-error {{member 'derivedExtStaticProp' cannot be used on type 'ThisDerived1'}}
    self.derivedExtStaticFunc0()

    var ds1 : DerivedNestedStruct
    var dc1 : DerivedNestedClass
    var do1 : DerivedNestedUnion = .DerivedUnionX(42)
    var dt1 : DerivedNestedTypealias
    var ds2 = self.DerivedNestedStruct()
    var dc2 = self.DerivedNestedClass()
    var do2 = self.DerivedUnionX(24) // expected-error {{type 'ThisDerived1' has no member 'DerivedUnionX'}}
    var do3 = self.DerivedNestedUnion.DerivedUnionX(24)
    var dt2 = self.DerivedNestedTypealias()

    var des1 : DerivedExtNestedStruct
    var dec1 : DerivedExtNestedClass
    var deo1 : DerivedExtNestedUnion = .DerivedExtUnionX(42)
    var det1 : DerivedExtNestedTypealias
    var des2 = self.DerivedExtNestedStruct()
    var dec2 = self.DerivedExtNestedClass()
    var deo2 = self.DerivedExtUnionX(24) // expected-error {{type 'ThisDerived1' has no member 'DerivedExtUnionX'}}
    var deo3 = self.DerivedExtNestedUnion.DerivedExtUnionX(24)
    var det2 = self.DerivedExtNestedTypealias()

    self.Type // expected-error {{type 'ThisDerived1' has no member 'Type'}}
  }

  class func staticTestSuper1() {
    super.baseInstanceVar = 42 // expected-error {{member 'baseInstanceVar' cannot be used on type 'ThisBase1'}}
    super.baseProp = 42 // expected-error {{member 'baseProp' cannot be used on type 'ThisBase1'}}
    super.baseFunc0() // expected-error {{missing argument}}
    super.baseFunc0(ThisBase1())()
    super.baseFunc1(42) // expected-error {{cannot convert value of type 'Int' to expected argument type 'ThisBase1'}}
    super.baseFunc1(ThisBase1())(42)
    super[0] = 42.0 // expected-error {{instance member 'subscript' cannot be used on type 'ThisBase1'}}
    super.baseStaticVar = 42
    super.baseStaticProp = 42
    super.baseStaticFunc0()

    super.baseExtProp = 42 // expected-error {{member 'baseExtProp' cannot be used on type 'ThisBase1'}}
    super.baseExtFunc0() // expected-error {{missing argument}}
    super.baseExtStaticVar = 42 
    super.baseExtStaticProp = 42 // expected-error {{member 'baseExtStaticProp' cannot be used on type 'ThisBase1'}}
    super.baseExtStaticFunc0()

    var bs2 = super.BaseNestedStruct()
    var bc2 = super.BaseNestedClass()
    var bo2 = super.BaseUnionX(24) // expected-error {{type 'ThisBase1' has no member 'BaseUnionX'}}
    var bo3 = super.BaseNestedUnion.BaseUnionX(24)
    var bt2 = super.BaseNestedTypealias()

    super.derivedInstanceVar = 42 // expected-error {{type 'ThisBase1' has no member 'derivedInstanceVar'}}
    super.derivedProp = 42 // expected-error {{type 'ThisBase1' has no member 'derivedProp'}}
    super.derivedFunc0() // expected-error {{type 'ThisBase1' has no member 'derivedFunc0'}}
    super.derivedStaticVar = 42 // expected-error {{type 'ThisBase1' has no member 'derivedStaticVar'}}
    super.derivedStaticProp = 42 // expected-error {{type 'ThisBase1' has no member 'derivedStaticProp'}}
    super.derivedStaticFunc0() // expected-error {{type 'ThisBase1' has no member 'derivedStaticFunc0'}}

    super.derivedExtProp = 42 // expected-error {{type 'ThisBase1' has no member 'derivedExtProp'}}
    super.derivedExtFunc0() // expected-error {{type 'ThisBase1' has no member 'derivedExtFunc0'}}
    super.derivedExtStaticVar = 42 // expected-error {{type 'ThisBase1' has no member 'derivedExtStaticVar'}}
    super.derivedExtStaticProp = 42 // expected-error {{type 'ThisBase1' has no member 'derivedExtStaticProp'}}
    super.derivedExtStaticFunc0() // expected-error {{type 'ThisBase1' has no member 'derivedExtStaticFunc0'}}

    var ds2 = super.DerivedNestedStruct() // expected-error {{type 'ThisBase1' has no member 'DerivedNestedStruct'}}
    var dc2 = super.DerivedNestedClass() // expected-error {{type 'ThisBase1' has no member 'DerivedNestedClass'}}
    var do2 = super.DerivedUnionX(24) // expected-error {{type 'ThisBase1' has no member 'DerivedUnionX'}}
    var do3 = super.DerivedNestedUnion.DerivedUnionX(24) // expected-error {{type 'ThisBase1' has no member 'DerivedNestedUnion'}}
    var dt2 = super.DerivedNestedTypealias(42) // expected-error {{type 'ThisBase1' has no member 'DerivedNestedTypealias'}}

    var des2 = super.DerivedExtNestedStruct() // expected-error {{type 'ThisBase1' has no member 'DerivedExtNestedStruct'}}
    var dec2 = super.DerivedExtNestedClass() // expected-error {{type 'ThisBase1' has no member 'DerivedExtNestedClass'}}
    var deo2 = super.DerivedExtUnionX(24) // expected-error {{type 'ThisBase1' has no member 'DerivedExtUnionX'}}
    var deo3 = super.DerivedExtNestedUnion.DerivedExtUnionX(24) // expected-error {{type 'ThisBase1' has no member 'DerivedExtNestedUnion'}}
    var det2 = super.DerivedExtNestedTypealias(42) // expected-error {{type 'ThisBase1' has no member 'DerivedExtNestedTypealias'}}

    super.Type // expected-error {{type 'ThisBase1' has no member 'Type'}}
  }
}

extension ThisBase1 {
  var baseExtProp : Int {
    get {
      return 42
    }
    set {}
  }

  func baseExtFunc0() {} // expected-note 2 {{'baseExtFunc0()' declared here}}

  var baseExtStaticVar: Int // expected-error {{extensions may not contain stored properties}} // expected-note 2 {{did you mean 'baseExtStaticVar'?}}

  var baseExtStaticProp: Int { // expected-note 2 {{did you mean 'baseExtStaticProp'?}}
    get {
      return 42
    }
    set {}
  }

  class func baseExtStaticFunc0() {} // expected-note {{did you mean 'baseExtStaticFunc0'?}}

  struct BaseExtNestedStruct {} // expected-note 2 {{did you mean 'BaseExtNestedStruct'?}}
  class BaseExtNestedClass { // expected-note {{did you mean 'BaseExtNestedClass'?}}
    init() { }
  }
  enum BaseExtNestedUnion { // expected-note {{did you mean 'BaseExtNestedUnion'?}}
    case BaseExtUnionX(Int)
  }

  typealias BaseExtNestedTypealias = Int // expected-note 2 {{did you mean 'BaseExtNestedTypealias'?}}
}

extension ThisDerived1 {
  var derivedExtProp : Int {
    get {
      return 42
    }
    set {}
  }

  func derivedExtFunc0() {} // expected-note {{'derivedExtFunc0()' declared here}}

  var derivedExtStaticVar: Int // expected-error {{extensions may not contain stored properties}}

  var derivedExtStaticProp: Int {
    get {
      return 42
    }
    set {}
  }

  class func derivedExtStaticFunc0() {}

  struct DerivedExtNestedStruct {}
  class DerivedExtNestedClass {
    init() { }
  }
  enum DerivedExtNestedUnion { // expected-note {{did you mean 'DerivedExtNestedUnion'?}}
    case DerivedExtUnionX(Int)
  }

  typealias DerivedExtNestedTypealias = Int
}

// <rdar://problem/11554141>
func shadowbug() {
  var Foo = 10
  func g() {
    struct S {
      var x : Foo
      typealias Foo = Int
    }
  }
}
func scopebug() {
  let Foo = 10
  struct S {
    typealias Foo = Int
  }
  _ = Foo
}
struct Ordering {
  var x : Foo
  typealias Foo = Int
}

// <rdar://problem/12202655>
class Outer {
  class Inner {}
  class MoreInner : Inner {}
}

func makeGenericStruct<S>(_ x: S) -> GenericStruct<S> {
  return GenericStruct<S>()
}
struct GenericStruct<T> {}


// <rdar://problem/13952064>
extension Outer {
  class ExtInner {}
}

// <rdar://problem/14149537>
func useProto<R : MyProto>(_ value: R) -> R.Element {
  return value.get()
}

protocol MyProto {
  associatedtype Element
  func get() -> Element
}


// <rdar://problem/14488311>
struct DefaultArgumentFromExtension {
  func g(_ x: @escaping (DefaultArgumentFromExtension) -> () -> () = f) {
    let f = 42
    var x2 = x
    x2 = f // expected-error{{cannot assign value of type 'Int' to type '(DefaultArgumentFromExtension) -> () -> ()'}}
    _ = x2
  }
  var x : (DefaultArgumentFromExtension) -> () -> () = f
}
extension DefaultArgumentFromExtension {
  func f() {}
}

struct MyStruct {
  var state : Bool
  init() { state = true }
  mutating func mod() {state = false}
  // expected-note @+1 {{mark method 'mutating' to make 'self' mutable}} {{3-3=mutating }}
  func foo() { mod() } // expected-error {{cannot use mutating member on immutable value: 'self' is immutable}}
}


// <rdar://problem/19935319> QoI: poor diagnostic initializing a variable with a non-class func
class Test19935319 {
  let i = getFoo()  // expected-error {{cannot use instance member 'getFoo' within property initializer; property initializers run before 'self' is available}}
  
  func getFoo() -> Int {}
}

// <rdar://problem/27013358> Crash using instance member as default parameter
class rdar27013358 {
  let defaultValue = 1
  func returnTwo() -> Int {
    return 2
  }
  init(defaulted value: Int = defaultValue) {} // expected-error {{cannot use instance member 'defaultValue' as a default parameter}}
  init(another value: Int = returnTwo()) {} // expected-error {{cannot use instance member 'returnTwo' as a default parameter}}
}

// <rdar://problem/23904262> QoI: ivar default initializer cannot reference other default initialized ivars?
class r23904262 {
  let x = 1
  let y = x // expected-error {{cannot use instance member 'x' within property initializer; property initializers run before 'self' is available}}
}


// <rdar://problem/21677702> Static method reference in static var doesn't work
class r21677702 {
  static func method(value: Int) -> Int { return value }
  static let x = method(value: 123)
  static let y = method(123) // expected-error {{missing argument label 'value:' in call}}
}


// <rdar://problem/16954496> lazy properties must use "self." in their body, and can weirdly refer to class variables directly
class r16954496 {
  func bar() {}
  lazy var x: Array<(r16954496) -> () -> Void> = [bar]
}



// <rdar://problem/27413116> [Swift] Using static constant defined in enum when in switch statement doesnt compile
enum MyEnum {
  case one
  case two
  case oneTwoThree

  static let kMyConstant = "myConstant"
}

switch "someString" {
case MyEnum.kMyConstant: // this causes a compiler error
  print("yay")
case MyEnum.self.kMyConstant: // this works fine
  print("hmm")
default:
  break
}

func foo() {
  _ = MyEnum.One // expected-error {{enum type 'MyEnum' has no case 'One'; did you mean 'one'}}{{14-17=one}}
  _ = MyEnum.Two // expected-error {{enum type 'MyEnum' has no case 'Two'; did you mean 'two'}}{{14-17=two}}
  _ = MyEnum.OneTwoThree // expected-error {{enum type 'MyEnum' has no case 'OneTwoThree'; did you mean 'oneTwoThree'}}{{14-25=oneTwoThree}}
}

enum MyGenericEnum<T> {
  case one(T)
  case oneTwo(T)
}

func foo1() {
  _ = MyGenericEnum<Int>.One // expected-error {{enum type 'MyGenericEnum<Int>' has no case 'One'; did you mean 'one'}}{{26-29=one}}
  _ = MyGenericEnum<Int>.OneTwo // expected-error {{enum type 'MyGenericEnum<Int>' has no case 'OneTwo'; did you mean 'oneTwo'}}{{26-32=oneTwo}}
}
