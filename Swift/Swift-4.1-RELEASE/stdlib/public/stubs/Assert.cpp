//===--- Assert.cpp - Assertion failure reporting -------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/Runtime/Config.h"
#include "swift/Runtime/Debug.h"
#include "../SwiftShims/AssertionReporting.h"
#include <cstdarg>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

using namespace swift;

bool swift::_swift_reportFatalErrorsToDebugger = true;

static int swift_asprintf(char **strp, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
#if defined(_WIN32)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
  int len = _vscprintf(fmt, args);
#pragma GCC diagnostic pop
  if (len < 0) {
    va_end(args);
    return -1;
  }
  char *buffer = static_cast<char *>(malloc(len + 1));
  if (!buffer) {
    va_end(args);
    return -1;
  }
  int result = vsprintf(buffer, fmt, args);
  if (result < 0) {
    va_end(args);
    free(buffer);
    return -1;
  }
  *strp = buffer;
#else
  int result = vasprintf(strp, fmt, args);
#endif
  va_end(args);
  return result;
}

static void logPrefixAndMessageToDebugger(
    const unsigned char *prefix, int prefixLength,
    const unsigned char *message, int messageLength
) {
  if (!_swift_reportFatalErrorsToDebugger)
    return;

  char *debuggerMessage;
  if (messageLength) {
    swift_asprintf(&debuggerMessage, "%.*s: %.*s", prefixLength, prefix,
        messageLength, message);
  } else {
    swift_asprintf(&debuggerMessage, "%.*s", prefixLength, prefix);
  }
  _swift_reportToDebugger(RuntimeErrorFlagFatal, debuggerMessage);
  free(debuggerMessage);
}

void swift::_swift_stdlib_reportFatalErrorInFile(
    const unsigned char *prefix, int prefixLength,
    const unsigned char *message, int messageLength,
    const unsigned char *file, int fileLength,
    uint32_t line,
    uint32_t flags
) {
  logPrefixAndMessageToDebugger(prefix, prefixLength, message, messageLength);

  char *log;
  swift_asprintf(
      &log, "%.*s: %.*s%sfile %.*s, line %" PRIu32 "\n",
      prefixLength, prefix,
      messageLength, message,
      (messageLength ? ": " : ""),
      fileLength, file,
      line);

  swift_reportError(flags, log);
  free(log);
}

void swift::_swift_stdlib_reportFatalError(
    const unsigned char *prefix, int prefixLength,
    const unsigned char *message, int messageLength,
    uint32_t flags
) {
  logPrefixAndMessageToDebugger(prefix, prefixLength, message, messageLength);

  char *log;
  swift_asprintf(
      &log, "%.*s: %.*s\n",
      prefixLength, prefix,
      messageLength, message);

  swift_reportError(flags, log);
  free(log);
}

void swift::_swift_stdlib_reportUnimplementedInitializerInFile(
    const unsigned char *className, int classNameLength,
    const unsigned char *initName, int initNameLength,
    const unsigned char *file, int fileLength,
    uint32_t line, uint32_t column,
    uint32_t flags
) {
  char *log;
  swift_asprintf(
      &log,
      "%.*s: %" PRIu32 ": %" PRIu32 ": Fatal error: Use of unimplemented "
      "initializer '%.*s' for class '%.*s'\n",
      fileLength, file,
      line, column,
      initNameLength, initName,
      classNameLength, className);

  swift_reportError(flags, log);
  free(log);
}

void swift::_swift_stdlib_reportUnimplementedInitializer(
    const unsigned char *className, int classNameLength,
    const unsigned char *initName, int initNameLength,
    uint32_t flags
) {
  char *log;
  swift_asprintf(
      &log,
      "Fatal error: Use of unimplemented "
      "initializer '%.*s' for class '%.*s'\n",
      initNameLength, initName,
      classNameLength, className);

  swift_reportError(flags, log);
  free(log);
}

