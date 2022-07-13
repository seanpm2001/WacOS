// FIXME(integer): with new integer protocols implemented the overflows are no
// longer caught: <rdar://problem/29937936>
// XFAIL: *

// RUN: not %target-swift-frontend -emit-sil %s 2>&1 | %FileCheck --check-prefix=CHECK-%target-ptrsize %s

// FIXME: This test should be merged back into
// diagnostic_constant_propagation.swift when we have fixed:
// <rdar://problem/19434979> -verify does not respect #if
//
// For the same reason, this test is using FileCheck instead of -verify.

// FIXME: <rdar://problem/19508336> Extend test/SILOptimizer/diagnostic_constant_propagation.swift to 32-bit platforms

#if arch(i386) || arch(arm)
func testArithmeticOverflow_Int_32bit() {
  do {
    // Literals.
    var t1: Int = 0x7fff_ffff // OK
    var t2: Int = 0x8000_0000
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: integer literal {{.*}}overflows when stored into 'Int'{{$}}

    var t3: Int = -0x8000_0000 // OK
    var t4: Int = -0x8000_0001
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: integer literal {{.*}}overflows when stored into 'Int'{{$}}
  }

  do {
    // Negation.
    var t1: Int = -(-0x7fff_ffff) // OK
    var t2: Int = -(-0x8000_0000)
    // FIXME: Missing diagnostic:
    // <rdar://problem/19623142> Overflow in arithmetic negation is not detected at compile time
  }

  do {
    // Addition.
    var t1: Int = 0x7fff_fffe + 1 // OK
    var t2: Int = 0x7fff_fffe + 2
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '2147483646 + 2' (on type 'Int') results in an overflow{{$}}

    var t3: Int = -0x7fff_ffff + (-1) // OK
    var t4: Int = -0x7fff_ffff + (-2)
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '-2147483647 + -2' (on type 'Int') results in an overflow{{$}}
  }

  do {
    // Subtraction.
    var t1: Int = 0x7fff_fffe - (-1) // OK
    var t2: Int = 0x7fff_fffe - (-2)
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '2147483646 - -2' (on type 'Int') results in an overflow{{$}}

    var t3: Int = -0x7fff_ffff - 1 // OK
    var t4: Int = -0x7fff_ffff - 2
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '-2147483647 - 2' (on type 'Int') results in an overflow{{$}}
  }

  do {
    // Multiplication.
    var t1: Int = 0x7fff_fffe * 1 // OK
    var t2: Int = 0x7fff_fffe * 2
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '2147483646 * 2' (on type 'Int') results in an overflow{{$}}

    var t3: Int = -0x7fff_ffff * 1 // OK
    var t4: Int = -0x7fff_ffff * 2
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '-2147483647 * 2' (on type 'Int') results in an overflow{{$}}
  }

  do {
    // Division.
    var t1: Int = 0x7fff_fffe / 2 // OK
    var t2: Int = 0x7fff_fffe / 0
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t3: Int = -0x7fff_ffff / 2 // OK
    var t4: Int = -0x7fff_ffff / 0
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t5: Int = -0x8000_0000 / -1
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division '-2147483648 / -1' results in an overflow{{$}}
  }

  do {
    // Remainder.
    var t1: Int = 0x7fff_fffe % 2 // OK
    var t2: Int = 0x7fff_fffe % 0
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t3: Int = -0x7fff_ffff % 2 // OK
    var t4: Int = -0x7fff_ffff % 0
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t5: Int = -0x8000_0000 % -1
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division '-2147483648 % -1' results in an overflow{{$}}
  }

  do {
    // Right shift.
    var t1: Int = 0 >> 0
    var t2: Int = 0 >> 1
    var t3: Int = 0 >> (-1)
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer cannot be converted to unsigned type 'Builtin.Int32'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19622485> 'Builtin.Int64' leaks into diagnostics

    var t4: Int = 123 >> 0
    var t5: Int = 123 >> 1
    var t6: Int = 123 >> (-1)
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer cannot be converted to unsigned type 'Builtin.Int32'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19622485> 'Builtin.Int64' leaks into diagnostics

    var t7: Int = (-1) >> 0
    var t8: Int = (-1) >> 1

    var t9: Int = 0x7fff_ffff >> 31 // OK
    var t10: Int = 0x7fff_ffff >> 32
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
    var t11: Int = 0x7fff_ffff >> 33
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
  }

  do {
    // Left shift.
    var t1: Int = 0 << 0
    var t2: Int = 0 << 1
    var t3: Int = 0 << (-1)
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer cannot be converted to unsigned type 'Builtin.Int32'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19622485> 'Builtin.Int64' leaks into diagnostics

    var t4: Int = 123 << 0
    var t5: Int = 123 << 1
    var t6: Int = 123 << (-1)
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer cannot be converted to unsigned type 'Builtin.Int32'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19622485> 'Builtin.Int64' leaks into diagnostics

    var t7: Int = (-1) << 0
    var t8: Int = (-1) << 1

    var t9: Int = 0x7fff_ffff << 31 // OK
    var t10: Int = 0x7fff_ffff << 32
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
    var t11: Int = 0x7fff_ffff << 33
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
  }
}

func testArithmeticOverflow_UInt_32bit() {
  do {
    // Literals.
    var t1: UInt = 0x7fff_ffff // OK
    var t2: UInt = 0x8000_0000
    var t3: UInt = 0xffff_ffff

    var t4: UInt = -1
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer {{.*}}overflows when stored into unsigned type 'UInt'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19623566> Obscure diagnostic for assigning negative numbers to unsigned

    var t5: UInt = -0xffff_ffff
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative {{.*}}overflows when stored into unsigned type 'UInt'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19623566> Obscure diagnostic for assigning negative numbers to unsigned
  }

  do {
    // There is no negation for unsigned integers.
  }

  do {
    // Addition.
    var t1: UInt = 0 + 0 // OK
    var t2: UInt = 0xffff_ffff + 0 // OK
    var t3: UInt = 0xffff_ffff + 1
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '4294967295 + 1' (on type 'UInt') results in an overflow{{$}}

    var t4: UInt = 0xffff_fffe + 1 // OK
    var t5: UInt = 0xffff_fffe + 2
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '4294967294 + 2' (on type 'UInt') results in an overflow{{$}}
  }

  do {
    // Subtraction.
    var t1: UInt = 0xffff_fffe - 1 // OK
    var t2: UInt = 0xffff_fffe - 0xffff_ffff
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '4294967294 - 4294967295' (on type 'UInt') results in an overflow{{$}}

    var t3: UInt = 0 - 0 // OK
    var t4: UInt = 0 - 1
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '0 - 1' (on type 'UInt') results in an overflow{{$}}
  }

  do {
    // Multiplication.
    var t1: UInt = 0xffff_ffff * 0 // OK
    var t2: UInt = 0xffff_ffff * 1 // OK
    var t3: UInt = 0xffff_ffff * 2 // OK
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '4294967295 * 2' (on type 'UInt') results in an overflow{{$}}

    var t4: UInt = 0xffff_ffff * 0xffff_ffff // OK
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '4294967295 * 4294967295' (on type 'UInt') results in an overflow{{$}}

    var t5: UInt = 0x7fff_fffe * 0 // OK
    var t6: UInt = 0x7fff_fffe * 1 // OK
    var t7: UInt = 0x7fff_fffe * 2 // OK
    var t8: UInt = 0x7fff_fffe * 3 // OK
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '2147483646 * 3' (on type 'UInt') results in an overflow{{$}}
  }

  do {
    // Division.
    var t1: UInt = 0x7fff_fffe / 2 // OK
    var t2: UInt = 0x7fff_fffe / 0
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t3: UInt = 0xffff_ffff / 2 // OK
    var t4: UInt = 0xffff_ffff / 0
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

  }

  do {
    // Remainder.
    var t1: UInt = 0x7fff_fffe % 2 // OK
    var t2: UInt = 0x7fff_fffe % 0
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t3: UInt = 0xffff_ffff % 2 // OK
    var t4: UInt = 0xffff_ffff % 0
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}
  }

  do {
    // Right shift.
    var t1: UInt = 0 >> 0
    var t2: UInt = 0 >> 1

    var t3: UInt = 123 >> 0
    var t4: UInt = 123 >> 1

    var t5: UInt = (-1) >> 0
    var t6: UInt = (-1) >> 1

    var t7: UInt = 0x7fff_ffff >> 31 // OK
    var t8: UInt = 0x7fff_ffff >> 32
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
    var t9: UInt = 0x7fff_ffff >> 33
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
  }

  do {
    // Left shift.
    var t1: UInt = 0 << 0
    var t2: UInt = 0 << 1

    var t3: UInt = 123 << 0
    var t4: UInt = 123 << 1

    var t5: UInt = (-1) << 0
    var t6: UInt = (-1) << 1

    var t7: UInt = 0x7fff_ffff << 31 // OK
    var t8: UInt = 0x7fff_ffff << 32
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
    var t9: UInt = 0x7fff_ffff << 33
    // CHECK-32-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
  }
}

#elseif arch(x86_64) || arch(arm64) || arch(powerpc64) || arch(powerpc64le) || arch(s390x)

func testArithmeticOverflow_Int_64bit() {
  do {
    // Literals.
    var t1: Int = 0x7fff_ffff_ffff_ffff // OK
    var t2: Int = 0x8000_0000_0000_0000
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: integer literal {{.*}} overflows when stored into 'Int'{{$}}

    var t3: Int = -0x8000_0000_0000_0000 // OK
    var t4: Int = -0x8000_0000_0000_0001
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: integer literal {{.*}} overflows when stored into 'Int'{{$}}
  }

  do {
    // Negation.
    var t1: Int = -(-0x7fff_ffff_ffff_ffff) // OK
    var t2: Int = -(-0x8000_0000_0000_0000)
    // FIXME: Missing diagnostic:
    // <rdar://problem/19623142> Overflow in arithmetic negation is not detected at compile time
  }

  do {
    // Addition.
    var t1: Int = 0x7fff_ffff_ffff_fffe + 1 // OK
    var t2: Int = 0x7fff_ffff_ffff_fffe + 2
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '9223372036854775806 + 2' (on type 'Int') results in an overflow{{$}}

    var t3: Int = -0x7fff_ffff_ffff_ffff + (-1) // OK
    var t4: Int = -0x7fff_ffff_ffff_ffff + (-2)
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '-9223372036854775807 + -2' (on type 'Int') results in an overflow{{$}}
  }

  do {
    // Subtraction.
    var t1: Int = 0x7fff_ffff_ffff_fffe - (-1) // OK
    var t2: Int = 0x7fff_ffff_ffff_fffe - (-2)
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '9223372036854775806 - -2' (on type 'Int') results in an overflow{{$}}

    var t3: Int = -0x7fff_ffff_ffff_ffff - 1 // OK
    var t4: Int = -0x7fff_ffff_ffff_ffff - 2
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '-9223372036854775807 - 2' (on type 'Int') results in an overflow{{$}}
  }

  do {
    // Multiplication.
    var t1: Int = 0x7fff_ffff_ffff_fffe * 1 // OK
    var t2: Int = 0x7fff_ffff_ffff_fffe * 2
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '9223372036854775806 * 2' (on type 'Int') results in an overflow{{$}}

    var t3: Int = -0x7fff_ffff_ffff_ffff * 1 // OK
    var t4: Int = -0x7fff_ffff_ffff_ffff * 2
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '-9223372036854775807 * 2' (on type 'Int') results in an overflow{{$}}
  }

  do {
    // Division.
    var t1: Int = 0x7fff_ffff_ffff_fffe / 2 // OK
    var t2: Int = 0x7fff_ffff_ffff_fffe / 0
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t3: Int = -0x7fff_ffff_ffff_ffff / 2 // OK
    var t4: Int = -0x7fff_ffff_ffff_ffff / 0
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t5: Int = -0x8000_0000_0000_0000 / -1
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division '-9223372036854775808 / -1' results in an overflow{{$}}
  }

  do {
    // Remainder.
    var t1: Int = 0x7fff_ffff_ffff_fffe % 2 // OK
    var t2: Int = 0x7fff_ffff_ffff_fffe % 0
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t3: Int = -0x7fff_ffff_ffff_ffff % 2 // OK
    var t4: Int = -0x7fff_ffff_ffff_ffff % 0
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t5: Int = -0x8000_0000_0000_0000 % -1
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division '-9223372036854775808 % -1' results in an overflow{{$}}
  }

  do {
    // Right shift.
    var t1: Int = 0 >> 0
    var t2: Int = 0 >> 1
    var t3: Int = 0 >> (-1)
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer cannot be converted to unsigned type 'Builtin.Int64'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19622485> 'Builtin.Int64' leaks into diagnostics

    var t4: Int = 123 >> 0
    var t5: Int = 123 >> 1
    var t6: Int = 123 >> (-1)
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer cannot be converted to unsigned type 'Builtin.Int64'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19622485> 'Builtin.Int64' leaks into diagnostics

    var t7: Int = (-1) >> 0
    var t8: Int = (-1) >> 1

    var t9: Int = 0x7fff_ffff_ffff_ffff >> 63
    var t10: Int = 0x7fff_ffff_ffff_ffff >> 64
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
    var t11: Int = 0x7fff_ffff_ffff_ffff >> 65
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
  }

  do {
    // Left shift.
    var t1: Int = 0 << 0
    var t2: Int = 0 << 1
    var t3: Int = 0 << (-1)
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer cannot be converted to unsigned type 'Builtin.Int64'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19622485> 'Builtin.Int64' leaks into diagnostics

    var t4: Int = 123 << 0
    var t5: Int = 123 << 1
    var t6: Int = 123 << (-1)
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer cannot be converted to unsigned type 'Builtin.Int64'{{$}}
    // FIXME: Bad diagnostic:
    // <rdar://problem/19622485> 'Builtin.Int64' leaks into diagnostics

    var t7: Int = (-1) << 0
    var t8: Int = (-1) << 1

    var t9: Int = 0x7fff_ffff_ffff_ffff << 63
    var t10: Int = 0x7fff_ffff_ffff_ffff << 64
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
    var t11: Int = 0x7fff_ffff_ffff_ffff << 65
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
  }
}

func testArithmeticOverflow_UInt_64bit() {
  do {
    // Literals.
    var t1: UInt = 0x7fff_ffff_ffff_ffff // OK
    var t2: UInt = 0x8000_0000_0000_0000
    var t3: UInt = 0xffff_ffff_ffff_ffff

    var t4: UInt = -1
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer {{.*}} overflows when stored into unsigned type 'UInt'{{$}}

    var t5: UInt = -0xffff_ffff_ffff_ffff
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: negative integer {{.*}} overflows when stored into unsigned type 'UInt'{{$}}
  }

  do {
    // There is no negation for unsigned integers.
  }

  do {
    // Addition.
    var t1: UInt = 0 + 0 // OK
    var t2: UInt = 0xffff_ffff_ffff_ffff + 0 // OK
    var t3: UInt = 0xffff_ffff_ffff_ffff + 1
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '18446744073709551615 + 1' (on type 'UInt') results in an overflow{{$}}

    var t4: UInt = 0xffff_ffff_ffff_fffe + 1 // OK
    var t5: UInt = 0xffff_ffff_ffff_fffe + 2
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '18446744073709551614 + 2' (on type 'UInt') results in an overflow{{$}}
  }

  do {
    // Subtraction.
    var t1: UInt = 0xffff_ffff_ffff_fffe - 1 // OK
    var t2: UInt = 0xffff_ffff_ffff_fffe - 0xffff_ffff_ffff_ffff
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '18446744073709551614 - 18446744073709551615' (on type 'UInt') results in an overflow{{$}}

    var t3: UInt = 0 - 0 // OK
    var t4: UInt = 0 - 1
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '0 - 1' (on type 'UInt') results in an overflow{{$}}
  }

  do {
    // Multiplication.
    var t1: UInt = 0xffff_ffff_ffff_ffff * 0 // OK
    var t2: UInt = 0xffff_ffff_ffff_ffff * 1 // OK
    var t3: UInt = 0xffff_ffff_ffff_ffff * 2 // OK
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '18446744073709551615 * 2' (on type 'UInt') results in an overflow{{$}}

    var t4: UInt = 0xffff_ffff_ffff_ffff * 0xffff_ffff_ffff_ffff // OK
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '18446744073709551615 * 18446744073709551615' (on type 'UInt') results in an overflow{{$}}

    var t5: UInt = 0x7fff_ffff_ffff_fffe * 0 // OK
    var t6: UInt = 0x7fff_ffff_ffff_fffe * 1 // OK
    var t7: UInt = 0x7fff_ffff_ffff_fffe * 2 // OK
    var t8: UInt = 0x7fff_ffff_ffff_fffe * 3 // OK
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: arithmetic operation '9223372036854775806 * 3' (on type 'UInt') results in an overflow{{$}}
  }

  do {
    // Division.
    var t1: UInt = 0x7fff_ffff_ffff_fffe / 2 // OK
    var t2: UInt = 0x7fff_ffff_ffff_fffe / 0
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t3: UInt = 0xffff_ffff_ffff_ffff / 2 // OK
    var t4: UInt = 0xffff_ffff_ffff_ffff / 0
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

  }

  do {
    // Remainder.
    var t1: UInt = 0x7fff_ffff_ffff_fffe % 2 // OK
    var t2: UInt = 0x7fff_ffff_ffff_fffe % 0
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}

    var t3: UInt = 0xffff_ffff_ffff_ffff % 2 // OK
    var t4: UInt = 0xffff_ffff_ffff_ffff % 0
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: division by zero{{$}}
  }

  do {
    // Right shift.
    var t1: UInt = 0 >> 0
    var t2: UInt = 0 >> 1

    var t3: UInt = 123 >> 0
    var t4: UInt = 123 >> 1

    var t5: UInt = (-1) >> 0
    var t6: UInt = (-1) >> 1

    var t7: UInt = 0x7fff_ffff_ffff_ffff >> 63 // OK
    var t8: UInt = 0x7fff_ffff_ffff_ffff >> 64
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
    var t9: UInt = 0x7fff_ffff_ffff_ffff >> 65
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
  }

  do {
    // Left shift.
    var t1: UInt = 0 << 0
    var t2: UInt = 0 << 1

    var t3: UInt = 123 << 0
    var t4: UInt = 123 << 1

    var t5: UInt = (-1) << 0
    var t6: UInt = (-1) << 1

    var t7: UInt = 0x7fff_ffff_ffff_ffff << 63 // OK
    var t8: UInt = 0x7fff_ffff_ffff_ffff << 64
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
    var t9: UInt = 0x7fff_ffff_ffff_ffff << 65
    // CHECK-64-DAG: .swift:[[@LINE-1]]:{{[0-9]+}}: error: shift amount is greater than or equal to type size in bits{{$}}
  }
}

#else
_Unimplemented()
#endif

