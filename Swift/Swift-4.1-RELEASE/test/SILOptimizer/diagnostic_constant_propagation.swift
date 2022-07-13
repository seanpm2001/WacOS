// FIXME(integer): with new integer protocols implemented the overflows are no
// longer caught: <rdar://problem/29937936>
// XFAIL: *

// RUN: %target-swift-frontend -emit-sil -primary-file %s -o /dev/null -verify

// REQUIRES: PTRSIZE=64

// FIXME: <rdar://problem/19508336> Extend test/SILOptimizer/diagnostic_constant_propagation.swift to 32-bit platforms

// These are tests for diagnostics produced by constant propagation pass.

func testArithmeticOverflow() {
  let xu8 : UInt8 = 250
  let yu8 : UInt8 = 250
  var _ /*zu8*/ = xu8 + yu8 // expected-error {{arithmetic operation '250 + 250' (on type 'UInt8') results in an overflow}}
  var _ /*xpyu8*/ : UInt8   = 250 + 250 // expected-error {{arithmetic operation '250 + 250' (on type 'UInt8') results in an overflow}}
  var _ /*xpyi8*/ : Int8    = 126 + 126 // expected-error {{arithmetic operation '126 + 126' (on type 'Int8') results in an overflow}}
  var _ /*xmyu16*/ : UInt16 = 65000 * 2 // expected-error {{arithmetic operation '65000 * 2' (on type 'UInt16') results in an overflow}}
  var _ /*xmyi16*/ : Int16  = 32000 * 2 // expected-error {{arithmetic operation '32000 * 2' (on type 'Int16') results in an overflow}}
  var _ /*xmyu32*/ : UInt32 = 4294967295 * 30 // expected-error {{arithmetic operation '4294967295 * 30' (on type 'UInt32') results in an overflow}}
  var _ /*xpyi32*/ : Int32 = 2147483647 + 30 // expected-error {{arithmetic operation '2147483647 + 30' (on type 'Int32') results in an overflow}}
  var _ /*xpyu64*/ : UInt64 = 9223372036854775807 * 30 // expected-error {{results in an overflow}}
  var _ /*xpyi64*/ : Int64 = 9223372036854775807 + 1  // expected-error {{results in an overflow}}

  var xu8_2 : UInt8 = 240
  xu8_2 += 40 // expected-error {{arithmetic operation '240 + 40' (on type 'UInt8') results in an overflow}}

  var _ : UInt8 = 230 - 240 // expected-error {{arithmetic operation '230 - 240' (on type 'UInt8') results in an overflow}}
  
  var xu8_3 : UInt8 = 240   // Global (cross block) analysis.
  for _ in 0..<10 {}
  xu8_3 += 40 // expected-error {{arithmetic operation '240 + 40' (on type 'UInt8') results in an overflow}}
  var _ : UInt8 = 240 + 5 + 15 // expected-error {{arithmetic operation '245 + 15' (on type 'UInt8') results in an overflow}}
  
  // TODO: We should remove the second init for Int8 - see rdar://problem/19224768
  _ = Int8(126) + Int8(1+1) // expected-error {{arithmetic operation '126 + 2' (on type 'Int8') results in an overflow}}
//  DISABLED FOR NOW
//  asserts in the shift operators confuse constant propagation
//  var csh1: Int8 = (1 << 7) - 1 // expected - error {{arithmetic operation '-128 - 1' (on type 'Int8') results in an overflow}}
//  var csh2: Int8 = (-1 & ~(1<<7))+1 // expected - error {{arithmetic operation '127 + 1' (on type 'Int8') results in an overflow}}
}

@_transparent 
func myaddSigned(_ x: Int8, _ y: Int8, _ z: Int8) -> Int8 {
  return x + y
}
@_transparent 
func myaddUnsigned(_ x: UInt8, _ y: UInt8, _ z: UInt8) -> UInt8 {
  return x + y
}

func testGenericArithmeticOverflowMessage() {
  _ = myaddSigned(125, 125, 125) // expected-error{{arithmetic operation '125 + 125' (on signed 8-bit integer type) results in an overflow}}
  _ = myaddUnsigned(250, 250, 250) // expected-error{{arithmetic operation '250 + 250' (on unsigned 8-bit integer type) results in an overflow}}
}

typealias MyInt = UInt8

func testConvertOverflow() {
  var _ /*int8_minus_two*/  : Int8 = (-2)
  var _ /*int8_plus_two*/   : Int8 = (2)
  var _ /*int16_minus_two*/ : Int16 = (-2)
  var _ /*int16_plus_two*/  : Int16 = (2)
  var _ /*int32_minus_two*/ : Int32 = (-2)
  var _ /*int32_plus_two*/  : Int32 = (2)
  var _ /*int64_minus_two*/ : Int64 = (-2)
  var _ /*int64_plus_two*/  : Int64 = (2)
  let int_minus_two   : Int = (-2)
  let int_plus_two    : Int = (2)
  var _ /*convert_minus_two*/ = Int8(int_minus_two)
  var _ /*convert_plus_two*/  = Int8(int_plus_two)

  var _ /*uint8_minus_two*/  : UInt8 = (-2) // expected-error {{negative integer '-2' overflows when stored into unsigned type 'UInt8'}}
  var _ /*uint8_plus_two*/   : UInt8 = (2)
  var _ /*uint16_minus_two*/ : UInt16 = (-2) // expected-error {{negative integer '-2' overflows when stored into unsigned type 'UInt16'}}
  var _ /*uint16_plus_two*/  : UInt16 = (2)
  var _ /*uint32_minus_two*/ : UInt32 = (-2) // expected-error {{negative integer '-2' overflows when stored into unsigned type 'UInt32'}}
  let uint32_plus_two  : UInt32 = (2)
  var _ /*uint64_minus_two*/ : UInt64 = (-2) // expected-error {{negative integer '-2' overflows when stored into unsigned type 'UInt64'}}
  var _ /*uint64_plus_two*/  : UInt64 = (2)
  var _ /*convert_s_to_u_minus_two*/ = UInt8(int_minus_two)  // expected-error {{integer overflows when converted from 'Int' to 'UInt8'}}
  var _ /*convert_s_to_u_plus_two*/  = UInt8(int_plus_two)
  var _ /*convert_u_to_s_plus_two*/  = Int8(uint32_plus_two)

  var _ /*int8_min*/     : Int8 = (-128)
  var _ /*int8_min_m1*/  : Int8 = (-129) // expected-error {{integer literal '-129' overflows when stored into 'Int8'}}
  var _ /*int16_min*/    : Int16 = (-32768)
  var _ /*int16_min_m1*/ : Int16 = (-32769) // expected-error {{integer literal '-32769' overflows when stored into 'Int16'}}
  var _ /*int32_min*/    : Int32 = (-2147483648)
  var _ /*int32_min_m1*/ : Int32 = (-2147483649) // expected-error {{integer literal '-2147483649' overflows when stored into 'Int32'}}
  var _ /*int64_min*/    : Int64 = (-9223372036854775808)
  var _ /*int64_min_m1*/ : Int64 = (-9223372036854775809) // expected-error {{integer literal '-9223372036854775809' overflows when stored into 'Int64'}}
  let int_minus_128 = -128
  var _ /*int8_min_conv*/     = Int8(int_minus_128)
  let int_minus_129 = -129
  var _ /*int8_min_m1_conv*/  = Int8(int_minus_129) // expected-error {{integer overflows when converted from 'Int' to 'Int8'}}

  var _ /*int8_max*/     : Int8 = (127)
  var _ /*int8_max_p1*/  : Int8 = (128) // expected-error {{integer literal '128' overflows when stored into 'Int8'}}
  let int16_max    : Int16 = (32767)
  var _ /*int16_max_p1*/ : Int16 = (32768) // expected-error {{integer literal '32768' overflows when stored into 'Int16'}}
  var _ /*int32_max*/    : Int32 = (2147483647)
  var _ /*int32_max_p1*/ : Int32 = (2147483648) // expected-error {{integer literal '2147483648' overflows when stored into 'Int32'}}
  var _ /*int64_max*/    : Int64 = (9223372036854775807)
  var _ /*int64_max_p1*/ : Int64 = (9223372036854775808) // expected-error {{integer literal '9223372036854775808' overflows when stored into 'Int64'}}
  var _ /*int16_max_conv*/    = Int16(UInt64(int16_max))
  let uint64_plus_32768 : UInt64 = 32768
  var _ /*int16_max_p1_conv*/ = Int16(uint64_plus_32768) // expected-error {{integer overflows when converted from 'UInt64' to 'Int16'}}
  
  var _ /*int8_max_pa*/      : Int8   = -13333; //expected-error{{integer literal '-13333' overflows when stored into 'Int8}}
  var _ /*int32_max_p_hex*/  : Int32  = 0xFFFF_FFFF; //expected-error{{integer literal '4294967295' overflows when stored into 'Int32'}}
  var _ /*uint32_max_hex*/   : UInt32 = 0xFFFF_FFFF
  var _ /*uint32_max_p_hex*/ : UInt32 = 0xFFFF_FFFF_F; //expected-error{{integer literal '68719476735' overflows when stored into 'UInt32'}}
  var _ /*uint0_typealias*/  : MyInt = 256; //expected-error{{integer literal '256' overflows when stored into 'MyInt'}}

  var _ /*uint8_min*/  : UInt8 = (0)
  let uint16_min : UInt16 = (0)
  var _ /*uint32_min*/ : UInt32 = (0)
  let uint64_min : UInt64 = (0)
  var _ /*uint8_min_conv_to_u*/ = UInt8(uint64_min)

  let int8_zero  : Int8 = (0)
  var _ /*int16_zero*/ : Int16 = (0)
  var _ /*int32_zero*/ : Int32 = (0)
  var _ /*int64_zero*/ : Int64 = (0)
  var _ /*int8_min_conv_to_s*/ = Int8(uint16_min)

  var _ /*uint8_max*/     : UInt8 = (255)
  var _ /*uint8_max_p1*/  : UInt8 = (256) // expected-error {{integer literal '256' overflows when stored into 'UInt8'}}
  var _ /*uint16_max*/    : UInt16 = (65535)
  var _ /*uint16_max_p1*/ : UInt16 = (65536) // expected-error {{integer literal '65536' overflows when stored into 'UInt16'}}
  var _ /*uint32_max*/    : UInt32 = (4294967295)
  var _ /*uint32_max_p1*/ : UInt32 = (4294967296) // expected-error {{integer literal '4294967296' overflows when stored into 'UInt32'}}
  var _ /*uint64_max*/    : UInt64 = (18446744073709551615)
  var _ /*uint64_max_p1*/ : UInt64 = (18446744073709551616) // expected-error {{integer literal '18446744073709551616' overflows when stored into 'UInt64'}}
  let uint16_255 : UInt16 = 255
  var _ /*uint8_max_conv*/    = UInt8(uint16_255)
  let uint16_256 : UInt16 = 256
  var _ /*uint8_max_p1_conv*/ = UInt8(uint16_256) // expected-error {{integer overflows when converted from 'UInt16' to 'UInt8'}}

  // Check same size int conversions.
  let int8_minus_1 : Int8 = -1
  let _ /*ssint8_neg*/    = UInt8(int8_minus_1) // expected-error {{negative integer cannot be converted to unsigned type 'UInt8'}}
  let uint8_128 : UInt8 = 128
  let _ /*ssint8_toobig*/ = Int8(uint8_128) // expected-error {{integer overflows when converted from 'UInt8' to 'Int8'}}
  let uint8_127 : UInt8 = 127
  let _ /*ssint8_good*/   = Int8(uint8_127)
  let int8_127 : Int8 = 127
  let _ /*ssint8_good2*/  = UInt8(int8_127)
  let _ /*ssint8_zero*/   = UInt8(int8_zero)
  let uint8_zero : UInt8 = 0
  let _ /*ssint8_zero2*/  = Int8(uint8_zero)
  
  // Check signed to unsigned extending size conversions.
  UInt16(Int8(-1)) // expected-error{{negative integer cannot be converted to unsigned type 'UInt16'}} // expected-warning{{unused}}
  UInt64(Int16(-200)) // expected-error{{negative integer cannot be converted to unsigned type 'UInt64'}} // expected-warning{{unused}}
  UInt64(Int32(-200)) // expected-error{{negative integer cannot be converted to unsigned type 'UInt64'}} // expected-warning{{unused}}
  Int16(Int8(-1)) // expected-warning{{unused}}
  Int64(Int16(-200)) // expected-warning{{unused}}
  Int64(Int32(-200)) // expected-warning{{unused}}
  Int64(UInt32(200)) // expected-warning{{unused}}
  UInt64(UInt32(200)) // expected-warning{{unused}}

  // IEEE binary32 max value = 2^128 * (2^23-1)/2^23
  var _ /*float32_max*/                     : Float32 = (340282326356119256160033759537265639424)
  var _ /*float32_max_p1*/                  : Float32 = (340282326356119256160033759537265639425)

  // 2^128 * (2^25-1)/2^25 - 1
  var _ /*float32_max_not_yet_overflow*/    : Float32 = (340282356779733661637539395458142568447)
  // 2^128 * (2^25-1)/2^25
  var _ /*float32_max_first_overflow*/      : Float32 = (340282356779733661637539395458142568448) // expected-error {{integer literal '340282356779733661637539395458142568448' overflows when stored into 'Float32'}}

  // 2^128
  var _ /*float32_max_definitely_overflow*/ : Float32 = (340282366920938463463374607431768211456) // expected-error {{integer literal '340282366920938463463374607431768211456' overflows when stored into 'Float32'}}

  // IEEE binary32 min value = -1 * 2^128 * (2^23-1)/2^23
  var _ /*float32_min*/                     : Float32 = (-340282326356119256160033759537265639424)
  var _ /*float32_min_p1*/                  : Float32 = (-340282326356119256160033759537265639425)

  // -1 * 2^128 * (2^25-1)/2^25 - 1
  var _ /*float32_min_not_yet_overflow*/    : Float32 = (-340282356779733661637539395458142568447)
  // -1 * 2^128 * (2^25-1)/2^25
  var _ /*float32_min_first_overflow*/      : Float32 = (-340282356779733661637539395458142568448) // expected-error {{integer literal '-340282356779733661637539395458142568448' overflows when stored into 'Float32'}}

  // -1 * 2^128
  var _ /*float32_min_definitely_overflow*/ : Float32 = (-340282366920938463463374607431768211456) // expected-error {{integer literal '-340282366920938463463374607431768211456' overflows when stored into 'Float32'}}

  // IEEE binary64 max value = 2^1024 * (2^52-1)/2^52
  var _ /*float64_max*/                     : Float64 = (179769313486231550856124328384506240234343437157459335924404872448581845754556114388470639943126220321960804027157371570809852884964511743044087662767600909594331927728237078876188760579532563768698654064825262115771015791463983014857704008123419459386245141723703148097529108423358883457665451722744025579520)
  var _ /*float64_max_p1*/                  : Float64 = (179769313486231550856124328384506240234343437157459335924404872448581845754556114388470639943126220321960804027157371570809852884964511743044087662767600909594331927728237078876188760579532563768698654064825262115771015791463983014857704008123419459386245141723703148097529108423358883457665451722744025579520)

  // 2^1024 * (2^54-1)/2^54 - 1
  var _/*float64_max_not_yet_overflow*/    : Float64 = (179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497791)
  // 2^1024 * (2^54-1)/2^54
  var _ /*float64_max_first_overflow*/      : Float64 = (179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792) // expected-error {{integer literal '179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792' overflows when stored into 'Double'}}

  // 2^1024
  var _/*float64_max_definitely_overflow*/ : Float64 = (179769313486231590772930519078902473361797697894230657273430081157732675805500963132708477322407536021120113879871393357658789768814416622492847430639474124377767893424865485276302219601246094119453082952085005768838150682342462881473913110540827237163350510684586298239947245938479716304835356329624224137216) // expected-error {{integer literal '179769313486231590772930519078902473361797697894230657273430081157732675805500963132708477322407536021120113879871393357658789768814416622492847430639474124377767893424865485276302219601246094119453082952085005768838150682342462881473913110540827237163350510684586298239947245938479716304835356329624224137216' overflows when stored into 'Double'}}

  // IEEE binary64 min value = -1 * 2^1024 * (2^52-1)/2^52
  var _/*float64_min*/                     : Float64 = (-179769313486231550856124328384506240234343437157459335924404872448581845754556114388470639943126220321960804027157371570809852884964511743044087662767600909594331927728237078876188760579532563768698654064825262115771015791463983014857704008123419459386245141723703148097529108423358883457665451722744025579520)
  var _/*float64_min_p1*/                  : Float64 = (-179769313486231550856124328384506240234343437157459335924404872448581845754556114388470639943126220321960804027157371570809852884964511743044087662767600909594331927728237078876188760579532563768698654064825262115771015791463983014857704008123419459386245141723703148097529108423358883457665451722744025579520)

  // -1 * 2^1024 * (2^54-1)/2^54 - 1
  var _/*float64_min_not_yet_overflow*/    : Float64 = (-179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497791)
  // -1 * 2^1024 * (2^54-1)/2^54
  var _/*float64_min_first_overflow*/      : Float64 = (-179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792) // expected-error {{integer literal '-179769313486231580793728971405303415079934132710037826936173778980444968292764750946649017977587207096330286416692887910946555547851940402630657488671505820681908902000708383676273854845817711531764475730270069855571366959622842914819860834936475292719074168444365510704342711559699508093042880177904174497792' overflows when stored into 'Double'}}

  // -1 * 2^1024
  var _/*float64_min_definitely_overflow*/ : Float64 = (-179769313486231590772930519078902473361797697894230657273430081157732675805500963132708477322407536021120113879871393357658789768814416622492847430639474124377767893424865485276302219601246094119453082952085005768838150682342462881473913110540827237163350510684586298239947245938479716304835356329624224137216) // expected-error {{integer literal '-179769313486231590772930519078902473361797697894230657273430081157732675805500963132708477322407536021120113879871393357658789768814416622492847430639474124377767893424865485276302219601246094119453082952085005768838150682342462881473913110540827237163350510684586298239947245938479716304835356329624224137216' overflows when stored into 'Double'}}
}

@_transparent 
func intConversionWrapperForUSCheckedConversion(_ x: UInt8, _ unused: UInt8) -> Int8 {
  return Int8(x)
}
@_transparent 
func intConversionWrapperForLiteral() -> Int8 {
  return 255 // expected-error {{integer literal '255' overflows when stored into 'Int8'}}
}
func testFallBackDiagnosticMessages() {
  _ = intConversionWrapperForUSCheckedConversion(255, 30) // expected-error {{integer overflows when converted from unsigned 'Builtin.Int8' to signed 'Builtin.Int8'}}
  _ = intConversionWrapperForLiteral() // expected-error {{integer literal '255' overflows when stored into signed 'Builtin.Int8'}}
}

// XXX FIXME -- blocked by: 15735295 Need [su]{div,rem}_with_overflow IR
/*
func testDivision() {
  var i  : Int = 3 / 3
  var id : Int = 3 / 0 // expected -error{{division by zero}}
  var ir : Int = 3 % 0 // expected -error{{division by zero}}

  var uzero : UInt8 = 0
  var uone : UInt8 = 1
  var u  : UInt8 = uzero / uone
  var ud : UInt8 = uone / uzero // expected -error{{division by zero}}
  var ur : UInt8 = uone % uzero // expected -error{{division by zero}}

  var f : Float = 3.0 / 0.0

  var minusOne : Int32 = -1
  var overflow : Int32 = -2147483648 / minusOne // expected -error{{division '-2147483648 / -1' results in an overflow}}
}
*/

func testPostIncOverflow() {
  var   s_max = Int.max
  s_max += 1  // expected-error {{arithmetic operation '9223372036854775807 + 1' (on type 'Int') results in an overflow}}

  var   u_max = UInt.max
  u_max += 1 // expected-error {{arithmetic operation '18446744073709551615 + 1' (on type 'UInt') results in an overflow}}

  var  s8_max = Int8.max
  s8_max += 1 // expected-error {{arithmetic operation '127 + 1' (on type 'Int8') results in an overflow}}

  var  u8_max = UInt8.max
  u8_max += 1 // expected-error {{arithmetic operation '255 + 1' (on type 'UInt8') results in an overflow}}

  var s16_max = Int16.max
  s16_max += 1 // expected-error {{arithmetic operation '32767 + 1' (on type 'Int16') results in an overflow}}

  var u16_max = UInt16.max
  u16_max += 1 // expected-error {{arithmetic operation '65535 + 1' (on type 'UInt16') results in an overflow}}

  var s32_max = Int32.max
  s32_max += 1 // expected-error {{arithmetic operation '2147483647 + 1' (on type 'Int32') results in an overflow}}

  var u32_max = UInt32.max
  u32_max += 1 // expected-error {{arithmetic operation '4294967295 + 1' (on type 'UInt32') results in an overflow}}

  var s64_max = Int64.max
  s64_max += 1 // expected-error {{arithmetic operation '9223372036854775807 + 1' (on type 'Int64') results in an overflow}}

  var u64_max = UInt64.max
  u64_max += 1 // expected-error {{arithmetic operation '18446744073709551615 + 1' (on type 'UInt64') results in an overflow}}
}

func testPostDecOverflow() {
  var   s_min = Int.min
  s_min -= 1  // expected-error {{arithmetic operation '-9223372036854775808 - 1' (on type 'Int') results in an overflow}}

  var   u_min = UInt.min
  u_min -= 1 // expected-error {{arithmetic operation '0 - 1' (on type 'UInt') results in an overflow}}

  var  s8_min = Int8.min
  s8_min -= 1 // expected-error {{arithmetic operation '-128 - 1' (on type 'Int8') results in an overflow}}

  var  u8_min = UInt8.min
  u8_min -= 1 // expected-error {{arithmetic operation '0 - 1' (on type 'UInt8') results in an overflow}}

  var s16_min = Int16.min
  s16_min -= 1 // expected-error {{arithmetic operation '-32768 - 1' (on type 'Int16') results in an overflow}}

  var u16_min = UInt16.min
  u16_min -= 1 // expected-error {{arithmetic operation '0 - 1' (on type 'UInt16') results in an overflow}}

  var s32_min = Int32.min
  s32_min -= 1 // expected-error {{arithmetic operation '-2147483648 - 1' (on type 'Int32') results in an overflow}}

  var u32_min = UInt32.min
  u32_min -= 1 // expected-error {{arithmetic operation '0 - 1' (on type 'UInt32') results in an overflow}}

  var s64_min = Int64.min
  s64_min -= 1 // expected-error {{arithmetic operation '-9223372036854775808 - 1' (on type 'Int64') results in an overflow}}

  var u64_min = UInt64.min
  u64_min -= 1 // expected-error {{arithmetic operation '0 - 1' (on type 'UInt64') results in an overflow}}
}

func testAssumeNonNegative() {
  let input = -3
  _ = _assumeNonNegative(input) // expected-error {{assumed non-negative value '-3' is negative}}
}

protocol Num { func Double() -> Self }

extension Int8 : Num {
  @_transparent
  func Double() -> Int8 { return self * 2 }
}

@_transparent
func Double<T : Num>(_ x: T) -> T { return x.Double() }

func tryDouble() -> Int8 {
  return Double(Int8.max) // expected-error {{arithmetic operation '127 * 2' (on signed 8-bit integer type) results in an overflow}}
}

@_transparent
func add<T : SignedInteger>(_ left: T, _ right: T) -> T {
  return left + right
}

@_transparent
func applyBinary<T : SignedInteger>(_ fn: (T, T) -> (T), _ left: T, _ right: T) -> T {
  return fn(left, right)
}

func testTransparentApply() -> Int8 {
  return applyBinary(add, Int8.max, Int8.max) // expected-error {{arithmetic operation '127 + 127' (on signed 8-bit integer type) results in an overflow}}
}
