// RUN: not %swift -typecheck -target %target-triple %s %S/Inputs/fixits-enum-multifile.swift -emit-fixits-path %t.remap -I %S/Inputs -diagnostics-editor-mode
// RUN: c-arcmt-test %t.remap | arcmt-test -verify-transformed-files %s.result

func foo1(_ e: EMulti) {
  switch e {
  }
}
