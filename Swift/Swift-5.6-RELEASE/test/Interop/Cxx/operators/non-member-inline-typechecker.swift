// RUN: %target-typecheck-verify-swift -I %S/Inputs -enable-cxx-interop

import NonMemberInline

let lhs = LoadableIntWrapper(value: 42)
let rhs = LoadableIntWrapper(value: 23)

let resultPlus = lhs + rhs
let resultMinus = lhs - rhs
let resultStar = lhs * rhs
let resultSlash = lhs / rhs
let resultPercent = lhs % rhs
let resultCaret = lhs ^ rhs
let resultAmp = lhs & rhs
let resultPipe = lhs | rhs
let resultLessLess = lhs << rhs
let resultGreaterGreater = lhs >> rhs
let resultLess = lhs < rhs
let resultGreater = lhs > rhs
let resultEqualEqual = lhs == rhs
let resultExclaimEqual = lhs != rhs
let resultLessEqual = lhs <= rhs
let resultGreaterEqual = lhs >= rhs

var lhsBool = LoadableBoolWrapper(value: true)
var rhsBool = LoadableBoolWrapper(value: false)

let resultAmpAmp = lhsBool && rhsBool
let resultPipePipe = lhsBool && rhsBool
