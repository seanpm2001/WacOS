@_exported import autolinking_public
import autolinking_other
import autolinking_indirect
import autolinking_private
@_implementationOnly import autolinking_implementation_only

public func bfunc(x: Int = afunc(), y: Int = afunc2()) {
  cfunc()
  dfunc()
}
