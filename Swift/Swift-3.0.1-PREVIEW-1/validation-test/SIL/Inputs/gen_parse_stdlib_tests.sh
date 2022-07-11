#!/usr/bin/env bash

process_count=17
process_id_max=$((process_count - 1))

for id in $(seq 0 $process_id_max); do

  cat > parse_stdlib_$id.sil <<__EOF__
//// Automatically Generated From validation-test/SIL/Inputs/gen_parse_stdlib_tests.sh
////// Do Not Edit Directly!

// Make sure that we can parse the stdlib.sil deserialized from Swift.swiftmodule.

// RUN: rm -f %t.*
// RUN: %target-sil-opt -enable-sil-verify-all -sil-disable-ast-dump %platform-module-dir/Swift.swiftmodule -module-name=Swift -o %t.sil
// RUN: %target-sil-opt -enable-sil-verify-all %t.sil -ast-verifier-process-count=$process_count -ast-verifier-process-id=$id > /dev/null
// REQUIRES: long_test
// REQUIRES: nonexecutable_test
__EOF__

done
