// RUN: %target-swift-frontend -emit-sil %s -Onone -Xllvm \
// RUN:   -sil-print-after=allocbox-to-stack -Xllvm \
// RUN:   -sil-print-functions=$s3red19ThrowAddrOnlyStructV016throwsOptionalToG0ACyxGSgSi_tcfC \
// RUN:   -Xllvm -sil-print-debuginfo -o %t -module-name red 2>&1 | %FileCheck %s

// CHECK: bb{{[0-9]+}}(%{{[0-9]+}} : @owned $Error):
// CHECK:   dealloc_stack %{{[0-9]+}} : $*ThrowAddrOnlyStruct<T>, loc {{.*}}:26:68, scope 2
// CHECK:   br bb{{[0-9]+}}(%{{[0-9]+}} : $Error), loc {{.*}}:26:15, scope 2

protocol Patatino {
  init()
}
struct ThrowAddrOnlyStruct<T : Patatino> {
  var x : T
  init(fail: ()) throws { x = T() }
  init(failDuringDelegation: Int) throws {
    try self.init(fail: ())
  }
  init?(throwsToOptional: Int) {
    try? self.init(failDuringDelegation: throwsToOptional)
  }
  init(throwsOptionalToThrows: Int) throws {
    self.init(throwsToOptional: throwsOptionalToThrows)!
  }
  init?(throwsOptionalToOptional: Int) {
    try! self.init(throwsOptionalToThrows: throwsOptionalToOptional)
  }
}
