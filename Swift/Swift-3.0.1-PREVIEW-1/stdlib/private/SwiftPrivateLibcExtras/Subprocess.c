//===--- Subprocess.c - Subprocess Stubs ----------------------------------===//
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

// posix_spawn is not available on Android or Windows (MSVC).
#if !defined(__ANDROID__) && (!defined(_WIN32) || defined(__CYGWIN__))

#include "swift/Runtime/Config.h"

// NOTE: preprocess away the availability information to allow use of
// unsupported APIs on certain targets (i.e. tvOS)
#define availability(...)
#include <spawn.h>
#include <sys/types.h>
#if defined(__APPLE__)
// NOTE: forward declare this rather than including crt_externs.h as not all
// SDKs provide it
extern char ***_NSGetEnviron(void);
#endif // defined(__APPLE__)

SWIFT_CC(swift)
int swift_posix_spawn_file_actions_init(
    posix_spawn_file_actions_t *file_actions) {
  return posix_spawn_file_actions_init(file_actions);
}

SWIFT_CC(swift)
int swift_posix_spawn_file_actions_destroy(
    posix_spawn_file_actions_t *file_actions) {
  return posix_spawn_file_actions_destroy(file_actions);
}

SWIFT_CC(swift)
int swift_posix_spawn_file_actions_addclose(
    posix_spawn_file_actions_t *file_actions, int filedes) {
  return posix_spawn_file_actions_addclose(file_actions, filedes);
}

SWIFT_CC(swift)
int swift_posix_spawn_file_actions_adddup2(
    posix_spawn_file_actions_t *file_actions, int filedes, int newfiledes) {
  return posix_spawn_file_actions_adddup2(file_actions, filedes, newfiledes);
}

SWIFT_CC(swift)
int swift_posix_spawn(pid_t *__restrict pid, const char * __restrict path,
                      const posix_spawn_file_actions_t *file_actions,
                      const posix_spawnattr_t *__restrict attrp,
                      char *const argv[__restrict],
                      char *const envp[__restrict]) {
  return posix_spawn(pid, path, file_actions, attrp, argv, envp);
}

#if defined(__APPLE__)
SWIFT_CC(swift)
char ***swift_SwiftPrivateLibcExtras_NSGetEnviron(void) {
  return _NSGetEnviron();
}
#endif // defined(__APPLE__)
#endif // !defined(__ANDROID__) && (!defined(_WIN32) || defined(__CGYWIN__))

