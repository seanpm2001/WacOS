// With RTTI some of the objects with virtual bases / destructors in this test
// will cause linker errors because of undefined vtables.
// FIXME: Once we can link with libc++ we can start using RTTI.
// 
// RUN: %target-run-simple-swift(-I %S/Inputs -Xfrontend -enable-cxx-interop -Xcc -fno-rtti)
// RUN: %target-run-simple-swift(-I %S/Inputs -Xfrontend -enable-cxx-interop -Xcc -fno-rtti -O)
//
// REQUIRES: executable_test
// Windows doesn't support -fno-rtti.
// UNSUPPORTED: OS=windows-msvc

import CustomDestructor
import StdlibUnittest

var CXXDestructorTestSuite = TestSuite("CXXDestructor")

func createTypeWithVirtualBaseAndDestructor(
  _ ptr: UnsafeMutablePointer<Int32>
) {
  _ = HasVirtualBaseAndDestructor(ptr)
}

func createTypeWithBaseWithVirtualDestructor(
  _ ptr: UnsafeMutablePointer<Int32>
) {
  _ = HasBaseWithVirtualDestructor(ptr)
}

func createTypeWithVirtualBaseWithVirtualDestructor(
  _ ptr: UnsafeMutablePointer<Int32>
) {
  _ = HasVirtualBaseWithVirtualDestructor(ptr)
}

CXXDestructorTestSuite.test("Virtual base and destructor") {
  var value: Int32 = 0
  createTypeWithVirtualBaseAndDestructor(&value)
  expectEqual(value, 42)
}

CXXDestructorTestSuite.test("Base with virtual destructor") {
  var value: Int32 = 0
  createTypeWithBaseWithVirtualDestructor(&value)
  expectEqual(value, 42)
}

CXXDestructorTestSuite.test("Virtual base with virtual destructor") {
  var value: Int32 = 0
  createTypeWithVirtualBaseWithVirtualDestructor(&value)
  expectEqual(value, 42)
}

CXXDestructorTestSuite.test("Type with virtual defaulted destructor") {
  _ = HasVirtualDefaultedDestructor()
}

runAllTests()
