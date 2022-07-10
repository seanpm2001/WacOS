//===--- NotificationCenter.cpp -------------------------------------------===//
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

#include "SourceKit/Core/NotificationCenter.h"
#include "SourceKit/Support/Concurrency.h"

using namespace SourceKit;

void NotificationCenter::addDocumentUpdateNotificationReceiver(
    DocumentUpdateNotificationReceiver Receiver) {

  WorkQueue::dispatchOnMain([this, Receiver]{
    DocUpdReceivers.push_back(Receiver);
  });
}

void NotificationCenter::postDocumentUpdateNotification(
    StringRef DocumentName) const {
  
  std::string DocName = DocumentName;
  WorkQueue::dispatchOnMain([this, DocName]{
    for (auto &Fn : DocUpdReceivers)
      Fn(DocName);
  });
}
