//===--- Platform.cpp - Implement platform-related helpers ----------------===//
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

#include "swift/Basic/Platform.h"
#include "llvm/ADT/Triple.h"

using namespace swift;

bool swift::tripleIsiOSSimulator(const llvm::Triple &triple) {
  llvm::Triple::ArchType arch = triple.getArch();
  return (triple.isiOS() &&
          (arch == llvm::Triple::x86 || arch == llvm::Triple::x86_64));
}

bool swift::tripleIsAppleTVSimulator(const llvm::Triple &triple) {
  llvm::Triple::ArchType arch = triple.getArch();
  return (triple.isTvOS() &&
         (arch == llvm::Triple::x86 || arch == llvm::Triple::x86_64));
}

bool swift::tripleIsWatchSimulator(const llvm::Triple &triple) {
  llvm::Triple::ArchType arch = triple.getArch();
  return (triple.isWatchOS() &&
         (arch == llvm::Triple::x86 || arch == llvm::Triple::x86_64));
}

bool swift::tripleIsAnySimulator(const llvm::Triple &triple) {
  return tripleIsiOSSimulator(triple) ||
      tripleIsWatchSimulator(triple) ||
      tripleIsAppleTVSimulator(triple);
}

DarwinPlatformKind swift::getDarwinPlatformKind(const llvm::Triple &triple) {
  if (triple.isiOS()) {
    if (triple.isTvOS()) {
      if (tripleIsAppleTVSimulator(triple))
        return DarwinPlatformKind::TvOSSimulator;
      return DarwinPlatformKind::TvOS;
    }

    if (tripleIsiOSSimulator(triple))
      return DarwinPlatformKind::IPhoneOSSimulator;
    return DarwinPlatformKind::IPhoneOS;
  }

  if (triple.isWatchOS()) {
    if (tripleIsWatchSimulator(triple))
      return DarwinPlatformKind::WatchOSSimulator;
    return DarwinPlatformKind::WatchOS;
  }

  if (triple.isMacOSX())
    return DarwinPlatformKind::MacOS;

  llvm_unreachable("Unsupported Darwin platform");
}

static StringRef getPlatformNameForDarwin(const DarwinPlatformKind platform) {
  switch (platform) {
  case DarwinPlatformKind::MacOS:
    return "macosx";
  case DarwinPlatformKind::IPhoneOS:
    return "iphoneos";
  case DarwinPlatformKind::IPhoneOSSimulator:
    return "iphonesimulator";
  case DarwinPlatformKind::TvOS:
    return "appletvos";
  case DarwinPlatformKind::TvOSSimulator:
    return "appletvsimulator";
  case DarwinPlatformKind::WatchOS:
    return "watchos";
  case DarwinPlatformKind::WatchOSSimulator:
    return "watchsimulator";
  }
  llvm_unreachable("Unsupported Darwin platform");
}

StringRef swift::getPlatformNameForTriple(const llvm::Triple &triple) {
  switch (triple.getOS()) {
  case llvm::Triple::UnknownOS:
    llvm_unreachable("unknown OS");
  case llvm::Triple::CloudABI:
  case llvm::Triple::DragonFly:
  case llvm::Triple::KFreeBSD:
  case llvm::Triple::Lv2:
  case llvm::Triple::NetBSD:
  case llvm::Triple::OpenBSD:
  case llvm::Triple::Solaris:
  case llvm::Triple::Haiku:
  case llvm::Triple::Minix:
  case llvm::Triple::RTEMS:
  case llvm::Triple::NaCl:
  case llvm::Triple::CNK:
  case llvm::Triple::Bitrig:
  case llvm::Triple::AIX:
  case llvm::Triple::CUDA:
  case llvm::Triple::NVCL:
  case llvm::Triple::AMDHSA:
  case llvm::Triple::ELFIAMCU:
    return "";
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS:
  case llvm::Triple::WatchOS:
    return getPlatformNameForDarwin(getDarwinPlatformKind(triple));
  case llvm::Triple::Linux:
    return triple.isAndroid() ? "android" : "linux";
  case llvm::Triple::FreeBSD:
    return "freebsd";
  case llvm::Triple::Win32:
    return "windows";
  case llvm::Triple::PS4:
    return "ps4";
  }
  llvm_unreachable("unsupported OS");
}

StringRef swift::getMajorArchitectureName(const llvm::Triple &Triple) {
  if (Triple.isOSLinux()) {
    switch(Triple.getSubArch()) {
    default:
      return Triple.getArchName();
      break;
    case llvm::Triple::SubArchType::ARMSubArch_v7:
      return "armv7";
      break;
    case llvm::Triple::SubArchType::ARMSubArch_v6:
      return "armv6";
      break;
    }
  } else {
    return Triple.getArchName();
  }
}
