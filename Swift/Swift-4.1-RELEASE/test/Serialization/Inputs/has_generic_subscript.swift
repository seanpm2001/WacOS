public struct GenericSubscript {
  public init() { }

  public subscript<K, V>(k: K) -> V {
    get {
      while true { }
    }
    set { }
  }
}

public struct Outer<T> {
  public struct Inner<U> {
    public init() {}
  }
}

extension Outer.Inner {
  public subscript<K, V>(k: K) -> V {
    get {
      while true { }
    }
    set { }
  }
}
