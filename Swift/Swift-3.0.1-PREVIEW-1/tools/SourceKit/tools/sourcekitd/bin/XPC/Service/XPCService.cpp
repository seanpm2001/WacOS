//===--- XPCService.cpp ---------------------------------------------------===//
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

#include "sourcekitd/Internal-XPC.h"
#include "sourcekitd/Logging.h"
#include "sourcekitd/XpcTracing.h"

#include "SourceKit/Core/LLVM.h"
#include "SourceKit/Support/Concurrency.h"
#include "SourceKit/Support/UIdent.h"
#include "SourceKit/Support/Logging.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Threading.h"

#include <xpc/xpc.h>

using namespace SourceKit;
using namespace sourcekitd;

static xpc_connection_t MainConnection = nullptr;

void sourcekitd::postNotification(sourcekitd_response_t Notification) {
  xpc_connection_t peer = MainConnection;
  if (!peer)
    goto done;

  {
    xpc_object_t contents = xpc_array_create(nullptr, 0);
    xpc_array_set_uint64(contents, XPC_ARRAY_APPEND,
                         (uint64_t)xpc::Message::Notification);
    xpc_array_set_value(contents, XPC_ARRAY_APPEND, Notification);

    xpc_object_t msg = xpc_dictionary_create(nullptr, nullptr, 0);
    xpc_dictionary_set_value(msg, xpc::KeyInternalMsg, contents);
    xpc_release(contents);

    xpc_connection_send_message(peer, msg);
    xpc_release(msg);
  }

done:
  // The function accepted ownership.
  xpc_release(Notification);
}

namespace {
/// \brief Associates sourcekitd_uid_t to a UIdent.
class SKUIDToUIDMap {
  typedef llvm::DenseMap<void *, UIdent> MapTy;
  MapTy Map;
  WorkQueue Queue{ WorkQueue::Dequeuing::Concurrent, "UIDMap" };

public:
  UIdent get(sourcekitd_uid_t SKDUID);
  void set(sourcekitd_uid_t SKDUID, UIdent UID);
};
}

static SKUIDToUIDMap UIDMap;

sourcekitd_uid_t sourcekitd::SKDUIDFromUIdent(UIdent UID) {
  if (void *Tag = UID.getTag())
    return reinterpret_cast<sourcekitd_uid_t>(Tag);

  // FIXME: The following should run in the synchronous dispatch queue of the
  // connection. But does it matter, since if MainConnection is null or gets
  // destroyed it means the client crashed ?
  xpc_connection_t peer = MainConnection;
  if (!peer)
    return nullptr;

  xpc_object_t contents = xpc_array_create(nullptr, 0);
  xpc_array_set_uint64(contents, XPC_ARRAY_APPEND,
                       (uint64_t)xpc::Message::UIDSynchronization);
  xpc_array_set_string(contents, XPC_ARRAY_APPEND, UID.c_str());

  xpc_object_t msg = xpc_dictionary_create(nullptr, nullptr,  0);
  xpc_dictionary_set_value(msg, xpc::KeyInternalMsg, contents);
  xpc_release(contents);

  xpc_object_t reply = xpc_connection_send_message_with_reply_sync(peer, msg);
  xpc_release(msg);
  if (xpc_get_type(reply) == XPC_TYPE_ERROR) {
    xpc_release(reply);
    return nullptr;
  }

  assert(xpc_get_type(reply) == XPC_TYPE_DICTIONARY);
  uint64_t val = xpc_dictionary_get_uint64(reply, xpc::KeyMsgResponse);
  xpc_release(reply);

  sourcekitd_uid_t skduid = sourcekitd_uid_t(val);
  UID.setTag(skduid);
  UIDMap.set(skduid, UID);
  return skduid;
}

UIdent sourcekitd::UIdentFromSKDUID(sourcekitd_uid_t SKDUID) {
  // This should be used only for debugging/logging purposes.

  UIdent UID = UIDMap.get(SKDUID);
  if (UID.isValid())
    return UID;

  xpc_connection_t Peer = MainConnection;
  if (!Peer)
    return UIdent();

  xpc_object_t contents = xpc_array_create(nullptr, 0);
  xpc_array_set_uint64(contents, XPC_ARRAY_APPEND,
                       (uint64_t)xpc::Message::UIDSynchronization);
  xpc_array_set_uint64(contents, XPC_ARRAY_APPEND, uintptr_t(SKDUID));

  xpc_object_t msg = xpc_dictionary_create(nullptr, nullptr,  0);
  xpc_dictionary_set_value(msg, xpc::KeyInternalMsg, contents);
  xpc_release(contents);

  xpc_object_t reply = xpc_connection_send_message_with_reply_sync(Peer, msg);
  xpc_release(msg);
  if (xpc_get_type(reply) == XPC_TYPE_ERROR) {
    xpc_release(reply);
    return UIdent();
  }

  assert(xpc_get_type(reply) == XPC_TYPE_DICTIONARY);
  const char *Str = xpc_dictionary_get_string(reply, xpc::KeyMsgResponse);

  UID = UIdent(Str);
  UID.setTag(SKDUID);
  UIDMap.set(SKDUID, UID);

  xpc_release(reply);
  return UID;
}

void anchorForGetMainExecutableInXPCService() {}

namespace {
/// Responsible for replying to an XPC request.
class XPCResponder {
  xpc_connection_t Peer;
  xpc_object_t Event;
  bool Responded = false;

public:
  XPCResponder(xpc_object_t event, xpc_connection_t peer)
    : Peer(xpc_connection_t(xpc_retain(peer))), Event(xpc_retain(event)) {}

  ~XPCResponder() {
    if (!Responded) {
      LOG_WARN_FUNC("failed to respond to request");
      sendReply(createErrorRequestFailed("Internal error: no response was "
                                         "provided for the request"));
    }
    xpc_release(Event);
    xpc_release(Peer);
  }

  /// Accepts ownership of the response object.
  void sendReply(sourcekitd_response_t response) {
    if (Responded) {
      LOG_WARN_FUNC("tried to respond to an already handled request");
      return;
    }

    xpc_object_t reply = xpc_dictionary_create_reply(Event);
    xpc_dictionary_set_value(reply, xpc::KeyMsgResponse, response);
    xpc_release(response);

    xpc_connection_send_message(Peer, reply);
    xpc_release(reply);
    Responded = true;
  }
};
}

std::string sourcekitd::getRuntimeLibPath() {
  std::string MainExePath = llvm::sys::fs::getMainExecutable("sourcekit",
             reinterpret_cast<void *>(&anchorForGetMainExecutableInXPCService));
#ifdef SOURCEKIT_UNVERSIONED_FRAMEWORK_BUNDLE
  // MainExePath points to "lib/sourcekitd.framework/XPCServices/
  //                       SourceKitService.xpc/SourceKitService"
  const unsigned MainExeLibNestingLevel = 4;
#else
  // MainExePath points to "lib/sourcekitd.framework/Versions/Current/XPCServices/
  //                       SourceKitService.xpc/Contents/MacOS/SourceKitService"
  const unsigned MainExeLibNestingLevel = 8;
#endif

  // Get it to lib.
  StringRef Path = MainExePath;
  for (unsigned i = 0; i < MainExeLibNestingLevel; ++i)
    Path = llvm::sys::path::parent_path(Path);
  return Path;
}

static void sourcekitdServer_peer_event_handler(xpc_connection_t peer,
                                            xpc_object_t event) {
  xpc_type_t type = xpc_get_type(event);
  if (type == XPC_TYPE_ERROR) {
    if (event == XPC_ERROR_CONNECTION_INVALID) {
      // The client process on the other end of the connection has either
      // crashed or cancelled the connection. After receiving this error,
      // the connection is in an invalid state, and we do not need to
      // call xpc_connection_cancel().
      // No need to call sourcekitd::shutdown() since the process is going down
      // anyway, plus if we get a new connection before the process closes then
      // we will fail to re-initialize properly since the initialize call is at
      // main.
      xpc_transaction_end();
    } else if (event == XPC_ERROR_TERMINATION_IMMINENT) {
      // Handle per-connection termination cleanup.
      xpc_connection_cancel(peer);
    }

  } else {
    assert(type == XPC_TYPE_DICTIONARY);
    // Handle the message
    xpc_retain(event);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),
    ^{
      xpc_object_t contents = xpc_dictionary_get_value(event, "msg");

      if (!contents) {
        // Ping back.
        contents = xpc_dictionary_get_value(event, "ping");
        assert(contents && "unexpected message");
        xpc_object_t reply = xpc_dictionary_create_reply(event);
        assert(reply);
        xpc_connection_send_message(peer, reply);
        xpc_release(reply);
        return;
      }

      auto Responder = std::make_shared<XPCResponder>(event, peer);
      xpc_release(event);

      assert(xpc_get_type(contents) == XPC_TYPE_ARRAY);
      sourcekitd_object_t req = xpc_array_get_value(contents, 0);
      sourcekitd::handleRequest(req,
        [Responder](sourcekitd_response_t response) {
          Responder->sendReply(response);
        });
    });
  }
}

static void getInitializationInfo(xpc_connection_t peer) {
  xpc_object_t contents = xpc_array_create(nullptr, 0);
  xpc_array_set_uint64(contents, XPC_ARRAY_APPEND,
                       (uint64_t)xpc::Message::Initialization);

  xpc_object_t msg = xpc_dictionary_create(nullptr, nullptr,  0);
  xpc_dictionary_set_value(msg, xpc::KeyInternalMsg, contents);
  xpc_release(contents);

  xpc_object_t reply = xpc_connection_send_message_with_reply_sync(peer, msg);
  xpc_release(msg);
  if (xpc_get_type(reply) == XPC_TYPE_ERROR) {
    xpc_release(reply);
    return;
  }

  assert(xpc_get_type(reply) == XPC_TYPE_DICTIONARY);
  uint64_t Delay = xpc_dictionary_get_uint64(reply, xpc::KeySemaEditorDelay);
  uint64_t TracingEnabled = xpc_dictionary_get_uint64(reply,
                                                      xpc::KeyTracingEnabled);
  xpc_release(reply);

  if (Delay != 0) {
    llvm::SmallString<4> Buf;
    {
      llvm::raw_svector_ostream OS(Buf);
      OS << Delay;
    }
    setenv("SOURCEKIT_DELAY_SEMA_EDITOR", Buf.c_str(), /*overwrite=*/1);
  }

  if (TracingEnabled) {
    SourceKit::trace::enable();
  }
}

static void sourcekitdServer_event_handler(xpc_connection_t peer) {
  // Keep the service alive even when idle.
  xpc_transaction_begin();

  // By defaults, new connections will target the default dispatch
  // concurrent queue.
  xpc_connection_set_event_handler(peer, ^(xpc_object_t event) {
    sourcekitdServer_peer_event_handler(peer, event);
  });

  MainConnection = peer;

  // This will tell the connection to begin listening for events. If you
  // have some other initialization that must be done asynchronously, then
  // you can defer this call until after that initialization is done.
  xpc_connection_resume(peer);

  dispatch_async(dispatch_get_main_queue(), ^{
    getInitializationInfo(MainConnection);
  });
}

static void fatal_error_handler(void *user_data, const std::string& reason,
                                bool gen_crash_diag) {
  // Write the result out to stderr avoiding errs() because raw_ostreams can
  // call report_fatal_error.
  fprintf(stderr, "SOURCEKITD SERVER FATAL ERROR: %s\n", reason.c_str());
  ::abort();
}

int main(int argc, const char *argv[]) {
  llvm::install_fatal_error_handler(fatal_error_handler, 0);
  sourcekitd::enableLogging("sourcekit-serv");
  sourcekitd::initialize();
  sourcekitd::trace::initialize();

  // Increase the file descriptor limit.
  // FIXME: Portability ?
  static const size_t FDLimit = 4096;
  struct rlimit l;
  if (getrlimit(RLIMIT_NOFILE, &l) == 0) {
    if (l.rlim_cur < FDLimit) {
      l.rlim_cur = FDLimit;
      if (setrlimit(RLIMIT_NOFILE, &l) == 0) {
        LOG_INFO_FUNC(Low, "bumped file descriptor limit to " << FDLimit);
      } else {
        LOG_WARN_FUNC("setrlimit failed: " << llvm::sys::StrError());
      }
    }
  } else {
    LOG_WARN_FUNC("getrlimit failed: " << llvm::sys::StrError());
  }

  xpc_main(sourcekitdServer_event_handler);
  return 0;
}

UIdent SKUIDToUIDMap::get(sourcekitd_uid_t SKDUID) {
  UIdent UID;
  Queue.dispatchSync([&]{
    MapTy::iterator It = Map.find(SKDUID);
    if (It != Map.end())
      UID = It->second;
  });

  return UID;
}

void SKUIDToUIDMap::set(sourcekitd_uid_t SKDUID, UIdent UID) {
  Queue.dispatchBarrier([=]{
    this->Map[SKDUID] = UID;
  });
}

void sourcekitd::trace::sendTraceMessage(trace::sourcekitd_trace_message_t Msg) {
  if (!SourceKit::trace::enabled()) {
    xpc_release(Msg);
    return;
  }

  xpc_connection_t Peer = MainConnection;
  if (!Peer) {
    SourceKit::trace::disable();
    xpc_release(Msg);
    return;
  }

  xpc_object_t Contents = xpc_array_create(nullptr, 0);
  xpc_array_set_uint64(Contents, XPC_ARRAY_APPEND,
                       static_cast<uint64_t>(xpc::Message::TraceMessage));
  xpc_array_set_uint64(Contents, XPC_ARRAY_APPEND,
                       trace::getTracingSession());
  xpc_array_set_value(Contents, XPC_ARRAY_APPEND, Msg);
  xpc_release(Msg);

  xpc_object_t Message = xpc_dictionary_create(nullptr, nullptr,  0);
  xpc_dictionary_set_value(Message, xpc::KeyInternalMsg, Contents);
  xpc_release(Contents);

  xpc_object_t Reply =
    xpc_connection_send_message_with_reply_sync(Peer, Message);
  xpc_release(Message);

  if (xpc_get_type(Reply) == XPC_TYPE_ERROR) {
    SourceKit::trace::disable();
    xpc_release(Reply);
    return;
  }

  xpc_release(Reply);
}

