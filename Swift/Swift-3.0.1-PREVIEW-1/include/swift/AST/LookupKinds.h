//===--- LookupKinds.h - Swift name-lookup enums ----------------*- C++ -*-===//
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
//
// This file defines enums relating to name lookup.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_LOOKUPKINDS_H
#define SWIFT_LOOKUPKINDS_H

namespace swift {

/// NLKind - This is a specifier for the kind of name lookup being performed
/// by various query methods.
enum class NLKind {
  UnqualifiedLookup,
  QualifiedLookup
};

/// Constants used to customize name lookup.
enum NLOptions : unsigned {
  /// Visit supertypes (such as superclasses or inherited protocols)
  /// and their extensions as well as the current extension.
  NL_VisitSupertypes = 0x01,

  /// Consider declarations within protocols to which the context type conforms.
  NL_ProtocolMembers = 0x02,

  /// Remove non-visible declarations from the set of results.
  NL_RemoveNonVisible = 0x04,

  /// Remove overridden declarations from the set of results.
  NL_RemoveOverridden = 0x08,

  /// For existentials involving the special \c AnyObject protocol,
  /// allow lookups to find members of all classes.
  NL_DynamicLookup = 0x10,

  /// Don't check accessibility when doing lookup into a type.
  ///
  /// This option is not valid when performing lookup into a module.
  NL_IgnoreAccessibility = 0x20,

  /// This lookup is known to be a non-cascading dependency, i.e. one that does
  /// not affect downstream files.
  ///
  /// \see NL_KnownDependencyMask
  NL_KnownNonCascadingDependency = 0x40,

  /// This lookup is known to be a cascading dependency, i.e. one that can
  /// affect downstream files.
  ///
  /// \see NL_KnownDependencyMask
  NL_KnownCascadingDependency = 0x80,

  /// This lookup should only return type declarations.
  NL_OnlyTypes = 0x100,

  /// This lookup is known to not add any additional dependencies to the
  /// primary source file.
  ///
  /// \see NL_KnownDependencyMask
  NL_KnownNoDependency =
      NL_KnownNonCascadingDependency|NL_KnownCascadingDependency,

  /// A mask of all options controlling how a lookup should be recorded as a
  /// dependency.
  ///
  /// This offers three possible options: NL_KnownNonCascadingDependency,
  /// NL_KnownCascadingDependency, NL_KnownNoDependency, as well as a default
  /// "unspecified" value (0). If the dependency kind is unspecified, the
  /// lookup function will attempt to infer whether it is a cascading or
  /// non-cascading dependency from the decl context.
  NL_KnownDependencyMask = NL_KnownNoDependency,

  /// The default set of options used for qualified name lookup.
  ///
  /// FIXME: Eventually, add NL_ProtocolMembers to this, once all of the
  /// callers can handle it.
  NL_QualifiedDefault = NL_VisitSupertypes | NL_RemoveNonVisible |
                        NL_RemoveOverridden,

  /// The default set of options used for unqualified name lookup.
  NL_UnqualifiedDefault = NL_VisitSupertypes |
                          NL_RemoveNonVisible | NL_RemoveOverridden
};

static inline NLOptions operator|(NLOptions lhs, NLOptions rhs) {
  return NLOptions(unsigned(lhs) | unsigned(rhs));
}
static inline NLOptions &operator|=(NLOptions &lhs, NLOptions rhs) {
  return (lhs = lhs | rhs);
}
static inline NLOptions operator&(NLOptions lhs, NLOptions rhs) {
  return NLOptions(unsigned(lhs) & unsigned(rhs));
}
static inline NLOptions &operator&=(NLOptions &lhs, NLOptions rhs) {
  return (lhs = lhs & rhs);
}
static inline NLOptions operator~(NLOptions value) {
  return NLOptions(~(unsigned)value);
}

} // end namespace swift

#endif
