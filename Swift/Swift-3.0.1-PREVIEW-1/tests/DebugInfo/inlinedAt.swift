// RUN: %target-swift-frontend %s -O -I %t -emit-sil -emit-verbose-sil -o - \
// RUN:    | %FileCheck %s --check-prefix=CHECK-SIL
// RUN: %target-swift-frontend %s -O -I %t -emit-ir -g -o - | %FileCheck %s

public var glob : Int = 0
@inline(never) public func hold(_ n : Int) { glob = n }

#sourceLocation(file: "abc.swift", line: 100)
@inline(__always)
func h(_ k : Int) -> Int {      // 101
  hold(k)                       // 102
  return k                      // 103
}

#sourceLocation(file: "abc.swift", line: 200)
@inline(__always)
func g(_ j : Int) -> Int {      // 201
  hold(j)                       // 202
  return h(j)                   // 203
}

#sourceLocation(file: "abc.swift", line: 301)
public func f(_ i : Int) -> Int { // 301
  return g(i)                     // 302
}

// CHECK-SIL: sil {{.*}}@_TF9inlinedAt1fFSiSi :
// CHECK-SIL-NOT: return
// CHECK-SIL: debug_value %0 : $Int, let, name "k", argno 1
// CHECK-SIL-SAME: line:101:10:in_prologue
// CHECK-SIL-SAME: perf_inlined_at line:203:10
// CHECK-SIL-SAME: perf_inlined_at line:302:10

// CHECK: define {{.*}}@_TF9inlinedAt1fFSiSi({{.*}})
// CHECK-NOT: ret
// CHECK: @llvm.dbg.value
// CHECK: @llvm.dbg.value
// CHECK: @llvm.dbg.value({{.*}}), !dbg ![[L1:.*]]

// CHECK: ![[F:.*]] = distinct !DISubprogram(name: "f",
// CHECK: ![[G:.*]] = distinct !DISubprogram(name: "g",

// CHECK: ![[L3:.*]] = !DILocation(line: 302, column: 13,
// CHECK-SAME:                     scope: ![[F_SCOPE:.*]])
// CHECK: ![[F_SCOPE]] = distinct !DILexicalBlock(scope: ![[F]],
// CHECK-SAME:                                    line: 301, column: 33)
// CHECK: ![[G_SCOPE:.*]] = distinct !DILexicalBlock(scope: ![[G]],
// CHECK-SAME:                                    line: 201, column: 26)
// CHECK: ![[H:.*]] = distinct !DISubprogram(name: "h",
// CHECK: ![[L1]] = !DILocation(line: 101, column: 8, scope: ![[H]],
// CHECK-SAME:                  inlinedAt: ![[L2:.*]])
// CHECK: ![[L2]] = !DILocation(line: 203, column: 13, scope: ![[G_SCOPE]],
// CHECK-SAME:                  inlinedAt: ![[L3]])
