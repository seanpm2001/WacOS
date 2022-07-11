// RUN: %target-swift-frontend -Xllvm -sil-full-demangle -profile-generate -profile-coverage-mapping -emit-sorted-sil -emit-sil -module-name coverage_var_init %s | %FileCheck %s
// RUN: %target-swift-frontend -profile-generate -profile-coverage-mapping -emit-ir %s

final class VarInit {
  // CHECK: sil_coverage_map {{.*}} "$s17coverage_var_init7VarInitC018initializedWrapperE0SivpfP"
  // CHECK-NEXT: [[@LINE+1]]:4 -> [[@LINE+1]]:42 : 0
  @Wrapper var initializedWrapperInit = 2

  // CHECK: sil_coverage_map {{.*}} "$s17coverage_var_init7VarInitC04lazydE033_49373CB2DFB47C8DC62FA963604688DFLLSSvgSSyXEfU_"
  // CHECK-NEXT: [[@LINE+1]]:42 -> [[@LINE+3]]:4 : 0
  private lazy var lazyVarInit: String = {
    return "lazyVarInit"
  }()

  // CHECK: sil_coverage_map {{.*}} "$s17coverage_var_init7VarInitC05basicdE033_49373CB2DFB47C8DC62FA963604688DFLLSSvpfiSSyXEfU_"
  // CHECK-NEXT: [[@LINE+1]]:38 -> [[@LINE+3]]:4 : 0
  private var basicVarInit: String = {
    return "Hello"
  }()

  // CHECK: sil_coverage_map {{.*}} "$s17coverage_var_init7VarInitC06simpleD033_49373CB2DFB47C8DC62FA963604688DFLLSSvg"
  // CHECK-NEXT: [[@LINE+1]]:33 -> [[@LINE+3]]:4 : 0
  private var simpleVar: String {
    return "Hello"
  }

  func coverageFunction() {
    print(lazyVarInit)
    print(basicVarInit)
    print(simpleVar)
    print(initializedWrapperInit)
  }
}

@propertyWrapper struct Wrapper {
  init(wrappedValue: Int) {}
  var wrappedValue: Int { 1 }
}

VarInit().coverageFunction()
