//===--- ImportName.cpp - Imported Swift names for Clang decls ------------===//
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
// This file provides class definitions for naming-related concerns in the
// ClangImporter.
//
//===----------------------------------------------------------------------===//

#include "CFTypeInfo.h"
#include "IAMInference.h"
#include "ImporterImpl.h"
#include "ClangDiagnosticConsumer.h"
#include "swift/Subsystems.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsClangImporter.h"
#include "swift/AST/Module.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/Types.h"
#include "swift/AST/TypeRepr.h"
#include "swift/Basic/StringExtras.h"
#include "swift/ClangImporter/ClangImporterOptions.h"
#include "swift/Parse/Parser.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/Module.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <memory>

#include "llvm/ADT/Statistic.h"
#define DEBUG_TYPE "Import Name"
STATISTIC(ImportNameNumCacheHits, "# of times the import name cache was hit");
STATISTIC(ImportNameNumCacheMisses, "# of times the import name cache was missed");

using namespace swift;
using namespace importer;

// Commonly-used Clang classes.
using clang::CompilerInstance;
using clang::CompilerInvocation;


/// Determine whether the given Clang selector matches the given
/// selector pieces.
static bool isNonNullarySelector(clang::Selector selector,
                                 ArrayRef<StringRef> pieces) {
  unsigned n = selector.getNumArgs();
  if (n == 0) return false;
  if (n != pieces.size()) return false;

  for (unsigned i = 0; i != n; ++i) {
    if (selector.getNameForSlot(i) != pieces[i]) return false;
  }

  return true;
}

/// Whether we should make a variadic method with the given selector
/// non-variadic.
static bool shouldMakeSelectorNonVariadic(clang::Selector selector) {
  // This is UIActionSheet's designated initializer.
  if (isNonNullarySelector(selector,
                           { "initWithTitle",
                             "delegate",
                             "cancelButtonTitle",
                             "destructiveButtonTitle",
                             "otherButtonTitles" }))
    return true;

  // This is UIAlertView's designated initializer.
  if (isNonNullarySelector(selector,
                           { "initWithTitle",
                             "message",
                             "delegate",
                             "cancelButtonTitle",
                             "otherButtonTitles" }))
    return true;

  // Nothing else for now.
  return false;
}

static bool isBlockParameter(const clang::ParmVarDecl *param) {
  return param->getType()->isBlockPointerType();
}

static bool isErrorOutParameter(const clang::ParmVarDecl *param,
                         ForeignErrorConvention::IsOwned_t &isErrorOwned) {
  clang::QualType type = param->getType();

  // Must be a pointer.
  auto ptrType = type->getAs<clang::PointerType>();
  if (!ptrType) return false;
  type = ptrType->getPointeeType();

  // For NSError**, take ownership from the qualifier.
  if (auto objcPtrType = type->getAs<clang::ObjCObjectPointerType>()) {
    auto iface = objcPtrType->getInterfaceDecl();
    if (iface && iface->getName() == "NSError") {
      switch (type.getObjCLifetime()) {
      case clang::Qualifiers::OCL_None:
        llvm_unreachable("not in ARC?");

      case clang::Qualifiers::OCL_ExplicitNone:
      case clang::Qualifiers::OCL_Autoreleasing:
        isErrorOwned = ForeignErrorConvention::IsNotOwned;
        return true;

      case clang::Qualifiers::OCL_Weak:
        // We just don't know how to handle this.
        return false;

      case clang::Qualifiers::OCL_Strong:
        isErrorOwned = ForeignErrorConvention::IsOwned;
        return false;
      }
      llvm_unreachable("bad error ownership");
    }
  }
  return false;
}

static bool isBoolType(clang::ASTContext &ctx, clang::QualType type) {
  do {
    // Check whether we have a typedef for "BOOL" or "Boolean".
    if (auto typedefType = dyn_cast<clang::TypedefType>(type.getTypePtr())) {
      auto typedefDecl = typedefType->getDecl();
      if (typedefDecl->getName() == "BOOL" ||
          typedefDecl->getName() == "Boolean")
        return true;

      type = typedefDecl->getUnderlyingType();
      continue;
    }

    // Try to desugar one level...
    clang::QualType desugared = type.getSingleStepDesugaredType(ctx);
    if (desugared.getTypePtr() == type.getTypePtr())
      break;

    type = desugared;
  } while (!type.isNull());

  return false;
}

static bool isIntegerType(clang::QualType clangType) {
  if (auto builtinTy = clangType->getAs<clang::BuiltinType>()) {
    return (builtinTy->getKind() >= clang::BuiltinType::Bool &&
            builtinTy->getKind() <= clang::BuiltinType::UInt128) ||
           (builtinTy->getKind() >= clang::BuiltinType::SChar &&
            builtinTy->getKind() <= clang::BuiltinType::Int128);
  }

  return false;
}

/// Whether the given Objective-C type can be imported as an optional type.
static bool canImportAsOptional(clang::ASTContext &ctx, clang::QualType type) {
  // Note: this mimics ImportHint::canImportAsOptional.

  // Objective-C object pointers.
  if (type->getAs<clang::ObjCObjectPointerType>()) return true;

  // Block and C pointers, including CF types.
  if (type->isBlockPointerType() || type->isPointerType()) return true;

  return false;
}

static Optional<ForeignErrorConvention::Kind>
classifyMethodErrorHandling(const clang::ObjCMethodDecl *clangDecl,
                            OptionalTypeKind resultOptionality) {
  // TODO: opt out any non-standard methods here?
  clang::ASTContext &clangCtx = clangDecl->getASTContext();

  // Check for an explicit attribute.
  if (auto attr = clangDecl->getAttr<clang::SwiftErrorAttr>()) {
    switch (attr->getConvention()) {
    case clang::SwiftErrorAttr::None:
      return None;

    case clang::SwiftErrorAttr::NonNullError:
      return ForeignErrorConvention::NonNilError;

    // Only honor null_result if we actually imported as a
    // non-optional type.
    case clang::SwiftErrorAttr::NullResult:
      if (resultOptionality != OTK_None &&
          canImportAsOptional(clangCtx, clangDecl->getReturnType()))
        return ForeignErrorConvention::NilResult;
      return None;

    // Preserve the original result type on a zero_result unless we
    // imported it as Bool.
    case clang::SwiftErrorAttr::ZeroResult:
      if (isBoolType(clangCtx, clangDecl->getReturnType())) {
        return ForeignErrorConvention::ZeroResult;
      } else if (isIntegerType(clangDecl->getReturnType())) {
        return ForeignErrorConvention::ZeroPreservedResult;
      }
      return None;

    // There's no reason to do the same for nonzero_result because the
    // only meaningful value remaining would be zero.
    case clang::SwiftErrorAttr::NonZeroResult:
      if (isIntegerType(clangDecl->getReturnType()))
        return ForeignErrorConvention::NonZeroResult;
      return None;
    }
    llvm_unreachable("bad swift_error kind");
  }

  // Otherwise, apply the default rules.

  // For bool results, a zero value is an error.
  if (isBoolType(clangCtx, clangDecl->getReturnType())) {
    return ForeignErrorConvention::ZeroResult;
  }

  // For optional reference results, a nil value is normally an error.
  if (resultOptionality != OTK_None &&
      canImportAsOptional(clangCtx, clangDecl->getReturnType())) {
    return ForeignErrorConvention::NilResult;
  }

  return None;
}

static const char ErrorSuffix[] = "AndReturnError";
static const char AltErrorSuffix[] = "WithError";

/// Determine the optionality of the given Objective-C method.
///
/// \param method The Clang method.
static OptionalTypeKind getResultOptionality(
                          const clang::ObjCMethodDecl *method) {
  auto &clangCtx = method->getASTContext();

  // If nullability is available on the type, use it.
  if (auto nullability = method->getReturnType()->getNullability(clangCtx)) {
    return translateNullability(*nullability);
  }

  // If there is a returns_nonnull attribute, non-null.
  if (method->hasAttr<clang::ReturnsNonNullAttr>())
    return OTK_None;

  // Default to implicitly unwrapped optionals.
  return OTK_ImplicitlyUnwrappedOptional;
}

/// \brief Determine whether the given name is reserved for Swift.
static bool isSwiftReservedName(StringRef name) {
  tok kind = Lexer::kindOfIdentifier(name, /*InSILMode=*/false);
  return (kind != tok::identifier);
}

/// Determine whether we should lowercase the first word of the given value
/// name.
static bool shouldLowercaseValueName(StringRef name) {
  // If we see any lowercase characters, we can lowercase.
  for (auto c : name) {
    if (clang::isLowercase(c)) return true;
  }

  // Otherwise, lowercasing will either be a no-op or we have ALL_CAPS.
  return false;
}

/// Will recursively print out the fully qualified context for the given name.
/// Ends with a trailing "."
static void printFullContextPrefix(ImportedName name, ImportNameVersion version,
                                   llvm::raw_ostream &os,
                                   ClangImporter::Implementation &Impl) {
  const clang::NamedDecl *newDeclContextNamed = nullptr;
  switch (name.getEffectiveContext().getKind()) {
  case EffectiveClangContext::UnresolvedContext:
    os << name.getEffectiveContext().getUnresolvedName() << ".";
    // And we're done!
    return;

  case EffectiveClangContext::DeclContext: {
    auto namedDecl = dyn_cast<clang::NamedDecl>(
        name.getEffectiveContext().getAsDeclContext());
    if (!namedDecl) {
      // We're done
      return;
    }
    newDeclContextNamed = cast<clang::NamedDecl>(namedDecl);
    break;
  }

  case EffectiveClangContext::TypedefContext:
    newDeclContextNamed = name.getEffectiveContext().getTypedefName();
    break;
  }

  // Now, let's print out the parent
  assert(newDeclContextNamed && "should of been set");
  auto parentName = Impl.importFullName(newDeclContextNamed, version);
  printFullContextPrefix(parentName, version, os, Impl);
  os << parentName.getDeclName() << ".";
}

void ClangImporter::Implementation::printSwiftName(ImportedName name,
                                                   ImportNameVersion version,
                                                   bool fullyQualified,
                                                   llvm::raw_ostream &os) {
  // Property accessors.
  bool isGetter = false;
  bool isSetter = false;
  switch (name.getAccessorKind()) {
  case ImportedAccessorKind::None:
    break;

  case ImportedAccessorKind::PropertyGetter:
  case ImportedAccessorKind::SubscriptGetter:
    os << "getter:";
    isGetter = true;
    break;

  case ImportedAccessorKind::PropertySetter:
  case ImportedAccessorKind::SubscriptSetter:
    os << "setter:";
    isSetter = true;
    break;
  }

  if (fullyQualified)
    printFullContextPrefix(name, version, os, *this);

  // Base name.
  os << name.getDeclName().getBaseName();

  // Determine the number of argument labels we'll be producing.
  auto argumentNames = name.getDeclName().getArgumentNames();
  unsigned numArguments = argumentNames.size();
  if (name.getSelfIndex()) ++numArguments;
  if (isSetter) ++numArguments;

  // If the result is a simple name that is not a getter, we're done.
  if (numArguments == 0 && name.getDeclName().isSimpleName() && !isGetter)
    return;

  // We need to produce a function name.
  os << "(";
  unsigned currentArgName = 0;
  for (unsigned i = 0; i != numArguments; ++i) {
    // The "self" parameter.
    if (name.getSelfIndex() && *name.getSelfIndex() == i) {
      os << "self:";
      continue;
    }

    if (currentArgName < argumentNames.size()) {
      if (argumentNames[currentArgName].empty())
        os << "_";
      else
        os << argumentNames[currentArgName].str();
      os << ":";
      ++currentArgName;
      continue;
    }

    // We don't have a name for this argument.
    os << "_:";
  }
  os << ")";
}

/// Retrieve the name of the given Clang declaration context for
/// printing.
static StringRef getClangDeclContextName(const clang::DeclContext *dc) {
  auto type = getClangDeclContextType(dc);
  if (type.isNull()) return StringRef();

  return getClangTypeNameForOmission(dc->getParentASTContext(), type).Name;
}

namespace {
  /// Merge the a set of imported names produced for the overridden
  /// declarations of a given method or property.
  template<typename DeclType>
  void mergeOverriddenNames(ASTContext &ctx,
                            const DeclType *decl,
                            SmallVectorImpl<std::pair<const DeclType *,
                                                      ImportedName>>
                              &overriddenNames) {
    typedef std::pair<const DeclType *, ImportedName> OverriddenName;
    llvm::SmallPtrSet<DeclName, 4> known;
    (void)known.insert(DeclName());
    overriddenNames.erase(
        std::remove_if(overriddenNames.begin(), overriddenNames.end(),
                       [&](OverriddenName overridden) {
                         return !known.insert(overridden.second.getDeclName())
                                     .second;
                       }),
        overriddenNames.end());

    if (overriddenNames.size() < 2)
      return;

    // Complain about inconsistencies.
    std::string nameStr;
    auto method = dyn_cast<clang::ObjCMethodDecl>(decl);
    if (method)
      nameStr = method->getSelector().getAsString();
    else
      nameStr = cast<clang::ObjCPropertyDecl>(decl)->getName().str();
    for (unsigned i = 1, n = overriddenNames.size(); i != n; ++i) {
      ctx.Diags.diagnose(SourceLoc(), diag::inconsistent_swift_name,
                         method == nullptr,
                         nameStr,
                         getClangDeclContextName(decl->getDeclContext()),
                         overriddenNames[0].second,
                         getClangDeclContextName(
                           overriddenNames[0].first->getDeclContext()),
                         overriddenNames[i].second,
                         getClangDeclContextName(
                           overriddenNames[i].first->getDeclContext()));
    }
  }
} // end anonymous namespace

/// Skip a leading 'k' in a 'kConstant' pattern
static StringRef stripLeadingK(StringRef name) {
  if (name.size() >= 2 && name[0] == 'k' &&
      clang::isUppercase(name[1]))
    return name.drop_front(1);
  return name;
}

/// Strips a trailing "Notification", if present. Returns {} if name doesn't end
/// in "Notification", or it there would be nothing left.
StringRef importer::stripNotification(StringRef name) {
  name = stripLeadingK(name);
  StringRef notification = "Notification";
  if (name.size() <= notification.size() || !name.endswith(notification))
    return {};
  return name.drop_back(notification.size());
}

/// Whether the decl is from a module who requested import-as-member inference
static bool moduleIsInferImportAsMember(const clang::NamedDecl *decl,
                                        clang::Sema &clangSema) {
  clang::Module *submodule;
  if (auto m = decl->getImportedOwningModule()) {
    submodule = m;
  } else if (auto m = decl->getLocalOwningModule()) {
    submodule = m;
  } else if (auto m = clangSema.getPreprocessor().getCurrentModule()) {
    submodule = m;
  } else if (auto m = clangSema.getPreprocessor().getCurrentLexerSubmodule()) {
    submodule = m;
  } else {
    return false;
  }

  while (submodule) {
    if (submodule->IsSwiftInferImportAsMember) {
      // HACK HACK HACK: This is a workaround for some module invalidation issue
      // and inconsistency. This will go away soon.
      return submodule->Name == "CoreGraphics";
    }
    submodule = submodule->Parent;
  }

  return false;
}

/// Match the name of the given Objective-C method to its enclosing class name
/// to determine the name prefix that would be stripped if the class method
/// were treated as an initializer.
static Optional<unsigned>
matchFactoryAsInitName(const clang::ObjCMethodDecl *method) {
  // Only class methods can be mapped to initializers in this way.
  if (!method->isClassMethod()) return None;

  // Said class methods must be in an actual class.
  auto objcClass = method->getClassInterface();
  if (!objcClass) return None;

  // See if we can match the class name to the beginning of the first
  // selector piece.
  auto firstPiece = method->getSelector().getNameForSlot(0);
  if (firstPiece.empty())
    return None;
  StringRef firstArgLabel = matchLeadingTypeName(firstPiece,
                                                 objcClass->getName());
  if (firstArgLabel.size() == firstPiece.size())
    return None;

  // FIXME: Factory methods cannot have dummy parameters added for
  // historical reasons.
  if (!firstArgLabel.empty() && method->getSelector().getNumArgs() == 0)
    return None;

  // Return the prefix length.
  return firstPiece.size() - firstArgLabel.size();
}

/// Determine the kind of initializer the given factory method could be mapped
/// to, or produce \c None.
static Optional<CtorInitializerKind>
determineCtorInitializerKind(const clang::ObjCMethodDecl *method) {
  // Determine whether we have a suitable return type.
  if (method->hasRelatedResultType()) {
    // When the factory method has an "instancetype" result type, we
    // can import it as a convenience factory method.
    return CtorInitializerKind::ConvenienceFactory;
  }

  if (auto objcPtr = method->getReturnType()
                       ->getAs<clang::ObjCObjectPointerType>()) {
    auto objcClass = method->getClassInterface();
    if (!objcClass) return None;

    if (objcPtr->getInterfaceDecl() != objcClass) {
      // FIXME: Could allow a subclass here, but the rest of the compiler
      // isn't prepared for that yet.
      return None;
    }

    // Factory initializer.
    return CtorInitializerKind::Factory;
  }

  // Not imported as an initializer.
  return None;
}

namespace {
/// Aggregate struct for the common members of clang::SwiftVersionedAttr and
/// clang::SwiftVersionedRemovalAttr.
///
/// For a SwiftVersionedRemovalAttr, the Attr member will be null.
struct VersionedSwiftNameInfo {
  const clang::SwiftNameAttr *Attr;
  clang::VersionTuple Version;
  bool IsReplacedByActive;
};

/// The action to take upon seeing a particular versioned swift_name annotation.
enum class VersionedSwiftNameAction {
  /// This annotation is not interesting.
  Ignore,
  /// This annotation is better than whatever we have so far.
  Use,
  /// This annotation is better than nothing, but that's all; don't bother
  /// recording its version.
  UseAsFallback,
  /// This annotation itself isn't interesting, but its version shows that the
  /// correct answer is whatever's currently active.
  ResetToActive
};
} // end anonymous namespace

static VersionedSwiftNameAction
checkVersionedSwiftName(VersionedSwiftNameInfo info,
                        clang::VersionTuple bestSoFar,
                        ImportNameVersion requestedVersion) {
  if (!bestSoFar.empty() && bestSoFar <= info.Version)
    return VersionedSwiftNameAction::Ignore;

  if (info.IsReplacedByActive) {
    // We know that there are no versioned names between the active version and
    // a replacement version, because otherwise /that/ name would be active.
    // So if replacement < requested, we want to use the old value that was
    // replaced (but with very low priority), and otherwise we want to use the
    // new value that is now active. (Special case: replacement = 0 means that
    // a header annotation was replaced by an unversioned API notes annotation.)
    if (info.Version.empty() ||
        info.Version.getMajor() >= requestedVersion.majorVersionNumber()) {
      return VersionedSwiftNameAction::ResetToActive;
    }
    if (bestSoFar.empty())
      return VersionedSwiftNameAction::UseAsFallback;
    return VersionedSwiftNameAction::Ignore;
  }

  if (info.Version.getMajor() < requestedVersion.majorVersionNumber())
    return VersionedSwiftNameAction::Ignore;
  return VersionedSwiftNameAction::Use;
}


static const clang::SwiftNameAttr *
findSwiftNameAttr(const clang::Decl *decl, ImportNameVersion version) {
#ifndef NDEBUG
  if (Optional<const clang::Decl *> def = getDefinitionForClangTypeDecl(decl)) {
    assert((*def == nullptr || *def == decl) &&
           "swift_name should only appear on the definition");
  }
#endif

  if (version == ImportNameVersion::raw())
    return nullptr;

  // Handle versioned API notes for Swift 3 and later. This is the common case.
  if (version > ImportNameVersion::swift2()) {
    const auto *activeAttr = decl->getAttr<clang::SwiftNameAttr>();
    const clang::SwiftNameAttr *result = activeAttr;
    clang::VersionTuple bestSoFar;
    for (auto *attr : decl->attrs()) {
      VersionedSwiftNameInfo info;

      if (auto *versionedAttr = dyn_cast<clang::SwiftVersionedAttr>(attr)) {
        auto *added =
          dyn_cast<clang::SwiftNameAttr>(versionedAttr->getAttrToAdd());
        if (!added)
          continue;

        info = {added, versionedAttr->getVersion(),
                versionedAttr->getIsReplacedByActive()};

      } else if (auto *removeAttr =
                   dyn_cast<clang::SwiftVersionedRemovalAttr>(attr)) {
        if (removeAttr->getAttrKindToRemove() != clang::attr::SwiftName)
          continue;
        info = {nullptr, removeAttr->getVersion(),
                removeAttr->getIsReplacedByActive()};

      } else {
        continue;
      }

      switch (checkVersionedSwiftName(info, bestSoFar, version)) {
      case VersionedSwiftNameAction::Ignore:
        continue;
      case VersionedSwiftNameAction::Use:
        result = info.Attr;
        bestSoFar = info.Version;
        break;
      case VersionedSwiftNameAction::UseAsFallback:
        // HACK: If there's a swift_name attribute in the headers /and/ in the
        // unversioned API notes /and/ in the active versioned API notes, there
        // will be two "replacement" attributes, one for each of the first two
        // cases. Prefer the first one we see, because that turns out to be the
        // one from the API notes, which matches the semantics when there are no
        // versioned API notes. (This isn't very principled but there's at least
        // a test to tell us if it changes.)
        if (result == activeAttr)
          result = info.Attr;
        assert(bestSoFar.empty());
        break;
      case VersionedSwiftNameAction::ResetToActive:
        result = activeAttr;
        bestSoFar = info.Version;
        break;
      }
    }

    return result;
  }

  // The remainder of this function emulates the limited form of swift_name
  // supported in Swift 2.
  auto attr = decl->getAttr<clang::SwiftNameAttr>();
  if (!attr) return nullptr;

  // API notes produce implicit attributes; ignore them because they weren't
  // used for naming in Swift 2.
  if (attr->isImplicit()) return nullptr;

  // Hardcode certain kinds of explicitly-written Swift names that were
  // permitted and used in Swift 2. All others are ignored, so that we are
  // assuming a more direct translation from the Objective-C APIs into Swift.

  if (auto enumerator = dyn_cast<clang::EnumConstantDecl>(decl)) {
    // Foundation's NSXMLDTDKind had an explicit swift_name attribute in
    // Swift 2. Honor it.
    if (enumerator->getName() == "NSXMLDTDKind") return attr;
    return nullptr;
  }

  if (auto method = dyn_cast<clang::ObjCMethodDecl>(decl)) {
    // Special case: mapping to an initializer.
    if (attr->getName().startswith("init(")) {
      // If we have a class method, honor the annotation to turn a class
      // method into an initializer.
      if (method->isClassMethod()) return attr;

      return nullptr;
    }

    // Special case: preventing a mapping to an initializer.
    if (matchFactoryAsInitName(method) && determineCtorInitializerKind(method))
      return attr;

    return nullptr;
  }

  return nullptr;
}

/// Determine whether the given class method should be imported as
/// an initializer.
static FactoryAsInitKind
getFactoryAsInit(const clang::ObjCInterfaceDecl *classDecl,
                 const clang::ObjCMethodDecl *method,
                 ImportNameVersion version) {
  if (auto *customNameAttr = findSwiftNameAttr(method, version)) {
    if (customNameAttr->getName().startswith("init("))
      return FactoryAsInitKind::AsInitializer;
    else
      return FactoryAsInitKind::AsClassMethod;
  }

  return FactoryAsInitKind::Infer;
}

/// Determine whether this Objective-C method should be imported as
/// an initializer.
///
/// \param prefixLength Will be set to the length of the prefix that
/// should be stripped from the first selector piece, e.g., "init"
/// or the restated name of the class in a factory method.
///
///  \param kind Will be set to the kind of initializer being
///  imported. Note that this does not distinguish designated
///  vs. convenience; both will be classified as "designated".
static bool shouldImportAsInitializer(const clang::ObjCMethodDecl *method,
                                      ImportNameVersion version,
                                      unsigned &prefixLength,
                                      CtorInitializerKind &kind) {
  /// Is this an initializer?
  if (isInitMethod(method)) {
    prefixLength = 4;
    kind = CtorInitializerKind::Designated;
    return true;
  }

  // It must be a class method.
  if (!method->isClassMethod()) return false;

  // Said class methods must be in an actual class.
  auto objcClass = method->getClassInterface();
  if (!objcClass) return false;

  // Check whether we should try to import this factory method as an
  // initializer.
  switch (getFactoryAsInit(objcClass, method, version)) {
  case FactoryAsInitKind::AsInitializer:
    // Okay; check for the correct result type below.
    prefixLength = 0;
    break;

  case FactoryAsInitKind::Infer:
    // See if we can match the class name to the beginning of the first
    // selector piece.
    if (auto matchedLength = matchFactoryAsInitName(method)) {
      prefixLength = *matchedLength;
      break;
    }

    return false;

  case FactoryAsInitKind::AsClassMethod:
    return false;
  }

  // Determine what kind of initializer we're creating.
  if (auto initKind = determineCtorInitializerKind(method)) {
    kind = *initKind;
    return true;
  }

  // Not imported as an initializer.
  return false;
}

/// Attempt to omit needless words from the given function name.
static bool omitNeedlessWordsInFunctionName(
    StringRef &baseName, SmallVectorImpl<StringRef> &argumentNames,
    ArrayRef<const clang::ParmVarDecl *> params, clang::QualType resultType,
    const clang::DeclContext *dc, const llvm::SmallBitVector &nonNullArgs,
    Optional<unsigned> errorParamIndex, bool returnsSelf, bool isInstanceMethod,
    NameImporter &nameImporter) {
  clang::ASTContext &clangCtx = nameImporter.getClangContext();
  const version::Version &swiftLanguageVersion =
      nameImporter.getLangOpts().EffectiveLanguageVersion;

  // Collect the parameter type names.
  StringRef firstParamName;
  SmallVector<OmissionTypeName, 4> paramTypes;
  for (unsigned i = 0, n = params.size(); i != n; ++i) {
    auto param = params[i];

    // Capture the first parameter name.
    if (i == 0)
      firstParamName = param->getName();

    // Determine the number of parameters.
    unsigned numParams = params.size();
    if (errorParamIndex) --numParams;

    bool isLastParameter
      = (i == params.size() - 1) ||
        (i == params.size() - 2 &&
         errorParamIndex && *errorParamIndex == params.size() - 1);

    // Figure out whether there will be a default argument for this
    // parameter.
    StringRef argumentName;
    if (i < argumentNames.size())
      argumentName = argumentNames[i];
    bool hasDefaultArg =
        ClangImporter::Implementation::inferDefaultArgument(
            param->getType(),
            getParamOptionality(swiftLanguageVersion, param,
                                !nonNullArgs.empty() && nonNullArgs[i]),
            nameImporter.getIdentifier(baseName), numParams, argumentName,
            i == 0, isLastParameter, nameImporter) != DefaultArgumentKind::None;

    paramTypes.push_back(getClangTypeNameForOmission(clangCtx,
                                                     param->getOriginalType())
                            .withDefaultArgument(hasDefaultArg));
  }

  // Find the property names.
  const InheritedNameSet *allPropertyNames = nullptr;
  auto contextType = getClangDeclContextType(dc);
  if (!contextType.isNull()) {
    if (auto objcPtrType = contextType->getAsObjCInterfacePointerType())
      if (auto objcClassDecl = objcPtrType->getInterfaceDecl())
        allPropertyNames = nameImporter.getAllPropertyNames(
            objcClassDecl, isInstanceMethod);
  }

  // Omit needless words.
  return omitNeedlessWords(baseName, argumentNames, firstParamName,
                           getClangTypeNameForOmission(clangCtx, resultType),
                           getClangTypeNameForOmission(clangCtx, contextType),
                           paramTypes, returnsSelf, /*isProperty=*/false,
                           allPropertyNames, nameImporter.getScratch());
}

/// Prepare global name for importing onto a swift_newtype.
static StringRef determineSwiftNewtypeBaseName(StringRef baseName,
                                               StringRef newtypeName,
                                               bool &strippedPrefix) {
  StringRef newBaseName = stripLeadingK(baseName);
  if (newBaseName != baseName) {
    baseName = newBaseName;
    strippedPrefix = true;
  }

  // Special case: Strip Notification for NSNotificationName
  auto stripped = stripNotification(baseName);
  if (!stripped.empty())
    return stripped;

  bool nonIdentifier = false;
  auto pre = getCommonWordPrefix(newtypeName, baseName, nonIdentifier);
  if (pre.size()) {
    baseName = baseName.drop_front(pre.size());
    strippedPrefix = true;
  }

  return baseName;
}

EffectiveClangContext
NameImporter::determineEffectiveContext(const clang::NamedDecl *decl,
                                        const clang::DeclContext *dc,
                                        ImportNameVersion version) {
  EffectiveClangContext res;

  // Enumerators can end up within their enclosing enum or in the global
  // scope, depending how their enclosing enumeration is imported.
  if (isa<clang::EnumConstantDecl>(decl)) {
    auto enumDecl = cast<clang::EnumDecl>(dc);
    switch (getEnumKind(enumDecl)) {
    case EnumKind::Enum:
    case EnumKind::Options:
      // Enums are mapped to Swift enums, Options to Swift option sets.
      if (version != ImportNameVersion::raw()) {
        res = cast<clang::DeclContext>(enumDecl);
        break;
      }
      LLVM_FALLTHROUGH;
    case EnumKind::Constants:
    case EnumKind::Unknown:
      // The enum constant goes into the redeclaration context of the
      // enum.
      res = enumDecl->getRedeclContext();
      break;
    }
    // Import onto a swift_newtype if present
  } else if (auto newtypeDecl = findSwiftNewtype(decl, clangSema, version)) {
    res = newtypeDecl;
    // Everything else goes into its redeclaration context.
  } else {
    res = dc->getRedeclContext();
  }

  // Anything in an Objective-C category or extension is adjusted to the
  // class context.
  if (auto category =
          dyn_cast_or_null<clang::ObjCCategoryDecl>(res.getAsDeclContext())) {
    // If the enclosing category is invalid, we cannot import the declaration.
    if (category->isInvalidDecl())
      return {};

    return category->getClassInterface();
  }

  return res;
}

bool NameImporter::hasNamingConflict(const clang::NamedDecl *decl,
                                     const clang::IdentifierInfo *proposedName,
                                     const clang::TypedefNameDecl *cfTypedef) {
  // Test to see if there is a value with the same name as 'proposedName'
  // in the same module as the decl
  // FIXME: This will miss macros.
  auto clangModule = getClangSubmoduleForDecl(decl);
  if (clangModule.hasValue() && clangModule.getValue())
    clangModule = clangModule.getValue()->getTopLevelModule();

  auto conflicts = [&](const clang::Decl *OtherD) -> bool {
    // If these are simply redeclarations, they do not conflict.
    if (decl->getCanonicalDecl() == OtherD->getCanonicalDecl())
      return false;

    // If we have a CF typedef, check whether the "other"
    // declaration we found is just the opaque type behind it. If
    // so, it does not conflict.
    if (cfTypedef) {
      if (auto cfPointerTy =
              cfTypedef->getUnderlyingType()->getAs<clang::PointerType>()) {
        if (auto tagDecl = cfPointerTy->getPointeeType()->getAsTagDecl()) {
          if (tagDecl->getCanonicalDecl() == OtherD)
            return false;
        }
      }
    }

    auto declModule = getClangSubmoduleForDecl(OtherD);
    if (!declModule.hasValue())
      return false;

    // Handle the bridging header case. This is pretty nasty since things
    // can get added to it *later*, but there's not much we can do.
    if (!declModule.getValue())
      return *clangModule == nullptr;
    return *clangModule == declModule.getValue()->getTopLevelModule();
  };

  // Allow this lookup to find hidden names.  We don't want the
  // decision about whether to rename the decl to depend on
  // what exactly the user has imported.  Indeed, if we're being
  // asked to resolve a serialization cross-reference, the user
  // may not have imported this module at all, which means a
  // normal lookup wouldn't even find the decl!
  //
  // Meanwhile, we don't need to worry about finding unwanted
  // hidden declarations from different modules because we do a
  // module check before deciding that there's a conflict.
  clang::LookupResult lookupResult(clangSema, proposedName,
                                   clang::SourceLocation(),
                                   clang::Sema::LookupOrdinaryName);
  lookupResult.setAllowHidden(true);
  lookupResult.suppressDiagnostics();

  if (clangSema.LookupName(lookupResult, /*scope=*/nullptr)) {
    if (std::any_of(lookupResult.begin(), lookupResult.end(), conflicts))
      return true;
  }

  lookupResult.clear(clang::Sema::LookupTagName);
  if (clangSema.LookupName(lookupResult, /*scope=*/nullptr)) {
    if (std::any_of(lookupResult.begin(), lookupResult.end(), conflicts))
      return true;
  }

  return false;
}

static bool shouldBeSwiftPrivate(NameImporter &nameImporter,
                                 const clang::NamedDecl *decl,
                                 ImportNameVersion version) {
  // Decl with the attribute are obviously private
  if (decl->hasAttr<clang::SwiftPrivateAttr>())
    return true;

  // Enum constants that are not imported as members should be considered
  // private if the parent enum is marked private.
  if (auto *ECD = dyn_cast<clang::EnumConstantDecl>(decl)) {
    auto *ED = cast<clang::EnumDecl>(ECD->getDeclContext());
    switch (nameImporter.getEnumKind(ED)) {
    case EnumKind::Enum:
    case EnumKind::Options:
      if (version != ImportNameVersion::raw())
        break;
      LLVM_FALLTHROUGH;
    case EnumKind::Constants:
    case EnumKind::Unknown:
      if (ED->hasAttr<clang::SwiftPrivateAttr>())
        return true;
      if (auto *enumTypedef = ED->getTypedefNameForAnonDecl())
        if (enumTypedef->hasAttr<clang::SwiftPrivateAttr>())
          return true;
      break;
    }
  }

  return false;
}

Optional<ForeignErrorConvention::Info> NameImporter::considerErrorImport(
    const clang::ObjCMethodDecl *clangDecl, StringRef &baseName,
    SmallVectorImpl<StringRef> &paramNames,
    ArrayRef<const clang::ParmVarDecl *> params, bool isInitializer,
    bool hasCustomName) {
  // If the declaration name isn't parallel to the actual parameter
  // list (e.g. if the method has C-style parameter declarations),
  // don't try to apply error conventions.
  bool expectsToRemoveError =
      hasCustomName && paramNames.size() + 1 == params.size();
  if (!expectsToRemoveError && paramNames.size() != params.size())
    return None;

  for (unsigned index = params.size(); index-- != 0; ) {
    // Allow an arbitrary number of trailing blocks.
    if (isBlockParameter(params[index]))
      continue;

    // Otherwise, require the last parameter to be an out-parameter.
    auto isErrorOwned = ForeignErrorConvention::IsNotOwned;
    if (!isErrorOutParameter(params[index], isErrorOwned))
      break;

    auto errorKind =
      classifyMethodErrorHandling(clangDecl,
                                  getResultOptionality(clangDecl));
    if (!errorKind) return None;

    // Consider adjusting the imported declaration name to remove the
    // parameter.
    bool adjustName = !hasCustomName;

    // Never do this if it's the first parameter of a constructor.
    if (isInitializer && index == 0) {
      adjustName = false;
    }

    // If the error parameter is the first parameter, try removing the
    // standard error suffix from the base name.
    StringRef suffixToStrip;
    StringRef origBaseName = baseName;
    if (adjustName && index == 0 && paramNames[0].empty()) {
      if (baseName.endswith(ErrorSuffix))
        suffixToStrip = ErrorSuffix;
      else if (baseName.endswith(AltErrorSuffix))
        suffixToStrip = AltErrorSuffix;

      if (!suffixToStrip.empty()) {
        StringRef newBaseName = baseName.drop_back(suffixToStrip.size());
        if (newBaseName.empty() || isSwiftReservedName(newBaseName)) {
          adjustName = false;
          suffixToStrip = {};
        } else {
          baseName = newBaseName;
        }
      }
    }

    // Also suppress name changes if there's a collision.
    // TODO: this logic doesn't really work with init methods
    // TODO: this privileges the old API over the new one
    if (adjustName &&
        hasErrorMethodNameCollision(clangDecl, index, suffixToStrip)) {
      // If there was a conflict on the first argument, and this was
      // the first argument and we're not stripping error suffixes, just
      // give up completely on error import.
      if (index == 0 && suffixToStrip.empty()) {
        return None;

      // If there was a conflict stripping an error suffix, adjust the
      // name but don't change the base name.  This avoids creating a
      // spurious _: () argument.
      } else if (index == 0 && !suffixToStrip.empty()) {
        suffixToStrip = {};
        baseName = origBaseName;

      // Otherwise, give up on adjusting the name.
      } else {
        adjustName = false;
        baseName = origBaseName;
      }
    }

    // If we're adjusting the name, erase the error parameter.
    if (adjustName) {
      paramNames.erase(paramNames.begin() + index);
    }

    bool replaceParamWithVoid = !adjustName && !expectsToRemoveError;
    ForeignErrorConvention::Info errorInfo(
        *errorKind, index, isErrorOwned,
        (ForeignErrorConvention::IsReplaced_t)replaceParamWithVoid);
    return errorInfo;
  }

  // Didn't find an error parameter.
  return None;
}

bool NameImporter::hasErrorMethodNameCollision(
    const clang::ObjCMethodDecl *method, unsigned paramIndex,
    StringRef suffixToStrip) {
  // Copy the existing selector pieces into an array.
  auto selector = method->getSelector();
  unsigned numArgs = selector.getNumArgs();
  assert(numArgs > 0);

  SmallVector<clang::IdentifierInfo *, 4> chunks;
  for (unsigned i = 0, e = selector.getNumArgs(); i != e; ++i) {
    chunks.push_back(selector.getIdentifierInfoForSlot(i));
  }

  auto &ctx = method->getASTContext();
  if (paramIndex == 0 && !suffixToStrip.empty()) {
    StringRef name = chunks[0]->getName();
    assert(name.endswith(suffixToStrip));
    name = name.drop_back(suffixToStrip.size());
    chunks[0] = &ctx.Idents.get(name);
  } else if (paramIndex != 0) {
    chunks.erase(chunks.begin() + paramIndex);
  }

  auto newSelector = ctx.Selectors.getSelector(numArgs - 1, chunks.data());
  const clang::ObjCMethodDecl *conflict;
  if (auto iface = method->getClassInterface()) {
    conflict = iface->lookupMethod(newSelector, method->isInstanceMethod());
  } else {
    auto protocol = cast<clang::ObjCProtocolDecl>(method->getDeclContext());
    conflict = protocol->getMethod(newSelector, method->isInstanceMethod());
  }

  if (conflict == nullptr)
    return false;

  // Look to see if the conflicting decl is unavailable, either because it's
  // been marked NS_SWIFT_UNAVAILABLE, because it's actually marked unavailable,
  // or because it was deprecated before our API sunset. We can handle
  // "conflicts" where one form is unavailable.
  return !isUnavailableInSwift(conflict, availability,
                               enableObjCInterop());
}

/// Whether we should suppress this factory method being imported as an
/// initializer. We want to do this when explicitly directed to, or when
/// importing a property accessor.
static bool suppressFactoryMethodAsInit(const clang::ObjCMethodDecl *method,
                                        ImportNameVersion version,
                                        CtorInitializerKind initKind) {
  return (version == ImportNameVersion::raw() || method->isPropertyAccessor()) &&
         (initKind == CtorInitializerKind::Factory ||
          initKind == CtorInitializerKind::ConvenienceFactory);
}

ImportedName NameImporter::importNameImpl(const clang::NamedDecl *D,
                                          ImportNameVersion version,
                                          clang::DeclarationName givenName) {
  ImportedName result;

  /// Whether we want a Swift 3 or later name
  bool swift3OrLaterName = version > ImportNameVersion::swift2();

  // Objective-C categories and extensions don't have names, despite
  // being "named" declarations.
  if (isa<clang::ObjCCategoryDecl>(D))
    return ImportedName();

  // Dig out the definition, if there is one.
  if (auto def = getDefinitionForClangTypeDecl(D)) {
    if (*def)
      D = static_cast<const clang::NamedDecl *>(*def);
  }

  // Compute the effective context.
  auto dc = const_cast<clang::DeclContext *>(D->getDeclContext());
  auto effectiveCtx = determineEffectiveContext(D, dc, version);
  if (!effectiveCtx)
    return ImportedName();
  result.effectiveContext = effectiveCtx;

  // FIXME: ugly to check here, instead perform unified check up front in
  // containing struct...
  if (findSwiftNewtype(D, clangSema, version))
    result.info.importAsMember = true;

  // Find the original method/property declaration and retrieve the
  // name from there.
  if (auto method = dyn_cast<clang::ObjCMethodDecl>(D)) {
    // Inherit the name from the "originating" declarations, if
    // there are any.
    SmallVector<std::pair<const clang::ObjCMethodDecl *, ImportedName>, 4>
        overriddenNames;
    SmallVector<const clang::ObjCMethodDecl *, 4> overriddenMethods;
    method->getOverriddenMethods(overriddenMethods);
    for (auto overridden : overriddenMethods) {
      const auto overriddenName = importName(overridden, version, givenName);
      if (overriddenName.getDeclName())
        overriddenNames.push_back({overridden, overriddenName});
    }

    // If we found any names of overridden methods, return those names.
    if (!overriddenNames.empty()) {
      if (overriddenNames.size() > 1)
        mergeOverriddenNames(swiftCtx, method, overriddenNames);
      overriddenNames[0].second.effectiveContext = result.effectiveContext;
      return overriddenNames[0].second;
    }
  } else if (auto property = dyn_cast<clang::ObjCPropertyDecl>(D)) {
    // Inherit the name from the "originating" declarations, if
    // there are any.
    if (auto getter = property->getGetterMethodDecl()) {
      SmallVector<std::pair<const clang::ObjCPropertyDecl *, ImportedName>, 4>
          overriddenNames;
      SmallVector<const clang::ObjCMethodDecl *, 4> overriddenMethods;
      SmallPtrSet<const clang::ObjCPropertyDecl *, 4> knownProperties;
      (void)knownProperties.insert(property);

      getter->getOverriddenMethods(overriddenMethods);
      for (auto overridden : overriddenMethods) {
        if (!overridden->isPropertyAccessor())
          continue;
        auto overriddenProperty = overridden->findPropertyDecl(true);
        if (!overriddenProperty)
          continue;
        if (!knownProperties.insert(overriddenProperty).second)
          continue;

        const auto overriddenName = importName(overriddenProperty, version,
                                               givenName);
        if (overriddenName.getDeclName())
          overriddenNames.push_back({overriddenProperty, overriddenName});
      }

      // If we found any names of overridden methods, return those names.
      if (!overriddenNames.empty()) {
        if (overriddenNames.size() > 1)
          mergeOverriddenNames(swiftCtx, property, overriddenNames);
        overriddenNames[0].second.effectiveContext = result.effectiveContext;
        return overriddenNames[0].second;
      }
    }
  }

  // If we have a swift_name attribute, use that.
  if (auto *nameAttr = findSwiftNameAttr(D, version)) {
    bool skipCustomName = false;

    // Parse the name.
    ParsedDeclName parsedName = parseDeclName(nameAttr->getName());
    if (!parsedName || parsedName.isOperator())
      return result;

    // If we have an Objective-C method that is being mapped to an
    // initializer (e.g., a factory method whose name doesn't fit the
    // convention for factory methods), make sure that it can be
    // imported as an initializer.
    bool isInitializer = false;
    auto method = dyn_cast<clang::ObjCMethodDecl>(D);
    if (method) {
      unsigned initPrefixLength;
      if (parsedName.BaseName == "init" && parsedName.IsFunctionName) {
        if (!shouldImportAsInitializer(method, version, initPrefixLength,
                                       result.info.initKind)) {
          // We cannot import this as an initializer anyway.
          return ImportedName();
        }

        // If this swift_name attribute maps a factory method to an
        // initializer and we were asked not to do so, ignore the
        // custom name.
        if (suppressFactoryMethodAsInit(method, version,
                                        result.getInitKind())) {
          skipCustomName = true;
        } else {
          // Note that this is an initializer.
          isInitializer = true;
        }
      }
    }

    if (!skipCustomName) {
      result.info.hasCustomName = true;
      result.declName = parsedName.formDeclName(swiftCtx);

      // Handle globals treated as members.
      if (parsedName.isMember()) {
        // FIXME: Make sure this thing is global.
        result.effectiveContext = parsedName.ContextName;
        if (parsedName.SelfIndex) {
          result.info.hasSelfIndex = true;
          result.info.selfIndex = *parsedName.SelfIndex;
        }
        result.info.importAsMember = true;

        if (parsedName.BaseName == "init")
          result.info.initKind = CtorInitializerKind::Factory;
      }

      // Map property getters/setters.
      if (parsedName.IsGetter)
        result.info.accessorKind = ImportedAccessorKind::PropertyGetter;
      else if (parsedName.IsSetter)
        result.info.accessorKind = ImportedAccessorKind::PropertySetter;

      if (method && parsedName.IsFunctionName) {
        // Get the parameters.
        ArrayRef<const clang::ParmVarDecl *> params{method->param_begin(),
                                                    method->param_end()};

        if (auto errorInfo = considerErrorImport(method, parsedName.BaseName,
                                                 parsedName.ArgumentLabels,
                                                 params, isInitializer,
                                                 /*hasCustomName=*/true)) {
          result.info.hasErrorInfo = true;
          result.info.errorInfo = *errorInfo;
        }
      }

      return result;
    }
  } else if (swift3OrLaterName && (inferImportAsMember ||
                                   moduleIsInferImportAsMember(D, clangSema)) &&
             (isa<clang::VarDecl>(D) || isa<clang::FunctionDecl>(D)) &&
             dc->isTranslationUnit()) {
    auto inference = IAMResult::infer(swiftCtx, clangSema, D);
    if (inference.isImportAsMember()) {
      result.info.importAsMember = true;
      result.declName = inference.name;
      result.effectiveContext = inference.effectiveDC;

      // Instance or static
      if (inference.selfIndex) {
        result.info.hasSelfIndex = true;
        result.info.selfIndex = *inference.selfIndex;
      }

      // Property
      if (inference.isGetter())
        result.info.accessorKind = ImportedAccessorKind::PropertyGetter;
      else if (inference.isSetter())
        result.info.accessorKind = ImportedAccessorKind::PropertySetter;

      // Inits are factory. These C functions are neither convenience nor
      // designated, as they return a fully formed object of that type.
      if (inference.isInit())
        result.info.initKind = CtorInitializerKind::Factory;

      return result;
    }
  }

  // For empty names, there is nothing to do.
  if (D->getDeclName().isEmpty())
    return result;

  /// Whether the result is a function name.
  bool isFunction = false;
  bool isInitializer = false;
  unsigned initializerPrefixLen;
  StringRef baseName;
  SmallVector<StringRef, 4> argumentNames;
  SmallString<16> selectorSplitScratch;
  ArrayRef<const clang::ParmVarDecl *> params;
  switch (D->getDeclName().getNameKind()) {
  case clang::DeclarationName::CXXConstructorName:
  case clang::DeclarationName::CXXConversionFunctionName:
  case clang::DeclarationName::CXXDestructorName:
  case clang::DeclarationName::CXXLiteralOperatorName:
  case clang::DeclarationName::CXXOperatorName:
  case clang::DeclarationName::CXXUsingDirective:
  case clang::DeclarationName::CXXDeductionGuideName:
    // Handling these is part of C++ interoperability.
    llvm_unreachable("unhandled C++ interoperability");

  case clang::DeclarationName::Identifier:
    // Map the identifier.
    baseName = D->getDeclName().getAsIdentifierInfo()->getName();

    if (givenName) {
      if (!givenName.isIdentifier())
        return ImportedName();
      baseName = givenName.getAsIdentifierInfo()->getName();
    }

    // For Objective-C BOOL properties, use the name of the getter
    // which, conventionally, has an "is" prefix.
    if (swift3OrLaterName) {
      if (auto property = dyn_cast<clang::ObjCPropertyDecl>(D)) {
        if (isBoolType(clangSema.Context, property->getType()))
          baseName = property->getGetterName().getNameForSlot(0);
      }
    }

    // For C functions, create empty argument names.
    if (auto function = dyn_cast<clang::FunctionDecl>(D)) {
      isFunction = true;
      params = {function->param_begin(), function->param_end()};
      for (auto param : params) {
        (void)param;
        argumentNames.push_back(StringRef());
      }
      if (function->isVariadic())
        argumentNames.push_back(StringRef());
    }
    break;

  case clang::DeclarationName::ObjCMultiArgSelector:
  case clang::DeclarationName::ObjCOneArgSelector:
  case clang::DeclarationName::ObjCZeroArgSelector: {
    auto objcMethod = cast<clang::ObjCMethodDecl>(D);

    // Map the Objective-C selector directly.
    auto selector = D->getDeclName().getObjCSelector();

    // Respect the given name.
    if (givenName) {
      switch (givenName.getNameKind()) {
      case clang::DeclarationName::ObjCOneArgSelector:
      case clang::DeclarationName::ObjCMultiArgSelector:
      case clang::DeclarationName::ObjCZeroArgSelector:

        // Make sure the given name has the right count of arguments.
        if (selector.getNumArgs() != givenName.getObjCSelector().getNumArgs())
          return ImportedName();
        selector = givenName.getObjCSelector();
        break;
      default:
        return ImportedName();
      }
    }

    baseName = selector.getNameForSlot(0);

    // We don't support methods with empty first selector pieces.
    if (baseName.empty())
      return ImportedName();

    isInitializer = shouldImportAsInitializer(objcMethod, version,
                                              initializerPrefixLen,
                                              result.info.initKind);

    // If we would import a factory method as an initializer but were
    // asked not to, don't consider this as an initializer.
    if (isInitializer && suppressFactoryMethodAsInit(objcMethod, version,
                                                     result.getInitKind())) {
      isInitializer = false;
    }

    if (isInitializer)
      baseName = "init";

    // Get the parameters.
    params = {objcMethod->param_begin(), objcMethod->param_end()};

    // If we have a variadic method for which we need to drop the last
    // selector piece, do so now.
    unsigned numArgs = selector.getNumArgs();
    if (objcMethod->isVariadic() && shouldMakeSelectorNonVariadic(selector)) {
      --numArgs;
      result.info.droppedVariadic = true;
      params = params.drop_back(1);
    }

    for (unsigned index = 0; index != numArgs; ++index) {
      if (index == 0) {
        argumentNames.push_back(StringRef());
      } else {
        StringRef argName = selector.getNameForSlot(index);
        argumentNames.push_back(argName);
      }
    }

    // For initializers, compute the first argument name.
    if (isInitializer) {
      // Skip over the prefix.
      auto argName = selector.getNameForSlot(0).substr(initializerPrefixLen);

      // Drop "With" if present after the "init".
      bool droppedWith = false;
      if (argName.startswith("With")) {
        argName = argName.substr(4);
        droppedWith = true;
      }

      // Lowercase the remaining argument name.
      argName = camel_case::toLowercaseWord(argName, selectorSplitScratch);

      // If we dropped "with" and ended up with a reserved name,
      // put "with" back.
      if (droppedWith && isSwiftReservedName(argName)) {
        selectorSplitScratch = "with";
        selectorSplitScratch +=
            selector.getNameForSlot(0).substr(initializerPrefixLen + 4);
        argName = selectorSplitScratch;
      }

      // Set the first argument name to be the name we computed. If
      // there is no first argument, create one for this purpose.
      if (argumentNames.empty()) {
        if (!argName.empty()) {
          // FIXME: Record what happened here for the caller?
          argumentNames.push_back(argName);
        }
      } else {
        argumentNames[0] = argName;
      }
    }

    if (auto errorInfo = considerErrorImport(
        objcMethod, baseName, argumentNames, params, isInitializer,
        /*hasCustomName=*/false)) {
        result.info.hasErrorInfo = true;
        result.info.errorInfo = *errorInfo;
    }

    isFunction = true;

    // Is this one of the accessors for subscripts?
    if (objcMethod->getMethodFamily() == clang::OMF_None &&
        objcMethod->isInstanceMethod()) {
      if (isNonNullarySelector(objcMethod->getSelector(),
                               {"objectAtIndexedSubscript"}) ||
          isNonNullarySelector(objcMethod->getSelector(),
                               {"objectForKeyedSubscript"}))
        result.info.accessorKind = ImportedAccessorKind::SubscriptGetter;
      else if (isNonNullarySelector(objcMethod->getSelector(),
                                    {"setObject", "atIndexedSubscript"}) ||
               isNonNullarySelector(objcMethod->getSelector(),
                                    {"setObject", "forKeyedSubscript"}))
        result.info.accessorKind = ImportedAccessorKind::SubscriptSetter;
    }

    break;
  }
  }

  // Perform automatic name transformations.

  // Enumeration constants may have common prefixes stripped.
  bool strippedPrefix = false;
  if (version != ImportNameVersion::raw() && isa<clang::EnumConstantDecl>(D)) {
    auto enumDecl = cast<clang::EnumDecl>(D->getDeclContext());
    auto enumInfo = getEnumInfo(enumDecl);

    StringRef removePrefix = enumInfo.getConstantNamePrefix();
    if (!removePrefix.empty()) {
      if (baseName.startswith(removePrefix)) {
        baseName = baseName.substr(removePrefix.size());
        strippedPrefix = true;
      } else if (givenName) {
        // Calculate the new prefix.
        // What if the preferred name causes longer prefix?
        StringRef subPrefix = [](StringRef LHS, StringRef RHS) {
          if (LHS.size() > RHS.size())
            std::swap(LHS, RHS) ;
          return StringRef(LHS.data(), std::mismatch(LHS.begin(), LHS.end(),
            RHS.begin()).first - LHS.begin());
        }(removePrefix, baseName);
        if (!subPrefix.empty()) {
          baseName = baseName.substr(subPrefix.size());
          strippedPrefix = true;
        }
      }
    }
  }

  // If the error is an error enum, it will be mapped to the 'Code'
  // enum nested within an NSError-containing struct. Strip the word
  // "Code" off the end of the name, if it's there, because it's
  // redundant.
  if (auto enumDecl = dyn_cast<clang::EnumDecl>(D)) {
    if (enumDecl->isThisDeclarationADefinition()) {
      auto enumInfo = getEnumInfo(enumDecl);
      if (enumInfo.isErrorEnum() && baseName.size() > 4 &&
          camel_case::getLastWord(baseName) == "Code")
        baseName = baseName.substr(0, baseName.size() - 4);
    }
  }

  // Objective-C protocols may have the suffix "Protocol" appended if
  // the non-suffixed name would conflict with another entity in the
  // same top-level module.
  SmallString<16> baseNameWithProtocolSuffix;
  if (auto objcProto = dyn_cast<clang::ObjCProtocolDecl>(D)) {
    if (objcProto->hasDefinition()) {
      if (hasNamingConflict(D, objcProto->getIdentifier(), nullptr)) {
        baseNameWithProtocolSuffix = baseName;
        baseNameWithProtocolSuffix += SWIFT_PROTOCOL_SUFFIX;
        baseName = baseNameWithProtocolSuffix;
      }
    }
  }

  // Typedef declarations might be CF types that will drop the "Ref"
  // suffix.
  clang::ASTContext &clangCtx = clangSema.Context;
  if (swift3OrLaterName) {
    if (auto typedefNameDecl = dyn_cast<clang::TypedefNameDecl>(D)) {
      auto swiftName = getCFTypeName(typedefNameDecl);
      if (!swiftName.empty() &&
          !hasNamingConflict(D, &clangCtx.Idents.get(swiftName),
                             typedefNameDecl)) {
        // Adopt the requested name.
        baseName = swiftName;
      }
    }
  }

  // swift_newtype-ed declarations may have common words with the type name
  // stripped.
  if (auto newtypeDecl = findSwiftNewtype(D, clangSema, version)) {
    result.info.importAsMember = true;
    baseName = determineSwiftNewtypeBaseName(baseName, newtypeDecl->getName(),
                                             strippedPrefix);
  }

  if (!result.isSubscriptAccessor() && swift3OrLaterName) {
    // Objective-C properties.
    if (auto objcProperty = dyn_cast<clang::ObjCPropertyDecl>(D)) {
      auto contextType = getClangDeclContextType(
          D->getDeclContext());
      if (!contextType.isNull()) {
        auto contextTypeName =
            getClangTypeNameForOmission(clangCtx, contextType);
        auto propertyTypeName =
            getClangTypeNameForOmission(clangCtx, objcProperty->getType());
        // Find the property names.
        const InheritedNameSet *allPropertyNames = nullptr;
        if (!contextType.isNull()) {
          if (auto objcPtrType = contextType->getAsObjCInterfacePointerType())
            if (auto objcClassDecl = objcPtrType->getInterfaceDecl())
              allPropertyNames =
                  getAllPropertyNames(objcClassDecl, /*forInstance=*/true);
        }

        (void)omitNeedlessWords(baseName, {}, "", propertyTypeName,
                                contextTypeName, {}, /*returnsSelf=*/false,
                                /*isProperty=*/true, allPropertyNames,
                                scratch);
      }
    }

    // Objective-C methods.
    if (auto method = dyn_cast<clang::ObjCMethodDecl>(D)) {
      (void)omitNeedlessWordsInFunctionName(
          baseName, argumentNames, params, method->getReturnType(),
          method->getDeclContext(), getNonNullArgs(method, params),
          result.getErrorInfo()
              ? Optional<unsigned>(result.getErrorInfo()->ErrorParameterIndex)
              : None,
          method->hasRelatedResultType(), method->isInstanceMethod(), *this);
    }

    // If the result is a value, lowercase it.
    if (strippedPrefix && isa<clang::ValueDecl>(D) &&
        shouldLowercaseValueName(baseName)) {
      baseName = camel_case::toLowercaseInitialisms(baseName, scratch);
    }
  }

  // If this declaration has the swift_private attribute, prepend "__" to the
  // appropriate place.
  SmallString<16> swiftPrivateScratch;
  if (shouldBeSwiftPrivate(*this, D, version)) {
    // Special case: empty arg factory, "for historical reasons", is not private
    if (isInitializer && argumentNames.empty() &&
        (result.getInitKind() == CtorInitializerKind::Factory ||
         result.getInitKind() == CtorInitializerKind::ConvenienceFactory))
      return result;

    // Make the given name private.
    swiftPrivateScratch = "__";

    if (isInitializer) {
      // For initializers, prepend "__" to the first argument name.
      if (argumentNames.empty()) {
        // FIXME: Record that we did this.
        argumentNames.push_back("__");
      } else {
        swiftPrivateScratch += argumentNames[0];
        argumentNames[0] = swiftPrivateScratch;
      }
    } else {
      // For all other entities, prepend "__" to the base name.
      swiftPrivateScratch += baseName;
      baseName = swiftPrivateScratch;
    }
  }

  result.declName = formDeclName(swiftCtx, baseName, argumentNames, isFunction);
  return result;
}

/// Returns true if it is expected that the macro is ignored.
static bool shouldIgnoreMacro(StringRef name, const clang::MacroInfo *macro) {
  // Ignore include guards.
  if (macro->isUsedForHeaderGuard())
    return true;

  // If there are no tokens, there is nothing to convert.
  if (macro->tokens_empty())
    return true;

  // Currently we only convert non-function-like macros.
  if (macro->isFunctionLike())
    return true;

  // Consult the list of macros to suppress.
  auto suppressMacro = llvm::StringSwitch<bool>(name)
#define SUPPRESS_MACRO(NAME) .Case(#NAME, true)
#include "MacroTable.def"
                           .Default(false);

  if (suppressMacro)
    return true;

  return false;
}

bool ClangImporter::shouldIgnoreMacro(StringRef Name,
                                      const clang::MacroInfo *Macro) {
  return ::shouldIgnoreMacro(Name, Macro);
}

Identifier
NameImporter::importMacroName(const clang::IdentifierInfo *clangIdentifier,
                              const clang::MacroInfo *macro) {
  // If we're supposed to ignore this macro, return an empty identifier.
  if (::shouldIgnoreMacro(clangIdentifier->getName(), macro))
    return Identifier();

  // No transformation is applied to the name.
  StringRef name = clangIdentifier->getName();
  return swiftCtx.getIdentifier(name);
}

ImportedName NameImporter::importName(const clang::NamedDecl *decl,
                                      ImportNameVersion version,
                                      clang::DeclarationName givenName) {
  CacheKeyType key(decl, version);
  if (importNameCache.count(key) && !givenName) {
    ++ImportNameNumCacheHits;
    return importNameCache[key];
  }
  ++ImportNameNumCacheMisses;
  auto res = importNameImpl(decl, version, givenName);
  if (!givenName)
    importNameCache[key] = res;
  return res;
}

const InheritedNameSet *NameImporter::getAllPropertyNames(
                          clang::ObjCInterfaceDecl *classDecl,
                          bool forInstance) {
  classDecl = classDecl->getCanonicalDecl();

  // If we already have this information, return it.
  auto known = allProperties.find({classDecl, forInstance});
  if (known != allProperties.end()) return known->second.get();

  // Otherwise, get information from our superclass first.
  const InheritedNameSet *parentSet = nullptr;
  if (auto superclassDecl = classDecl->getSuperClass()) {
    parentSet = getAllPropertyNames(superclassDecl, forInstance);
  }

  // Create the set of properties.
  known = allProperties.insert(
            { std::pair<const clang::ObjCInterfaceDecl *, char>(classDecl,
                                                                forInstance),
              llvm::make_unique<InheritedNameSet>(parentSet) }).first;

  // Local function to add properties from the given set.
  auto addProperties = [&](clang::DeclContext::decl_range members) {
    for (auto member : members) {
      // Add Objective-C property names.
      if (auto property = dyn_cast<clang::ObjCPropertyDecl>(member)) {
        if (forInstance)
          known->second->add(property->getName());
        continue;
      }

      // Add no-parameter, non-void method names.
      if (auto method = dyn_cast<clang::ObjCMethodDecl>(member)) {
        if (method->getSelector().isUnarySelector() &&
            !method->getReturnType()->isVoidType() &&
            !method->hasRelatedResultType() &&
            method->isInstanceMethod() == forInstance) {
          known->second->add(method->getSelector().getNameForSlot(0));
          continue;
        }
      }
    }
  };

  // Dig out the class definition.
  auto classDef = classDecl->getDefinition();
  if (!classDef) return known->second.get();

  // Collect property names from the class definition.
  addProperties(classDef->decls());

  // Dig out the module that owns the class definition.
  auto module = classDef->getImportedOwningModule();
  if (module) module = module->getTopLevelModule();

  // Collect property names from all categories and extensions in the same
  // module as the class.
  for (auto category : classDef->known_categories()) {
    auto categoryModule = category->getImportedOwningModule();
    if (categoryModule) categoryModule = categoryModule->getTopLevelModule();
    if (module != categoryModule) continue;

    addProperties(category->decls());
  }

  return known->second.get();
}

