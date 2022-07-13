// RUN: %target-swift-frontend %s -emit-ir -g -o %t.ll
// RUN: %FileCheck %s < %t.ll

func markUsed<T>(_ t: T) {}

var puzzleOutput: [String] = []
// CHECK-NOT: !DILocalVariable(name: "$letter$generator"
// CHECK: !DILocalVariable(name: "letter",
// CHECK-SAME:             line: [[@LINE+1]]
for letter in ["g", "r", "e", "a", "t"] {
  switch letter {
  case "a", "e", "i", "o", "u", " ":
    continue
  default:
    puzzleOutput.append(letter)
  }
}
markUsed(puzzleOutput)


func count() {
// CHECK-NOT: !DILocalVariable(name: "$i$generator"
// CHECK: !DILocalVariable(name: "i",
// CHECK-SAME:             line: [[@LINE+1]]
  for i in 0...100 {
    markUsed(i)
  }
}
count()

// End-to-end test:
// RUN: llc %t.ll -filetype=obj -o - | %llvm-dwarfdump - | %FileCheck %s --check-prefix DWARF-CHECK
// DWARF-CHECK:  DW_TAG_variable
// DWARF-CHECK:  DW_AT_name ("letter")
//
// DWARF-CHECK:  DW_TAG_variable
// DWARF-CHECK:  DW_AT_name ("i")
