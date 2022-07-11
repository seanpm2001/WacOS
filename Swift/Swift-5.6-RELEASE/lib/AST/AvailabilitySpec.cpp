//===--- AvailabilitySpec.cpp - Swift Availability Query ASTs -------------===//
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
//
// This file implements the availability specification AST classes.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTContext.h"
#include "swift/AST/AvailabilitySpec.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;

SourceRange AvailabilitySpec::getSourceRange() const {
  switch (getKind()) {
  case AvailabilitySpecKind::PlatformVersionConstraint:
    return cast<PlatformVersionConstraintAvailabilitySpec>(this)->getSourceRange();

 case AvailabilitySpecKind::LanguageVersionConstraint:
 case AvailabilitySpecKind::PackageDescriptionVersionConstraint:
   return cast<PlatformAgnosticVersionConstraintAvailabilitySpec>(this)->getSourceRange();

  case AvailabilitySpecKind::OtherPlatform:
    return cast<OtherPlatformAvailabilitySpec>(this)->getSourceRange();
  }
  llvm_unreachable("bad AvailabilitySpecKind");
}

SourceRange PlatformVersionConstraintAvailabilitySpec::getSourceRange() const {
  return SourceRange(PlatformLoc, VersionSrcRange.End);
}

void PlatformVersionConstraintAvailabilitySpec::print(raw_ostream &OS,
                                              unsigned Indent) const {
  OS.indent(Indent) << '(' << "platform_version_constraint_availability_spec"
                    << " platform='" << platformString(getPlatform()) << "'"
                    << " version='" << getVersion() << "'"
                    << ')';
}

SourceRange PlatformAgnosticVersionConstraintAvailabilitySpec::getSourceRange() const {
  return SourceRange(PlatformAgnosticNameLoc, VersionSrcRange.End);
}

void PlatformAgnosticVersionConstraintAvailabilitySpec::print(raw_ostream &OS,
                                                      unsigned Indent) const {
  OS.indent(Indent) << '('
                    << "platform_agnostic_version_constraint_availability_spec"
                    << " kind='"
                    << (isLanguageVersionSpecific() ?
                         "swift" : "package_description")
                    << "'"
                    << " version='" << getVersion() << "'"
                    << ')';
}

void OtherPlatformAvailabilitySpec::print(raw_ostream &OS, unsigned Indent) const {
  OS.indent(Indent) << '(' << "other_constraint_availability_spec"
                    << " "
                    << ')';
}
