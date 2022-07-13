// RUN: %empty-directory(%t)

// RUN: mkdir -p %t/onone %t/wmo
// RUN: %target-build-swift -emit-module -emit-module-path %t/onone/library.swiftmodule -module-name=library -emit-library %S/Inputs/library.swift -o %t/onone/library.%target-dylib-extension
// RUN: %target-build-swift %S/main.swift %t/onone/library.%target-dylib-extension -I %t/onone/ -o %t/onone/main

// RUN: %target-build-swift -emit-module -emit-module-path %t/wmo/library.swiftmodule -module-name=library -emit-library -O -wmo %S/Inputs/library.swift -o %t/wmo/library.%target-dylib-extension
// RUN: %target-build-swift %S/main.swift %t/wmo/library.%target-dylib-extension -I %t/wmo/ -o %t/wmo/main

import library

protocol Takeaway {
  var costPounds: Float { get set }
  var costEuros: Float { get set }
  var costDollars: Float { get set }
}

extension FishAndChips: Takeaway {}

protocol Beverage {
  var abv: Int { get set }
}

extension Beer : Beverage {}

protocol PurrExtractor {
  var purrs: Int { get set }
}

extension LazyCat : PurrExtractor {}

// Dummy statement
_ = ()

public func launchToday(fc: FinalCountdown) {
  // Check if the setter is not transparent and therefore does not try to
  // reference the hidden offet variable symbol in the module.
  fc.count = 27
}

