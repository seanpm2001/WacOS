// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil %s -profile-generate -profile-coverage-mapping -emit-sil -o - | %FileCheck %s --check-prefix=SIL
// RUN: %target-swift-frontend -assume-parsing-unqualified-ownership-sil %s -profile-generate -profile-coverage-mapping -emit-ir -o - | %FileCheck %s --check-prefix=IR

// IR-NOT: __llvm_coverage_names
// IR-NOT: __profn

// SIL-DAG: sil hidden @_T08coverage2f1yyF
// SIL-DAG: string_literal utf8 "{{.*}}coverage.swift:{{.*}}_T08coverage2f1yyF"
internal func f1() {}

// SIL-DAG: sil private @_T08coverage2f233_[[F2HASH:[_a-zA-Z0-9]+]]
// SIL-DAG: string_literal utf8 "{{.*}}coverage.swift:_T08coverage2f233_[[F2HASH]]"
private func f2() {}

// SIL-DAG: sil @_T08coverage2f3yyF
// SIL-DAG: string_literal utf8 "_T08coverage2f3yyF"
public func f3() {
  f1();
  f2();
}
