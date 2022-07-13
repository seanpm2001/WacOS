// RUN: %empty-directory(%t)
// Clang-import a module.
import ClangModule

// Note: This test is highly dependent on the clang module cache
// format, but it is testing specifics of the module cache.

// 1. Test that swift-ide-test creates a thin module without debug info.

// RUN: %empty-directory(%t)
// RUN: %swift-ide-test_plain -print-usrs -target %target-triple -module-cache-path %t  -I %S/Inputs -source-filename %s
// RUN: head -c 4 %t/*/ClangModule-*.pcm | grep -q CPCH

// 2. Test that swift is creating clang modules with debug info.

// RUN: %empty-directory(%t)
// RUN: %target-swift-frontend %s -c -g -o %t.o -module-cache-path %t -I %S/Inputs
// RUN: file %t/*/ClangModule-*.pcm | egrep -q '(Mach-O|ELF)'

// 3. Test that swift-ide-check will not share swiftc's module cache.

// RUN: %swift-ide-test_plain -print-usrs -target %target-triple -module-cache-path %t  -I %S/Inputs -source-filename %s
