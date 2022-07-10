// RUN: %target-parse-verify-swift

let f1: (Int) -> Int = { $0 }
let f2: @convention(swift) (Int) -> Int = { $0 }
let f3: @convention(block) (Int) -> Int = { $0 }
let f4: @convention(c) (Int) -> Int = { $0 }

let f5: @convention(INTERCAL) (Int) -> Int = { $0 } // expected-error{{convention 'INTERCAL' not supported}}

