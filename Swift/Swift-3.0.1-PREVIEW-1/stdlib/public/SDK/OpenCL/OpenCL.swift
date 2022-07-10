//===----------------------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

@_exported import OpenCL // Clang module

@available(OSX, introduced: 10.7)
public func clSetKernelArgsListAPPLE(
  _ kernel: cl_kernel, _ uint: cl_uint, _ args: CVarArg...
) -> cl_int {
  // The variable arguments are num_args arguments that are the following:
  //      cl_uint arg_indx,
  //      size_t arg_size,
  //      const void *arg_value,
  return withVaList(args) {  clSetKernelArgsVaListAPPLE(kernel, uint, $0) }
}
