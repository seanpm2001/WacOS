// RUN: %swift-ide-test -structure -source-filename %s | %FileCheck %s

// CHECK: <class>class <name>MyCls</name> : <inherited><elem-typeref>OtherClass</elem-typeref></inherited> {
// CHECK:   <property>var <name>bar</name> : <type>Int</type></property>
// CHECK:   <property>var <name>anotherBar</name> : <type>Int</type> = 42</property>
// CHECK:   <cvar>class var <name>cbar</name> : <type>Int</type> = 0</cvar>
class MyCls : OtherClass {
  var bar : Int
  var anotherBar : Int = 42
  class var cbar : Int = 0

  // CHECK:   <ifunc>func <name>foo(<param>_ arg1: <type>Int</type></param>, <param><name>name</name>: <type>String</type></param>, <param><name>param</name> par: <type>String</type></param>)</name> {
  // CHECK:     <lvar>var <name>abc</name></lvar>
  // CHECK:     <if>if <elem-condexpr>1</elem-condexpr> <brace>{
  // CHECK:       <call><name>foo</name>(<arg>1</arg>, <arg><name>name</name>:"test"</arg>, <arg><name>param</name>:"test2"</arg>)</call>
  // CHECK:     }</brace>
  // CHECK:   }</ifunc>
  func foo(_ arg1: Int, name: String, param par: String) {
    var abc
    if 1 {
      foo(1, name:"test", param:"test2")
    }
  }

  // CHECK:   <ifunc><name>init (<param><name>x</name>: <type>Int</type></param>)</name></ifunc>
  init (x: Int)

  // CHECK:   <cfunc>class func <name>cfoo()</name></cfunc>
  class func cfoo()

// CHECK: }</class>
}

// CHECK: <struct>struct <name>MyStruc</name> {
// CHECK:   <property>var <name>myVar</name>: <type>Int</type></property>
// CHECK:   <svar>static var <name>sbar</name> : <type>Int</type> = 0</svar>
// CHECK:   <sfunc>static func <name>cfoo()</name></sfunc>
// CHECK: }</struct>
struct MyStruc {
  var myVar: Int
  static var sbar : Int = 0
  static func cfoo()
}

// CHECK: <protocol>protocol <name>MyProt</name> {
// CHECK:   <ifunc>func <name>foo()</name></ifunc>
// CHECK: }</protocol>
protocol MyProt {
  func foo()
}

// CHECK: <extension>extension <name>MyStruc</name> {
// CHECK:   <ifunc>func <name>foo()</name> {
// CHECK:   }</ifunc>
// CHECK: }</extension>
extension MyStruc {
  func foo() {
  }
}

// CHECK: <gvar>var <name>gvar</name> : <type>Int</type> = 0</gvar>
var gvar : Int = 0

// CHECK: <ffunc>func <name>ffoo()</name> {}</ffunc>
func ffoo() {}

// CHECK: <foreach>for <elem-id><lvar><name>i</name></lvar></elem-id> in <elem-expr>0...5</elem-expr> <brace>{}</brace></foreach>
for i in 0...5 {}
// CHECK: <foreach>for <elem-id>var (<lvar><name>i</name></lvar>, <lvar><name>j</name></lvar>)</elem-id> in <elem-expr>array</elem-expr> <brace>{}</brace></foreach>
for var (i, j) in array {}

// CHECK: <while>while <elem-condexpr>var <lvar><name>v</name></lvar> = o, <lvar><name>z</name></lvar> = o where v > z</elem-condexpr> <brace>{}</brace></while>
while var v = o, z = o where v > z {}
// CHECK: <while>while <elem-condexpr>v == 0</elem-condexpr> <brace>{}</brace></while>
while v == 0 {}
// CHECK: <repeat-while>repeat <brace>{}</brace> while <elem-expr>v == 0</elem-expr></repeat-while>
repeat {} while v == 0
// CHECK: <if>if <elem-condexpr>var <lvar><name>v</name></lvar> = o, <lvar><name>z</name></lvar> = o where v > z</elem-condexpr> <brace>{}</brace></if>
if var v = o, z = o where v > z {}

// CHECK: <switch>switch <elem-expr>v</elem-expr> {
// CHECK:   <case>case <elem-pattern>1</elem-pattern>: break;</case>
// CHECK:   <case>case <elem-pattern>2</elem-pattern>, <elem-pattern>3</elem-pattern>: break;</case>
// CHECK:   <case>case <elem-pattern><call><name>Foo</name>(<arg>var <lvar><name>x</name></lvar></arg>, <arg>var <lvar><name>y</name></lvar></arg>)</call> where x < y</elem-pattern>: break;</case>
// CHECK:   <case>case <elem-pattern>2 where <call><name>foo</name>()</call></elem-pattern>, <elem-pattern>3 where <call><name>bar</name>()</call></elem-pattern>: break;</case>
// CHECK:   <case><elem-pattern>default</elem-pattern>: break;</case>
// CHECK: }</switch>
switch v {
  case 1: break;
  case 2, 3: break;
  case Foo(var x, var y) where x < y: break;
  case 2 where foo(), 3 where bar(): break;
  default: break;
}

// CHECK: <gvar>let <name>myArray</name> = <array>[<elem-expr>1</elem-expr>, <elem-expr>2</elem-expr>, <elem-expr>3</elem-expr>]</array></gvar>
let myArray = [1, 2, 3]
// CHECK: <gvar>let <name>myDict</name> = <dictionary>[<elem-expr>1</elem-expr>:<elem-expr>1</elem-expr>, <elem-expr>2</elem-expr>:<elem-expr>2</elem-expr>, <elem-expr>3</elem-expr>:<elem-expr>3</elem-expr>]</dictionary></gvar>
let myDict = [1:1, 2:2, 3:3]
// CHECK: <gvar>let <name>myArray2</name> = <array>[<elem-expr>1</elem-expr>]</array></gvar>
let myArray2 = [1]
// CHECK: <gvar>let <name>myDict2</name> = <dictionary>[<elem-expr>1</elem-expr>:<elem-expr>1</elem-expr>]</dictionary></gvar>
let myDict2 = [1:1]

// CHECK: <foreach>for <brace>{}</brace></foreach>
for {}

// CHECK: <class>class <name><#MyCls#></name> : <inherited><elem-typeref><#OtherClass#></elem-typeref></inherited> {}
class <#MyCls#> : <#OtherClass#> {}

// CHECK: <ffunc>func <name><#test1#> ()</name> {
// CHECK:   <foreach>for <elem-id><lvar><name><#name#></name></lvar></elem-id> in <elem-expr><#items#></elem-expr> <brace>{}</brace></foreach>
// CHECK: }</ffunc>
func <#test1#> () {
  for <#name#> in <#items#> {}
}

// CHECK: <gvar>let <name>myArray</name> = <array>[<elem-expr><#item1#></elem-expr>, <elem-expr><#item2#></elem-expr>]</array></gvar>
let myArray = [<#item1#>, <#item2#>]

// CHECK: <ffunc>func <name>test1()</name> {
// CHECK:   <call><name>dispatch_async</name>(<arg><call><name>dispatch_get_main_queue</name>()</call></arg>, <arg><brace>{}</brace></arg>)</call>
// CHECK:   <call><name>dispatch_async</name>(<arg><call><name>dispatch_get_main_queue</name>()</call></arg>) <arg><brace>{}</brace></arg></call>
// CHECK: }</ffunc>
func test1() {
  dispatch_async(dispatch_get_main_queue(), {})
  dispatch_async(dispatch_get_main_queue()) {}
}

// CHECK: <enum>enum <name>SomeEnum</name> {
// CHECK:   <enum-case>case <enum-elem><name>North</name></enum-elem></enum-case>
// CHECK:   <enum-case>case <enum-elem><name>South</name></enum-elem>, <enum-elem><name>East</name></enum-elem></enum-case>
// CHECK:   <enum-case>case <enum-elem><name>QRCode</name>(String)</enum-elem></enum-case>
// CHECK:   <enum-case>case</enum-case>
// CHECK: }</enum>
enum SomeEnum {
  case North
  case South, East
  case QRCode(String)
  case
}

// CHECK: <enum>enum <name>Rawness</name> : <inherited><elem-typeref>Int</elem-typeref></inherited> {
// CHECK:   <enum-case>case <enum-elem><name>One</name> = <elem-initexpr>1</elem-initexpr></enum-elem></enum-case>
// CHECK:   <enum-case>case <enum-elem><name>Two</name> = <elem-initexpr>2</elem-initexpr></enum-elem>, <enum-elem><name>Three</name> = <elem-initexpr>3</elem-initexpr></enum-elem></enum-case>
// CHECK: }</enum>
enum Rawness : Int {
  case One = 1
  case Two = 2, Three = 3
}

// CHECK: <ffunc>func <name>rethrowFunc(<param>_ f: <type>() throws -> ()</type></param>)</name> rethrows {}</ffunc>
func rethrowFunc(_ f: () throws -> ()) rethrows {}

class NestedPoundIf{
    func foo1() {
        #if os(OSX)
          var a = 1
            #if USE_METAL
              var b = 2
              #if os(iOS)
                var c = 3
              #else
                var c = 3
              #endif
            #else
              var b = 2
            #endif
           #else
            var a = 1
        #endif
    }
    func foo2() {}
    func foo3() {}
}
// CHECK: <ifunc>func <name>foo2()</name> {}</ifunc>
// CHECK: <ifunc>func <name>foo3()</name> {}</ifunc>

class A {
  func foo(_ i : Int, animations: () -> ()) {}
  func perform() {foo(5, animations: {})}
// CHECK:  <ifunc>func <name>perform()</name> {<call><name>foo</name>(<arg>5</arg>, <arg><name>animations</name>: <brace>{}</brace></arg>)</call>}</ifunc>
}

// CHECK: <typealias>typealias <name>OtherA</name> = A</typealias>
typealias OtherA = A

class SubscriptTest {
  subscript(index: Int) -> Int {
    return 0
  }
  // CHECK: <subscript><name>subscript(<param>index: <type>Int</type></param>)</name> -> <type>Int</type> {
  // CHECK:  return 0
  // CHECK: }</subscript>

  subscript(string: String) -> Int {
    get {
      return 0
    }
    set(value) {
      print(value)
    }
  }
  // CHECK: <subscript><name>subscript(<param>string: <type>String</type></param>)</name> -> <type>Int</type> {
  // CHECK: get {
  // CHECK:   return 0
  // CHECK: }
  // CHECK: set(<param>value</param>) {
  // CHECK:   <call><name>print</name>(value)</call>
  // CHECK: }</subscript>
}

class ReturnType {
  func foo() -> Int { return 0 }
  // CHECK:  <ifunc>func <name>foo()</name> -> <type>Int</type> {
  // CHECK:  return 0
  // CHECK: }</ifunc>
  
  func foo2<T>() -> T {}
  // CHECK:  <ifunc>func <name>foo2<T>()</name> -> <type>T</type> {}</ifunc>
  
  func foo3() -> () -> Int {}
  // CHECK:  <ifunc>func <name>foo3()</name> -> <type>() -> Int</type> {}</ifunc>
}

protocol FooProtocol {
  associatedtype Bar
  // CHECK:  <associatedtype>associatedtype <name>Bar</name></associatedtype>
  associatedtype Baz: Equatable
  // CHECK:  <associatedtype>associatedtype <name>Baz</name>: Equatable</associatedtype>
}

a.b(c: d?.e?.f, g: h)
// CHECK: <call><name>a.b</name>(<arg><name>c</name>: d?.e?.f</arg>, <arg><name>g</name>: h</arg>)</call>

struct Tuples {
  var foo: (Int, String) {
    return (1, "test")
    // CHECK: <tuple>(<elem-expr>1</elem-expr>, <elem-expr>"test"</elem-expr>)</tuple>
  }
  
  func foo2() {
    foo3(x: (1, 20))
    // CHECK: <call><name>foo3</name>(<arg><name>x</name>: <tuple>(<elem-expr>1</elem-expr>, <elem-expr>20</elem-expr>)</tuple></arg>)</call>
    let y = (x, foo4(a: 0))
    // CHECK: <lvar>let <name>y</name> = <tuple>(<elem-expr>x</elem-expr>, <elem-expr><call><name>foo4</name>(<arg><name>a</name>: 0</arg>)</call></elem-expr>)</tuple></lvar>
    
    let z = (name1: 1, name2: 2)
    // CHECK: <lvar>let <name>z</name> = <tuple>(name1: <elem-expr>1</elem-expr>, name2: <elem-expr>2</elem-expr>)</tuple></lvar>
  }
}

