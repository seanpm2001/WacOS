
public func getVersion() -> Int {
#if BEFORE
  return 0
#else
  return 1
#endif
}


public protocol ElementProtocol : Equatable {
  func increment() -> Self
}

public protocol AddMethodsProtocol {
  associatedtype Element : ElementProtocol

  func importantOperation() -> Element
  func unimportantOperation() -> Element

#if AFTER
  func uselessOperation() -> Element
#endif
}

extension AddMethodsProtocol {
  public func unimportantOperation() -> Element {
    return importantOperation().increment()
  }

#if AFTER
  public func uselessOperation() -> Element {
    return unimportantOperation().increment()
  }
#endif
}

public func doSomething<T : AddMethodsProtocol>(_ t: T) -> [T.Element] {
#if BEFORE
  return [
      t.importantOperation(),
      t.unimportantOperation(),
      t.unimportantOperation().increment(),
  ]
#else
  return [
      t.importantOperation(),
      t.unimportantOperation(),
      t.uselessOperation(),
  ]
#endif
}


public protocol AddConstructorsProtocol {
  init(name: String)

#if AFTER
  init?(nickname: String)
#endif
}

extension AddConstructorsProtocol {
  public init?(nickname: String) {
    if nickname == "" {
      return nil
    }
    self.init(name: nickname + "ster")
  }
}

public func testConstructorProtocol<T : AddConstructorsProtocol>(_ t: T.Type) -> [T?] {
#if BEFORE
  return [t.init(name: "Puff")]
#else
  return [t.init(name: "Meow meow"),
          t.init(nickname: ""),
          t.init(nickname: "Robster the Lob")]
#endif
}


public protocol AddPropertiesProtocol {
  var topSpeed: Int { get nonmutating set }
  var maxRPM: Int { get set }

#if AFTER
  var maxSafeSpeed: Int { get set }
  var minSafeSpeed: Int { get nonmutating set }
  var redLine: Int { mutating get set }
#endif
}

extension AddPropertiesProtocol {
#if AFTER
  public var maxSafeSpeed: Int {
    get {
      return topSpeed / 2
    }
    set {
      topSpeed = newValue * 2
    }
  }

  public var minSafeSpeed: Int {
    get {
      return topSpeed / 4
    }
    nonmutating set {
      topSpeed = newValue * 4
    }
  }

  public var redLine: Int {
    get {
      return maxRPM - 2000
    }
    set {
      maxRPM = newValue + 2000
    }
  }
#endif
}

public func getProperties<T : AddPropertiesProtocol>(_ t: inout T) -> [Int] {
#if BEFORE
  return [t.topSpeed, t.maxRPM]
#else
  return [t.topSpeed, t.maxRPM, t.maxSafeSpeed, t.minSafeSpeed, t.redLine]
#endif
}

func increment(_ x: inout Int, by: Int) {
  x += by
}

public func setProperties<T : AddPropertiesProtocol>(_ t: inout T) {
#if AFTER
  t.minSafeSpeed = t.maxSafeSpeed
  increment(&t.redLine, by: 7000)
#else
  increment(&t.topSpeed, by: t.topSpeed)
  increment(&t.maxRPM, by: 7000)
#endif
}


public protocol AddSubscriptProtocol {
  associatedtype Key
  associatedtype Value

  func get(key key: Key) -> Value
  mutating func set(key key: Key, value: Value)

#if AFTER
  subscript(key: Key) -> Value { get set }
#endif
}

extension AddSubscriptProtocol {
  public subscript(key: Key) -> Value {
    get {
      return get(key: key)
    }
    set {
      set(key: key, value: newValue)
    }
  }
}

public func doSomething<T : AddSubscriptProtocol>(_ t: inout T, k1: T.Key, k2: T.Key) {
#if BEFORE
  t.set(key: k1, value: t.get(key: k2))
#else
  t[k1] = t[k2]
#endif
}
