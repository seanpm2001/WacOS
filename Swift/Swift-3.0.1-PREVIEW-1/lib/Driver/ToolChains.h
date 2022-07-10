//===--- ToolChains.h - Platform-specific ToolChain logic -------*- C++ -*-===//
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

#ifndef SWIFT_DRIVER_TOOLCHAINS_H
#define SWIFT_DRIVER_TOOLCHAINS_H

#include "swift/Basic/LLVM.h"
#include "swift/Driver/ToolChain.h"
#include "llvm/Support/Compiler.h"

namespace swift {
namespace driver {
namespace toolchains {

class LLVM_LIBRARY_VISIBILITY Darwin : public ToolChain {
protected:
  InvocationInfo constructInvocation(const InterpretJobAction &job,
                                     const JobContext &context) const override;
  InvocationInfo constructInvocation(const LinkJobAction &job,
                                     const JobContext &context) const override;

  std::string findProgramRelativeToSwiftImpl(StringRef name) const override;

public:
  Darwin(const Driver &D, const llvm::Triple &Triple) : ToolChain(D, Triple) {}
  ~Darwin() = default;

};

class LLVM_LIBRARY_VISIBILITY GenericUnix : public ToolChain {
protected:
  InvocationInfo constructInvocation(const InterpretJobAction &job,
                                     const JobContext &context) const override;
  InvocationInfo constructInvocation(const AutolinkExtractJobAction &job,
                                     const JobContext &context) const override;

  /// If provided, and if the user has not already explicitly specified a
  /// linker to use via the "-fuse-ld=" option, this linker will be passed to
  /// the compiler invocation via "-fuse-ld=". Return an empty string to not
  /// specify any specific linker (the "-fuse-ld=" option will not be
  /// specified).
  ///
  /// The default behavior is to use the gold linker on ARM architectures,
  /// and to not provide a specific linker otherwise.
  virtual std::string getDefaultLinker() const;

  /// The target to be passed to the compiler invocation. By default, this
  /// is the target triple, but this may be overridden to accommodate some
  /// platforms.
  virtual std::string getTargetForLinker() const;

  /// Whether to specify a linker -rpath to the Swift runtime library path.
  /// -rpath is not supported on all platforms, and subclasses may override
  /// this method to return false on platforms that don't support it. The
  /// default is to return true (and so specify an -rpath).
  virtual bool shouldProvideRPathToLinker() const;

  /// Provides a path to an object that should be linked first. On platforms
  /// that use ELF binaries, an object that provides markers and sizes for
  /// metadata sections must be linked first. Platforms that do not need this
  /// object may return an empty string; no additional objects are linked in
  /// this case.
  ///
  /// \param RuntimeLibraryPath A path to the Swift resource directory, which
  ///        on ARM architectures will contain metadata "begin" and "end"
  ///        objects.
  virtual std::string
  getPreInputObjectPath(StringRef RuntimeLibraryPath) const;

  /// Provides a path to an object that should be linked last. On platforms
  /// that use ELF binaries, an object that provides markers and sizes for
  /// metadata sections must be linked last. Platforms that do not need this
  /// object may return an empty string; no additional objects are linked in
  /// this case.
  ///
  /// \param RuntimeLibraryPath A path to the Swift resource directory, which
  ///        on ARM architectures will contain metadata "begin" and "end"
  ///        objects.
  virtual std::string
  getPostInputObjectPath(StringRef RuntimeLibraryPath) const;

  InvocationInfo constructInvocation(const LinkJobAction &job,
                                     const JobContext &context) const override;

public:
  GenericUnix(const Driver &D, const llvm::Triple &Triple) : ToolChain(D, Triple) {}
  ~GenericUnix() = default;
};

class LLVM_LIBRARY_VISIBILITY Android : public GenericUnix {
protected:
  std::string getTargetForLinker() const override;

  bool shouldProvideRPathToLinker() const override;
public:
  Android(const Driver &D, const llvm::Triple &Triple) : GenericUnix(D, Triple) {}
  ~Android() = default;
};

class LLVM_LIBRARY_VISIBILITY Cygwin : public GenericUnix {
protected:
  std::string getDefaultLinker() const override;

  std::string getTargetForLinker() const override;

  std::string getPreInputObjectPath(
    StringRef RuntimeLibraryPath) const override;

  std::string getPostInputObjectPath(
    StringRef RuntimeLibraryPath) const override;
public:
  Cygwin(const Driver &D, const llvm::Triple &Triple) : GenericUnix(D, Triple) {}
  ~Cygwin() = default;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace swift

#endif

