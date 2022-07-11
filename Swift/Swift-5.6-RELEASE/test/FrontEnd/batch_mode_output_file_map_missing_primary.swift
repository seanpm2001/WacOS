// RUN: %empty-directory(%t)
// RUN: echo 'print("Hello, World!")' >%t/main.swift
// RUN: touch %t/file-01.swift
// RUN: cd %t
// RUN: not %target-swift-frontend -parse -primary-file main.swift -primary-file file-01.swift -supplementary-output-file-map %S/Inputs/supplementary_output_filemap_missing_a_primary.yaml >%t/errs.txt 2>&1
// RUN: %FileCheck %s <%t/errs.txt

// CHECK: error: supplementary output file map '{{.*}}supplementary_output_filemap_missing_a_primary.yaml' is missing an entry for 'file-01.swift' (this likely indicates a compiler issue; please submit a bug report (https://swift.org/contributing/#reporting-bugs) and include the project)
