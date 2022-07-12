#if MODULE
public dynamic var public_global_var = "public_global_var"

public dynamic func public_global_func() -> String {
  return "public_global_func"
}

public dynamic func public_global_generic_func<T>(_ t: T.Type) -> String {
  return "public_global_generic_func"
}

public class PublicClass {
  public var str : String = ""
  public init() {}
  public dynamic init(x: Int) { str = "public_class_init" }

  public dynamic func function() -> String {
    return "public_class_func"
  }
  public dynamic final func finalFunction() -> String {
    return "public_class_final_func"
  }
  public dynamic func genericFunction<T>(_ t: T.Type) -> String {
    return "public_class_generic_func"
  }
}

public struct PublicStruct {
  public var str = ""
  public init() {}

  public dynamic init(x: Int) { str = "public_struct_init" }

  public dynamic func function() -> String {
    return "public_struct_func"
  }
  public dynamic func genericFunction<T>(_ t: T.Type) -> String {
    return "public_struct_generic_func"
  }
  public dynamic var public_stored_property : String = "public_stored_property"

  public dynamic subscript(_ x: Int) -> String {
    get {
      return "public_subscript_get"
    }
    set {
      str = newValue
    }
  }
  public dynamic subscript(y x: Int) -> String {
    _read {
      yield "public_subscript_get_modify_read"
    }
    _modify {
      yield &str
    }
  }
}

public enum PublicEnumeration<Q> {
  case A
  case B

  public dynamic func function() -> String {
    return "public_enum_func"
  }
  public dynamic func genericFunction<T>(_ t: T.Type) -> String {
    return "public_enum_generic_func"
  }
}
#elseif MODULENODYNAMIC
public dynamic var public_global_var = "public_global_var"

public func public_global_func() -> String {
  return "public_global_func"
}

public func public_global_generic_func<T>(_ t: T.Type) -> String {
  return "public_global_generic_func"
}

public class PublicClass {
  public var str : String = ""
  public init() {}
  public init(x: Int) { str = "public_class_init" }

  public func function() -> String {
    return "public_class_func"
  }

  public final func finalFunction() -> String {
    return "public_class_final_func"
  }

  public func genericFunction<T>(_ t: T.Type) -> String {
    return "public_class_generic_func"
  }
}

public struct PublicStruct {
  public var str = ""
  public init() {}

  public init(x: Int) { str = "public_struct_init" }

  public func function() -> String {
    return "public_struct_func"
  }
  public func genericFunction<T>(_ t: T.Type) -> String {
    return "public_struct_generic_func"
  }
  dynamic public var public_stored_property : String = "public_stored_property"

  public subscript(_ x: Int) -> String {
    get {
      return "public_subscript_get"
    }
    set {
      str = newValue
    }
  }
  public subscript(y x: Int) -> String {
    _read {
      yield "public_subscript_get_modify_read"
    }
    _modify {
      yield &str
    }
  }
}

public enum PublicEnumeration<Q> {
  case A
  case B

  public func function() -> String {
    return "public_enum_func"
  }
  public func genericFunction<T>(_ t: T.Type) -> String {
    return "public_enum_generic_func"
  }
}
#elseif MODULE2

import Module1

/// Public global functions, struct, class, and enum.

@_dynamicReplacement(for: public_global_var)
public var replacement_for_public_global_var : String {
  return "replacement of public_global_var"
}

@_dynamicReplacement(for: public_global_func())
public func replacement_for_public_global_func() -> String {
  return "replacement of " + public_global_func()
}

@_dynamicReplacement(for: public_global_generic_func(_:))
public func replacement_for_public_global_generic_func<T>(_ t: T.Type) -> String {
  return "replacement of " + public_global_generic_func(t)
}

extension PublicClass {

  // Designated initializers of resilient classes cannot be
  // dynamically replaced.
#if !EVOLUTION
  @_dynamicReplacement(for: init(x:))
  public init(y: Int) {
    str = "replacement of public_class_init"
  }
#endif

  @_dynamicReplacement(for: function())
  public func replacement_function() -> String {
    return "replacement of " + function()
  }
  @_dynamicReplacement(for: finalFunction())
  public func replacement_finalFunction() -> String {
    return "replacement of " + finalFunction()
  }
  @_dynamicReplacement(for: genericFunction(_:))
  public func replacement_genericFunction<T>(_ t: T.Type) -> String {
    return "replacement of " + genericFunction(t)
  }
}

extension PublicStruct {
  @_dynamicReplacement(for: init(x:))
  public init(y: Int) {
    self.init(x: y)
    str = "replacement of public_struct_init"
  }

  @_dynamicReplacement(for: function())
  public func replacement_function() -> String {
    return "replacement of " + function()
  }
  @_dynamicReplacement(for: genericFunction(_:))
  public func replacement_genericFunction<T>(_ t: T.Type) -> String {
    return "replacement of " + genericFunction(t)
  }
  @_dynamicReplacement(for: public_stored_property)
  var replacement_public_stored_property : String {
    return "replacement of " + public_stored_property
  }
  @_dynamicReplacement(for: subscript(_:))
  subscript(x x: Int) -> String {
    get {
      return "replacement of " + self[x]
    }
    set {
      str = "replacement of " + newValue
    }
  }

  @_dynamicReplacement(for: subscript(y:))
  public subscript(z x: Int) -> String {
    _read {
      yield "replacement of " + self[y: x]
    }
    _modify {
      yield &str
      str = "replacement of " + str
    }
  }
}

extension PublicEnumeration {
  @_dynamicReplacement(for: function())
  public func replacement_function() -> String {
    return "replacement of " + function()
  }
  @_dynamicReplacement(for: genericFunction(_:))
  public func replacement_genericFunction<T>(_ t: T.Type) -> String {
    return "replacement of " + genericFunction(t)
  }
}
#endif
