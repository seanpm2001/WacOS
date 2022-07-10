//===--- NameLookup.cpp - Swift Name Lookup Routines ----------------------===//
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
// This file implements interfaces for performing name lookup.
//
//===----------------------------------------------------------------------===//

#include "NameLookupImpl.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/AST.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/DebuggerClient.h"
#include "swift/AST/LazyResolver.h"
#include "swift/AST/ReferencedNameTracker.h"
#include "swift/Basic/Fallthrough.h"
#include "swift/Basic/SourceManager.h"
#include "swift/Basic/STLExtras.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TinyPtrVector.h"

using namespace swift;

void DebuggerClient::anchor() {}

void AccessFilteringDeclConsumer::foundDecl(ValueDecl *D,
                                            DeclVisibilityKind reason) {
  if (D->getASTContext().LangOpts.EnableAccessControl) {
    if (TypeResolver)
      TypeResolver->resolveAccessibility(D);
    if (D->isInvalid() && !D->hasAccessibility())
      return;
    if (!D->isAccessibleFrom(DC))
      return;
  }
  ChainedConsumer.foundDecl(D, reason);
}


template <typename Fn>
static void forAllVisibleModules(const DeclContext *DC, const Fn &fn) {
  DeclContext *moduleScope = DC->getModuleScopeContext();
  if (auto file = dyn_cast<FileUnit>(moduleScope))
    file->forAllVisibleModules(fn);
  else
    cast<Module>(moduleScope)->forAllVisibleModules(Module::AccessPathTy(), fn);
}

bool swift::removeOverriddenDecls(SmallVectorImpl<ValueDecl*> &decls) {
  if (decls.empty())
    return false;

  ASTContext &ctx = decls.front()->getASTContext();
  llvm::SmallPtrSet<ValueDecl*, 8> overridden;
  for (auto decl : decls) {
    while (auto overrides = decl->getOverriddenDecl()) {
      overridden.insert(overrides);

      // Because initializers from Objective-C base classes have greater
      // visibility than initializers written in Swift classes, we can
      // have a "break" in the set of declarations we found, where
      // C.init overrides B.init overrides A.init, but only C.init and
      // A.init are in the chain. Make sure we still remove A.init from the
      // set in this case.
      if (decl->getFullName().getBaseName() == ctx.Id_init) {
        /// FIXME: Avoid the possibility of an infinite loop by fixing the root
        ///        cause instead (incomplete circularity detection).
        assert(decl != overrides && "Circular class inheritance?");
        decl = overrides;
        continue;
      }

      break;
    }
  }

  // If no methods were overridden, we're done.
  if (overridden.empty()) return false;

  // Erase any overridden declarations
  bool anyOverridden = false;
  decls.erase(std::remove_if(decls.begin(), decls.end(),
                             [&](ValueDecl *decl) -> bool {
                               if (overridden.count(decl) > 0) {
                                 anyOverridden = true;
                                 return true;
                               }

                               return false;
                             }),
              decls.end());

  return anyOverridden;
}

enum class ConstructorComparison {
  Worse,
  Same,
  Better,
};

/// Determines whether \p ctor1 is a "better" initializer than \p ctor2.
static ConstructorComparison compareConstructors(ConstructorDecl *ctor1,
                                                 ConstructorDecl *ctor2,
                                                 const swift::ASTContext &ctx) {
  bool available1 = !ctor1->getAttrs().isUnavailable(ctx);
  bool available2 = !ctor2->getAttrs().isUnavailable(ctx);

  // An unavailable initializer is always worse than an available initializer.
  if (available1 < available2)
    return ConstructorComparison::Worse;

  if (available1 > available2)
    return ConstructorComparison::Better;

  CtorInitializerKind kind1 = ctor1->getInitKind();
  CtorInitializerKind kind2 = ctor2->getInitKind();

  if (kind1 > kind2)
    return ConstructorComparison::Worse;

  if (kind1 < kind2)
    return ConstructorComparison::Better;

  return ConstructorComparison::Same;
}

bool swift::removeShadowedDecls(SmallVectorImpl<ValueDecl*> &decls,
                                const Module *curModule,
                                LazyResolver *typeResolver) {
  // Category declarations by their signatures.
  llvm::SmallDenseMap<std::pair<CanType, Identifier>,
                      llvm::TinyPtrVector<ValueDecl *>>
    CollidingDeclGroups;

  /// Objective-C initializers are tracked by their context type and
  /// full name.
  llvm::SmallDenseMap<std::pair<CanType, DeclName>, 
                      llvm::TinyPtrVector<ConstructorDecl *>>
    ObjCCollidingConstructors;
  bool anyCollisions = false;
  for (auto decl : decls) {
    // Determine the signature of this declaration.
    // FIXME: the canonical type makes a poor signature, because we don't
    // canonicalize away default arguments and don't canonicalize polymorphic
    // types well.
    CanType signature;

    // FIXME: Egregious hack to avoid failing when there are no declared types.
    if (!decl->hasType() || isa<TypeAliasDecl>(decl) || isa<AbstractTypeParamDecl>(decl)) {
      // FIXME: Pass this down instead of getting it from the ASTContext.
      if (typeResolver && !decl->isBeingTypeChecked())
        typeResolver->resolveDeclSignature(decl);
      if (!decl->hasType())
        continue;
      if (auto assocType = dyn_cast<AssociatedTypeDecl>(decl))
        if (!assocType->getArchetype())
          continue;
    }
    
    // If the decl is currently being validated, this is likely a recursive
    // reference and we'll want to skip ahead so as to avoid having its type
    // attempt to desugar itself.
    if (decl->isBeingTypeChecked())
      continue;

    signature = decl->getType()->getCanonicalType();

    // FIXME: The type of a variable or subscript doesn't include
    // enough context to distinguish entities from different
    // constrained extensions, so use the overload signature's
    // type. This is layering a partial fix upon a total hack.
    if (auto asd = dyn_cast<AbstractStorageDecl>(decl))
      signature = asd->getOverloadSignature().InterfaceType;

    // If we've seen a declaration with this signature before, note it.
    auto &knownDecls =
        CollidingDeclGroups[std::make_pair(signature, decl->getName())];
    if (!knownDecls.empty())
      anyCollisions = true;

    knownDecls.push_back(decl);

    // Specifically keep track of Objective-C initializers, which can come from
    // either init methods or factory methods.
    if (decl->hasClangNode()) {
      if (auto ctor = dyn_cast<ConstructorDecl>(decl)) {
        auto ctorSignature
          = std::make_pair(ctor->getExtensionType()->getCanonicalType(),
                           decl->getFullName());
        auto &knownCtors = ObjCCollidingConstructors[ctorSignature];
        if (!knownCtors.empty())
          anyCollisions = true;
        knownCtors.push_back(ctor);
      }
    }
  }

  // If there were no signature collisions, there is nothing to do.
  if (!anyCollisions)
    return false;

  // Determine the set of declarations that are shadowed by other declarations.
  llvm::SmallPtrSet<ValueDecl *, 4> shadowed;
  ASTContext &ctx = decls[0]->getASTContext();
  for (auto &collidingDecls : CollidingDeclGroups) {
    // If only one declaration has this signature, it isn't shadowed by
    // anything.
    if (collidingDecls.second.size() == 1)
      continue;

    // Compare each declaration to every other declaration. This is
    // unavoidably O(n^2) in the number of declarations, but because they
    // all have the same signature, we expect n to remain small.
    for (unsigned firstIdx = 0, n = collidingDecls.second.size();
         firstIdx != n; ++firstIdx) {
      auto firstDecl = collidingDecls.second[firstIdx];
      auto firstModule = firstDecl->getModuleContext();
      for (unsigned secondIdx = firstIdx + 1; secondIdx != n; ++secondIdx) {
        // Determine whether one module takes precedence over another.
        auto secondDecl = collidingDecls.second[secondIdx];
        auto secondModule = secondDecl->getModuleContext();

        // If one declaration is in a protocol or extension thereof and the
        // other is not, prefer the one that is not.
        if ((bool)firstDecl->getDeclContext()
              ->getAsProtocolOrProtocolExtensionContext()
              != (bool)secondDecl->getDeclContext()
                   ->getAsProtocolOrProtocolExtensionContext()) {
          if (firstDecl->getDeclContext()
                ->getAsProtocolOrProtocolExtensionContext()) {
            shadowed.insert(firstDecl);
            break;
          } else {
            shadowed.insert(secondDecl);
            continue;
          }
        }

        // If one declaration is available and the other is not, prefer the
        // available one.
        if (firstDecl->getAttrs().isUnavailable(ctx) !=
              secondDecl->getAttrs().isUnavailable(ctx)) {
         if (firstDecl->getAttrs().isUnavailable(ctx)) {
           shadowed.insert(firstDecl);
           break;
         } else {
           shadowed.insert(secondDecl);
           continue;
         }
        }

        // Don't apply module-shadowing rules to members of protocol types.
        if (isa<ProtocolDecl>(firstDecl->getDeclContext()) ||
            isa<ProtocolDecl>(secondDecl->getDeclContext()))
          continue;

        // Prefer declarations in the current module over those in another
        // module.
        // FIXME: This is a hack. We should query a (lazily-built, cached)
        // module graph to determine shadowing.
        if ((firstModule == curModule) == (secondModule == curModule))
          continue;

        // If the first module is the current module, the second declaration
        // is shadowed by the first.
        if (firstModule == curModule) {
          shadowed.insert(secondDecl);
          continue;
        }

        // Otherwise, the first declaration is shadowed by the second. There is
        // no point in continuing to compare the first declaration to others.
        shadowed.insert(firstDecl);
        break;
      }
    }
  }
  
  // Check for collisions among Objective-C initializers. When such collisions
  // exist, we pick the
  for (const auto &colliding : ObjCCollidingConstructors) {
    if (colliding.second.size() == 1)
      continue;

    // Find the "best" constructor with this signature.
    ConstructorDecl *bestCtor = colliding.second[0];
    for (auto ctor : colliding.second) {
      auto comparison = compareConstructors(ctor, bestCtor, ctx);
      if (comparison == ConstructorComparison::Better)
        bestCtor = ctor;
    }

    // Shadow any initializers that are worse.
    for (auto ctor : colliding.second) {
      auto comparison = compareConstructors(ctor, bestCtor, ctx);
      if (comparison == ConstructorComparison::Worse)
        shadowed.insert(ctor);
    }
  }

  // If none of the declarations were shadowed, we're done.
  if (shadowed.empty())
    return false;

  // Remove shadowed declarations from the list of declarations.
  bool anyRemoved = false;
  decls.erase(std::remove_if(decls.begin(), decls.end(),
                             [&](ValueDecl *vd) {
                               if (shadowed.count(vd) > 0) {
                                 anyRemoved = true;
                                 return true;
                               }

                               return false;
                             }),
              decls.end());

  return anyRemoved;
}

namespace {
enum class DiscriminatorMatch {
  NoDiscriminator,
  Matches,
  Different
};
}

static DiscriminatorMatch matchDiscriminator(Identifier discriminator,
                                             const ValueDecl *value) {
  if (value->getFormalAccess() > Accessibility::FilePrivate)
    return DiscriminatorMatch::NoDiscriminator;

  auto containingFile =
    dyn_cast<FileUnit>(value->getDeclContext()->getModuleScopeContext());
  if (!containingFile)
    return DiscriminatorMatch::Different;

  if (discriminator == containingFile->getDiscriminatorForPrivateValue(value))
    return DiscriminatorMatch::Matches;

  return DiscriminatorMatch::Different;
}

static DiscriminatorMatch
matchDiscriminator(Identifier discriminator,
                   UnqualifiedLookupResult lookupResult) {
  return matchDiscriminator(discriminator, lookupResult.getValueDecl());
}

template <typename Result>
static void filterForDiscriminator(SmallVectorImpl<Result> &results,
                                   DebuggerClient *debugClient) {
  Identifier discriminator = debugClient->getPreferredPrivateDiscriminator();
  if (discriminator.empty())
    return;

  auto lastMatchIter = std::find_if(results.rbegin(), results.rend(),
                                    [discriminator](Result next) -> bool {
    return
      matchDiscriminator(discriminator, next) == DiscriminatorMatch::Matches;
  });
  if (lastMatchIter == results.rend())
    return;

  Result lastMatch = *lastMatchIter;

  auto newEnd = std::remove_if(results.begin(), lastMatchIter.base()-1,
                               [discriminator](Result next) -> bool {
    return
      matchDiscriminator(discriminator, next) == DiscriminatorMatch::Different;
  });
  results.erase(newEnd, results.end());
  results.push_back(lastMatch);
}

static void recordLookupOfTopLevelName(DeclContext *topLevelContext,
                                       DeclName name,
                                       bool isCascading) {
  auto SF = dyn_cast<SourceFile>(topLevelContext);
  if (!SF)
    return;
  auto *nameTracker = SF->getReferencedNameTracker();
  if (!nameTracker)
    return;
  nameTracker->addTopLevelName(name.getBaseName(), isCascading);
}

UnqualifiedLookup::UnqualifiedLookup(DeclName Name, DeclContext *DC,
                                     LazyResolver *TypeResolver,
                                     bool IsKnownNonCascading,
                                     SourceLoc Loc, bool IsTypeLookup,
                                     bool AllowProtocolMembers) {
  Module &M = *DC->getParentModule();
  ASTContext &Ctx = M.getASTContext();
  const SourceManager &SM = Ctx.SourceMgr;
  DebuggerClient *DebugClient = M.getDebugClient();

  NamedDeclConsumer Consumer(Name, Results, IsTypeLookup);

  Optional<bool> isCascadingUse;
  if (IsKnownNonCascading)
    isCascadingUse = false;

  SmallVector<UnqualifiedLookupResult, 4> UnavailableInnerResults;

  // Never perform local lookup for operators.
  if (Name.isOperator()) {
    if (!isCascadingUse.hasValue()) {
      isCascadingUse =
        DC->isCascadingContextForLookup(/*excludeFunctions=*/true);
    }
    DC = DC->getModuleScopeContext();

  } else {
    // If we are inside of a method, check to see if there are any ivars in
    // scope, and if so, whether this is a reference to one of them.
    // FIXME: We should persist this information between lookups.
    while (!DC->isModuleScopeContext()) {
      ValueDecl *BaseDecl = 0;
      ValueDecl *MetaBaseDecl = 0;
      GenericParamList *GenericParams = nullptr;
      Type ExtendedType;
      bool isTypeLookup = false;
      
      // If this declcontext is an initializer for a static property, then we're
      // implicitly doing a static lookup into the parent declcontext.
      if (auto *PBI = dyn_cast<PatternBindingInitializer>(DC))
        if (!DC->getParent()->isModuleScopeContext()) {
          if (auto PBD = PBI->getBinding()) {
            isTypeLookup = PBD->isStatic();
            DC = DC->getParent();
          }
        }
      
      if (auto *AFD = dyn_cast<AbstractFunctionDecl>(DC)) {
        // Look for local variables; normally, the parser resolves these
        // for us, but it can't do the right thing inside local types.
        // FIXME: when we can parse and typecheck the function body partially
        // for code completion, AFD->getBody() check can be removed.
        if (Loc.isValid() && AFD->getBody()) {
          if (!isCascadingUse.hasValue()) {
            isCascadingUse =
                !SM.rangeContainsTokenLoc(AFD->getBodySourceRange(), Loc);
          }

          namelookup::FindLocalVal localVal(SM, Loc, Consumer);
          localVal.visit(AFD->getBody());
          if (!Results.empty())
            return;
          for (auto *PL : AFD->getParameterLists())
            localVal.checkParameterList(PL);
          if (!Results.empty())
            return;
        }
        if (!isCascadingUse.hasValue() || isCascadingUse.getValue())
          isCascadingUse = AFD->isCascadingContextForLookup(false);

        if (AFD->getExtensionType()) {
          if (AFD->getDeclContext()->getAsProtocolOrProtocolExtensionContext()) {
            ExtendedType = AFD->getDeclContext()->getSelfTypeInContext();

            // Fallback path.
            if (!ExtendedType)
              ExtendedType = AFD->getExtensionType();
          } else {
            ExtendedType = AFD->getExtensionType();
          }
          BaseDecl = AFD->getImplicitSelfDecl();
          MetaBaseDecl = AFD->getExtensionType()->getAnyNominal();
          DC = DC->getParent();

          if (auto *FD = dyn_cast<FuncDecl>(AFD))
            if (FD->isStatic())
              ExtendedType = MetatypeType::get(ExtendedType);

          // If we're not in the body of the function, the base declaration
          // is the nominal type, not 'self'.
          if (Loc.isValid() &&
              AFD->getBodySourceRange().isValid() &&
              !SM.rangeContainsTokenLoc(AFD->getBodySourceRange(), Loc)) {
            BaseDecl = MetaBaseDecl;
          }
        }

        // Look in the generic parameters after checking our local declaration.
        GenericParams = AFD->getGenericParams();
      } else if (auto *ACE = dyn_cast<AbstractClosureExpr>(DC)) {
        // Look for local variables; normally, the parser resolves these
        // for us, but it can't do the right thing inside local types.
        if (Loc.isValid()) {
          if (auto *CE = dyn_cast<ClosureExpr>(ACE)) {
            namelookup::FindLocalVal localVal(SM, Loc, Consumer);
            localVal.visit(CE->getBody());
            if (!Results.empty())
              return;
            localVal.checkParameterList(CE->getParameters());
            if (!Results.empty())
              return;
          }
        }
        if (!isCascadingUse.hasValue())
          isCascadingUse = ACE->isCascadingContextForLookup(false);
      } else if (ExtensionDecl *ED = dyn_cast<ExtensionDecl>(DC)) {
        ExtendedType = ED->getSelfTypeInContext();

        BaseDecl = ED->getAsNominalTypeOrNominalTypeExtensionContext();
        MetaBaseDecl = BaseDecl;
        if (!isCascadingUse.hasValue())
          isCascadingUse = ED->isCascadingContextForLookup(false);
      } else if (NominalTypeDecl *ND = dyn_cast<NominalTypeDecl>(DC)) {
        ExtendedType = ND->getDeclaredType();
        BaseDecl = ND;
        MetaBaseDecl = BaseDecl;
        if (!isCascadingUse.hasValue())
          isCascadingUse = ND->isCascadingContextForLookup(false);
      } else if (auto I = dyn_cast<DefaultArgumentInitializer>(DC)) {
        // In a default argument, skip immediately out of both the
        // initializer and the function.
        isCascadingUse = false;
        DC = I->getParent()->getParent();
        continue;
      } else {
        assert(isa<TopLevelCodeDecl>(DC) || isa<Initializer>(DC));
        if (!isCascadingUse.hasValue())
          isCascadingUse = DC->isCascadingContextForLookup(false);
      }

      // If this is implicitly a lookup into the static members, add a metatype
      // wrapper.
      if (isTypeLookup && ExtendedType)
        ExtendedType = MetatypeType::get(ExtendedType, Ctx);
      
      // Check the generic parameters for something with the given name.
      if (GenericParams) {
        namelookup::FindLocalVal localVal(SM, Loc, Consumer);
        localVal.checkGenericParams(GenericParams);

        if (!Results.empty())
          return;
      }

      if (BaseDecl) {
        if (TypeResolver)
          TypeResolver->resolveDeclSignature(BaseDecl);

        NLOptions options = NL_UnqualifiedDefault;
        if (isCascadingUse.getValue())
          options |= NL_KnownCascadingDependency;
        else
          options |= NL_KnownNonCascadingDependency;

        if (AllowProtocolMembers)
          options |= NL_ProtocolMembers;
        if (IsTypeLookup)
          options |= NL_OnlyTypes;

        if (!ExtendedType)
          ExtendedType = ErrorType::get(Ctx);

        SmallVector<ValueDecl *, 4> Lookup;
        DC->lookupQualified(ExtendedType, Name, options, TypeResolver, Lookup);
        bool isMetatypeType = ExtendedType->is<AnyMetatypeType>();
        bool FoundAny = false;
        for (auto Result : Lookup) {
          // If we're looking into an instance, skip static functions.
          if (!isMetatypeType &&
              isa<FuncDecl>(Result) &&
              cast<FuncDecl>(Result)->isStatic())
            continue;

          // Classify this declaration.
          FoundAny = true;

          // Types are local or metatype members.
          if (auto TD = dyn_cast<TypeDecl>(Result)) {
            if (isa<GenericTypeParamDecl>(TD))
              Results.push_back(UnqualifiedLookupResult(Result));
            else
              Results.push_back(UnqualifiedLookupResult(MetaBaseDecl, Result));
            continue;
          } else if (auto FD = dyn_cast<FuncDecl>(Result)) {
            if (FD->isStatic() && !isMetatypeType)
              continue;
          } else if (isa<EnumElementDecl>(Result)) {
            Results.push_back(UnqualifiedLookupResult(BaseDecl, Result));
            continue;
          }

          Results.push_back(UnqualifiedLookupResult(BaseDecl, Result));
        }

        if (FoundAny) {
          // Predicate that determines whether a lookup result should
          // be unavailable except as a last-ditch effort.
          auto unavailableLookupResult =
            [&](const UnqualifiedLookupResult &result) {
            return result.getValueDecl()->getAttrs()
                     .isUnavailableInCurrentSwift();
          };

          // If all of the results we found are unavailable, keep looking.
          if (std::all_of(Results.begin(), Results.end(),
                          unavailableLookupResult)) {
            UnavailableInnerResults.append(Results.begin(), Results.end());
            Results.clear();
            FoundAny = false;
          } else {
            if (DebugClient)
              filterForDiscriminator(Results, DebugClient);
            return;
          }
        }

        // Check the generic parameters if our context is a generic type or
        // extension thereof.
        GenericParamList *dcGenericParams = nullptr;
        if (auto nominal = dyn_cast<NominalTypeDecl>(DC))
          dcGenericParams = nominal->getGenericParams();
        else if (auto ext = dyn_cast<ExtensionDecl>(DC))
          dcGenericParams = ext->getGenericParams();

        if (dcGenericParams) {
          namelookup::FindLocalVal localVal(SM, Loc, Consumer);
          localVal.checkGenericParams(dcGenericParams);

          if (!Results.empty())
            return;
        }
      }

      DC = DC->getParent();
    }

    if (!isCascadingUse.hasValue())
      isCascadingUse = true;
  }

  if (auto SF = dyn_cast<SourceFile>(DC)) {
    if (Loc.isValid()) {
      // Look for local variables in top-level code; normally, the parser
      // resolves these for us, but it can't do the right thing for
      // local types.
      namelookup::FindLocalVal localVal(SM, Loc, Consumer);
      localVal.checkSourceFile(*SF);
      if (!Results.empty())
        return;
    }
  }

  // TODO: Does the debugger client care about compound names?
  if (Name.isSimpleName()
      && DebugClient && DebugClient->lookupOverrides(Name.getBaseName(), DC,
                                                   Loc, IsTypeLookup, Results))
    return;

  recordLookupOfTopLevelName(DC, Name, isCascadingUse.getValue());

  // Add private imports to the extra search list.
  SmallVector<Module::ImportedModule, 8> extraImports;
  if (auto FU = dyn_cast<FileUnit>(DC))
    FU->getImportedModules(extraImports, Module::ImportFilter::Private);

  using namespace namelookup;
  SmallVector<ValueDecl *, 8> CurModuleResults;
  auto resolutionKind =
    IsTypeLookup ? ResolutionKind::TypesOnly : ResolutionKind::Overloadable;
  lookupInModule(&M, {}, Name, CurModuleResults, NLKind::UnqualifiedLookup,
                 resolutionKind, TypeResolver, DC, extraImports);

  for (auto VD : CurModuleResults)
    Results.push_back(UnqualifiedLookupResult(VD));

  if (DebugClient)
    filterForDiscriminator(Results, DebugClient);

  // Now add any names the DebugClient knows about to the lookup.
  if (Name.isSimpleName() && DebugClient)
      DebugClient->lookupAdditions(Name.getBaseName(), DC, Loc, IsTypeLookup,
                                   Results);

  // If we've found something, we're done.
  if (!Results.empty())
    return;

  // If we still haven't found anything, but we do have some
  // declarations that are "unavailable in the current Swift", drop
  // those in.
  if (!UnavailableInnerResults.empty()) {
    Results = std::move(UnavailableInnerResults);
    return;
  }

  if (!Name.isSimpleName())
    return;

  // Look for a module with the given name.
  if (Name.isSimpleName(M.getName())) {
    Results.push_back(UnqualifiedLookupResult(&M));
    return;
  }

  Module *desiredModule = Ctx.getLoadedModule(Name.getBaseName());
  if (!desiredModule && Name == Ctx.TheBuiltinModule->getName())
    desiredModule = Ctx.TheBuiltinModule;
  if (desiredModule) {
    forAllVisibleModules(DC, [&](const Module::ImportedModule &import) -> bool {
      if (import.second == desiredModule) {
        Results.push_back(UnqualifiedLookupResult(import.second));
        return false;
      }
      return true;
    });
  }
}

TypeDecl* UnqualifiedLookup::getSingleTypeResult() {
  if (Results.size() != 1)
    return nullptr;
  return dyn_cast<TypeDecl>(Results.back().getValueDecl());
}

#pragma mark Member lookup table

void LazyMemberLoader::anchor() {}

/// Lookup table used to store members of a nominal type (and its extensions)
/// for fast retrieval.
class swift::MemberLookupTable {
  /// The last extension that was included within the member lookup table's
  /// results.
  ExtensionDecl *LastExtensionIncluded = nullptr;

  /// The type of the internal lookup table.
  typedef llvm::DenseMap<DeclName, llvm::TinyPtrVector<ValueDecl *>>
    LookupTable;

  /// Lookup table mapping names to the set of declarations with that name.
  LookupTable Lookup;

public:
  /// Create a new member lookup table.
  explicit MemberLookupTable(ASTContext &ctx);

  /// Destroy the lookup table.
  void destroy();

  /// Update a lookup table with members from newly-added extensions.
  void updateLookupTable(NominalTypeDecl *nominal);

  /// \brief Add the given member to the lookup table.
  void addMember(Decl *members);

  /// \brief Add the given members to the lookup table.
  void addMembers(DeclRange members);

  /// \brief The given extension has been extended with new members; add them
  /// if appropriate.
  void addExtensionMembers(NominalTypeDecl *nominal,
                           ExtensionDecl *ext,
                           DeclRange members);

  /// Iterator into the lookup table.
  typedef LookupTable::iterator iterator;

  iterator begin() { return Lookup.begin(); }
  iterator end() { return Lookup.end(); }

  iterator find(DeclName name) {
    return Lookup.find(name);
  }

  // Only allow allocation of member lookup tables using the allocator in
  // ASTContext or by doing a placement new.
  void *operator new(size_t Bytes, ASTContext &C,
                     unsigned Alignment = alignof(MemberLookupTable)) {
    return C.Allocate(Bytes, Alignment);
  }
  void *operator new(size_t Bytes, void *Mem) {
    assert(Mem);
    return Mem;
  }
};

namespace {
  /// Stores the set of Objective-C methods with a given selector within the
  /// Objective-C method lookup table.
  struct StoredObjCMethods {
    /// The generation count at which this list was last updated.
    unsigned Generation = 0;

    /// The set of methods with the given selector.
    llvm::TinyPtrVector<AbstractFunctionDecl *> Methods;
  };
}

/// Class member lookup table, which is a member lookup table with a second
/// table for lookup based on Objective-C selector.
class ClassDecl::ObjCMethodLookupTable
        : public llvm::DenseMap<std::pair<ObjCSelector, char>,
                                StoredObjCMethods>
{
public:
  void destroy() {
    this->~ObjCMethodLookupTable();
  }

  // Only allow allocation of member lookup tables using the allocator in
  // ASTContext or by doing a placement new.
  void *operator new(size_t Bytes, ASTContext &C,
                     unsigned Alignment = alignof(MemberLookupTable)) {
    return C.Allocate(Bytes, Alignment);
  }
  void *operator new(size_t Bytes, void *Mem) {
    assert(Mem);
    return Mem;
  }
};

MemberLookupTable::MemberLookupTable(ASTContext &ctx) {
  // Register a cleanup with the ASTContext to call the lookup table
  // destructor.
  ctx.addCleanup([this]() {
    this->destroy();
  });
}

void MemberLookupTable::addMember(Decl *member) {
  // Only value declarations matter.
  auto vd = dyn_cast<ValueDecl>(member);
  if (!vd)
    return;

  // Unnamed entities cannot be found by name lookup.
  if (!vd->hasName())
    return;

  // If this declaration is already in the lookup table, don't add it
  // again.
  if (vd->ValueDeclBits.AlreadyInLookupTable) {
    return;
  }
  vd->ValueDeclBits.AlreadyInLookupTable = true;

  // Add this declaration to the lookup set under its compound name and simple
  // name.
  vd->getFullName().addToLookupTable(Lookup, vd);
}

void MemberLookupTable::addMembers(DeclRange members) {
  for (auto member : members) {
    addMember(member);
  }
}

void MemberLookupTable::addExtensionMembers(NominalTypeDecl *nominal,
                                            ExtensionDecl *ext,
                                            DeclRange members) {
  // We have not processed any extensions yet, so there's nothing to do.
  if (!LastExtensionIncluded)
    return;

  // If this extension shows up in the list of extensions not yet included
  // in the lookup table, there's nothing to do.
  for (auto notIncluded = LastExtensionIncluded->NextExtension.getPointer();
       notIncluded;
       notIncluded = notIncluded->NextExtension.getPointer()) {
    if (notIncluded == ext)
      return;
  }

  // Add the new members to the lookup table.
  addMembers(members);
}

void MemberLookupTable::updateLookupTable(NominalTypeDecl *nominal) {
  // If the last extension we included is the same as the last known extension,
  // we're already up-to-date.
  if (LastExtensionIncluded == nominal->LastExtension)
    return;

  // Add members from each of the extensions that we have not yet visited.
  for (auto next = LastExtensionIncluded
                     ? LastExtensionIncluded->NextExtension.getPointer()
                     : nominal->FirstExtension;
       next;
       (LastExtensionIncluded = next,next = next->NextExtension.getPointer())) {
    addMembers(next->getMembers());
  }
}

void MemberLookupTable::destroy() {
  this->~MemberLookupTable();
}

void NominalTypeDecl::addedMember(Decl *member) {
  // If we have a lookup table, add the new member to it.
  if (LookupTable.getPointer()) {
    LookupTable.getPointer()->addMember(member);
  }
}

void ExtensionDecl::addedMember(Decl *member) {
  if (NextExtension.getInt()) {
    if (getExtendedType()->is<ErrorType>())
      return;

    auto nominal = getExtendedType()->getAnyNominal();
    if (nominal->LookupTable.getPointer()) {
      // Make sure we have the complete list of extensions.
      // FIXME: This is completely unnecessary. We want to determine whether
      // our own extension has already been included in the lookup table.
      (void)nominal->getExtensions();

      nominal->LookupTable.getPointer()->addMember(member);
    }
  }
}

void NominalTypeDecl::prepareLookupTable(bool ignoreNewExtensions) {
  // If we haven't allocated the lookup table yet, do so now.
  if (!LookupTable.getPointer()) {
    auto &ctx = getASTContext();
    LookupTable.setPointer(new (ctx) MemberLookupTable(ctx));
  }

  // If we haven't walked the member list yet to update the lookup
  // table, do so now.
  if (!LookupTable.getInt()) {
    // Note that we'll have walked the members now.
    LookupTable.setInt(true);

    // Add the members of the nominal declaration to the table.
    LookupTable.getPointer()->addMembers(getMembers());
  }

  if (!ignoreNewExtensions) {
    // Update the lookup table to introduce members from extensions.
    LookupTable.getPointer()->updateLookupTable(this);
  }
}

void NominalTypeDecl::makeMemberVisible(ValueDecl *member) {
  if (!LookupTable.getPointer()) {
    auto &ctx = getASTContext();
    LookupTable.setPointer(new (ctx) MemberLookupTable(ctx));
  }
  
  LookupTable.getPointer()->addMember(member);
}

ArrayRef<ValueDecl *> NominalTypeDecl::lookupDirect(DeclName name,
                                                    bool ignoreNewExtensions) {
  // Make sure we have the complete list of members (in this nominal and in all
  // extensions).
  if (!ignoreNewExtensions) {
    for (auto E : getExtensions())
      (void)E->getMembers();
  }

  (void)getMembers();

  prepareLookupTable(ignoreNewExtensions);

  // Look for the declarations with this name.
  auto known = LookupTable.getPointer()->find(name);
  if (known == LookupTable.getPointer()->end())
    return { };

  // We found something; return it.
  return { known->second.begin(), known->second.size() };
}

void ClassDecl::createObjCMethodLookup() {
  assert(!ObjCMethodLookup && "Already have an Objective-C member table");
  auto &ctx = getASTContext();
  ObjCMethodLookup = new (ctx) ObjCMethodLookupTable();

  // Register a cleanup with the ASTContext to call the lookup table
  // destructor.
  ctx.addCleanup([this]() {
    this->ObjCMethodLookup->destroy();
  });
}

MutableArrayRef<AbstractFunctionDecl *>
ClassDecl::lookupDirect(ObjCSelector selector, bool isInstance) {
  if (!ObjCMethodLookup) {
    createObjCMethodLookup();
  }

  // If any modules have been loaded since we did the search last (or if we
  // hadn't searched before), look in those modules, too.
  auto &stored = (*ObjCMethodLookup)[{selector, isInstance}];
  ASTContext &ctx = getASTContext();
  if (ctx.getCurrentGeneration() > stored.Generation) {
    ctx.loadObjCMethods(this, selector, isInstance, stored.Generation,
                        stored.Methods);
    stored.Generation = ctx.getCurrentGeneration();
  }

  return { stored.Methods.begin(), stored.Methods.end() };
}

void ClassDecl::recordObjCMethod(AbstractFunctionDecl *method) {
  if (!ObjCMethodLookup) {
    createObjCMethodLookup();
  }

  assert(method->isObjC() && "Not an Objective-C method");

  // Record the method.
  bool isInstanceMethod = method->isObjCInstanceMethod();
  auto selector = method->getObjCSelector();
  auto &vec = (*ObjCMethodLookup)[{selector, isInstanceMethod}].Methods;

  // In a non-empty vector, we could have duplicates or conflicts.
  if (!vec.empty()) {
    // Check whether we have a duplicate. This only checks more than one
    // element in ill-formed code, so the linear search is acceptable.
    if (std::find(vec.begin(), vec.end(), method) != vec.end())
      return;

    if (vec.size() == 1) {
      // We have a conflict.
      getASTContext().recordObjCMethodConflict(this, selector,
                                               isInstanceMethod);
    }
  } else {
    // Record the first method that has this selector.
    getASTContext().recordObjCMethod(method);
  }

  vec.push_back(method);
}

static bool checkAccessibility(const DeclContext *useDC,
                               const DeclContext *sourceDC,
                               Accessibility access) {
  if (!useDC)
    return access >= Accessibility::Public;

  assert(sourceDC && "ValueDecl being accessed must have a valid DeclContext");
  switch (access) {
  case Accessibility::Private:
    if (sourceDC->getASTContext().LangOpts.EnableSwift3Private)
      return useDC == sourceDC || useDC->isChildContextOf(sourceDC);
    SWIFT_FALLTHROUGH;
  case Accessibility::FilePrivate:
    return useDC->getModuleScopeContext() == sourceDC->getModuleScopeContext();
  case Accessibility::Internal: {
    const Module *sourceModule = sourceDC->getParentModule();
    const DeclContext *useFile = useDC->getModuleScopeContext();
    if (useFile->getParentModule() == sourceModule)
      return true;
    if (auto *useSF = dyn_cast<SourceFile>(useFile))
      if (useSF->hasTestableImport(sourceModule))
        return true;
    return false;
  }
  case Accessibility::Public:
  case Accessibility::Open:
    return true;
  }
  llvm_unreachable("bad Accessibility");
}

bool ValueDecl::isAccessibleFrom(const DeclContext *DC) const {
  return checkAccessibility(DC, getDeclContext(), getFormalAccess());
}

bool AbstractStorageDecl::isSetterAccessibleFrom(const DeclContext *DC) const {
  assert(isSettable(DC));

  // If a stored property does not have a setter, it is still settable from the
  // designated initializer constructor. In this case, don't check setter
  // accessibility, it is not set.
  if (hasStorage() && !isSettable(nullptr))
    return true;
  
  return checkAccessibility(DC, getDeclContext(), getSetterAccessibility());
}

bool DeclContext::lookupQualified(Type type,
                                  DeclName member,
                                  NLOptions options,
                                  LazyResolver *typeResolver,
                                  SmallVectorImpl<ValueDecl *> &decls) const {
  using namespace namelookup;
  assert(decls.empty() && "additive lookup not supported");

  if (type->is<ErrorType>())
    return false;

  auto checkLookupCascading = [this, options]() -> Optional<bool> {
    switch (static_cast<unsigned>(options & NL_KnownDependencyMask)) {
    case 0:
      return isCascadingContextForLookup(/*excludeFunctions=*/false);
    case NL_KnownNonCascadingDependency:
      return false;
    case NL_KnownCascadingDependency:
      return true;
    case NL_KnownNoDependency:
      return None;
    default:
      // FIXME: Use llvm::CountPopulation_64 when that's declared constexpr.
      static_assert(__builtin_popcountll(NL_KnownDependencyMask) == 2,
                    "mask should only include four values");
      llvm_unreachable("mask only includes four values");
    }
  };

  // Look through lvalue and inout types.
  type = type->getLValueOrInOutObjectType();

  // Look through metatypes.
  if (auto metaTy = type->getAs<AnyMetatypeType>())
    type = metaTy->getInstanceType();

  // Look through DynamicSelf.
  if (auto dynamicSelf = type->getAs<DynamicSelfType>())
    type = dynamicSelf->getSelfType();

  // Look for module references.
  if (auto moduleTy = type->getAs<ModuleType>()) {
    Module *module = moduleTy->getModule();
    auto topLevelScope = getModuleScopeContext();
    if (module == topLevelScope->getParentModule()) {
      if (auto maybeLookupCascade = checkLookupCascading()) {
        recordLookupOfTopLevelName(topLevelScope, member,
                                   maybeLookupCascade.getValue());
      }
      lookupInModule(module, /*accessPath=*/{}, member, decls,
                     NLKind::QualifiedLookup, ResolutionKind::Overloadable,
                     typeResolver, topLevelScope);
    } else {
      // Note: This is a lookup into another module. Unless we're compiling
      // multiple modules at once, or if the other module re-exports this one,
      // it shouldn't be possible to have a dependency from that module on
      // anything in this one.

      // Perform the lookup in all imports of this module.
      forAllVisibleModules(this,
                           [&](const Module::ImportedModule &import) -> bool {
        if (import.second != module)
          return true;
        lookupInModule(import.second, import.first, member, decls,
                       NLKind::QualifiedLookup, ResolutionKind::Overloadable,
                       typeResolver, topLevelScope);
        // If we're able to do an unscoped lookup, we see everything. No need
        // to keep going.
        return !import.first.empty();
      });
    }

    llvm::SmallPtrSet<ValueDecl *, 4> knownDecls;
    decls.erase(std::remove_if(decls.begin(), decls.end(),
                               [&](ValueDecl *vd) -> bool {
      // If we're performing a type lookup, don't even attempt to validate
      // the decl if its not a type.
      if ((options & NL_OnlyTypes) && !isa<TypeDecl>(vd))
        return true;

      return !knownDecls.insert(vd).second;
    }), decls.end());

    if (auto *debugClient = topLevelScope->getParentModule()->getDebugClient())
      filterForDiscriminator(decls, debugClient);
    
    return !decls.empty();
  }

  auto &ctx = getASTContext();
  if (!ctx.LangOpts.EnableAccessControl)
    options |= NL_IgnoreAccessibility;

  // The set of nominal type declarations we should (and have) visited.
  SmallVector<NominalTypeDecl *, 4> stack;
  llvm::SmallPtrSet<NominalTypeDecl *, 4> visited;

  // Handle nominal types.
  bool wantProtocolMembers = false;
  bool wantLookupInAllClasses = false;
  if (auto nominal = type->getAnyNominal()) {
    visited.insert(nominal);
    stack.push_back(nominal);
    
    wantProtocolMembers = (options & NL_ProtocolMembers) &&
                          !isa<ProtocolDecl>(nominal);

    // If we want dynamic lookup and we're searching in the
    // AnyObject protocol, note this for later.
    if (options & NL_DynamicLookup) {
      if (auto proto = dyn_cast<ProtocolDecl>(nominal)) {
        if (proto->isSpecificProtocol(KnownProtocolKind::AnyObject))
          wantLookupInAllClasses = true;
      }
    }
  }
  // Handle archetypes
  else if (auto archetypeTy = type->getAs<ArchetypeType>()) {
    // Look in the protocols to which the archetype conforms (always).
    for (auto proto : archetypeTy->getConformsTo())
      if (visited.insert(proto).second)
        stack.push_back(proto);

    // If requested, look into the superclasses of this archetype.
    if (options & NL_VisitSupertypes) {
      if (auto superclassTy = archetypeTy->getSuperclass()) {
        if (auto superclassDecl = superclassTy->getAnyNominal()) {
          if (visited.insert(superclassDecl).second) {
            stack.push_back(superclassDecl);

            wantProtocolMembers = (options & NL_ProtocolMembers) &&
                                  !isa<ProtocolDecl>(superclassDecl);
          }
        }
      }
    }
  }
  // Handle protocol compositions.
  else if (auto compositionTy = type->getAs<ProtocolCompositionType>()) {
    SmallVector<ProtocolDecl *, 4> protocols;
    if (compositionTy->isExistentialType(protocols)) {
      for (auto proto : protocols) {
        if (visited.insert(proto).second) {
          stack.push_back(proto);

          // If we want dynamic lookup and this is the AnyObject
          // protocol, note this for later.
          if ((options & NL_DynamicLookup) &&
              proto->isSpecificProtocol(KnownProtocolKind::AnyObject))
            wantLookupInAllClasses = true;
        }
      }
    }
  }

  // Allow filtering of the visible declarations based on various
  // criteria.
  bool onlyCompleteObjectInits = false;
  auto isAcceptableDecl = [&](NominalTypeDecl *current, Decl *decl) -> bool {
    // If the decl is currently being type checked, then we have something
    // cyclic going on.  Instead of poking at parts that are potentially not
    // set up, just assume it is acceptable.  This will make sure we produce an
    // error later.
    if (decl->isBeingTypeChecked())
      return true;
    
    // Filter out designated initializers, if requested.
    if (onlyCompleteObjectInits) {
      if (auto ctor = dyn_cast<ConstructorDecl>(decl)) {
        if (!ctor->isInheritable())
          return false;
      } else {
        return false;
      }
    }

    // Ignore stub implementations.
    if (auto ctor = dyn_cast<ConstructorDecl>(decl)) {
      if (ctor->hasStubImplementation())
        return false;
    }

    // Check access.
    if (!(options & NL_IgnoreAccessibility))
      if (auto VD = dyn_cast<ValueDecl>(decl))
        return VD->isAccessibleFrom(this);

    return true;
  };

  ReferencedNameTracker *tracker = nullptr;
  if (auto containingSourceFile = dyn_cast<SourceFile>(getModuleScopeContext()))
    tracker = containingSourceFile->getReferencedNameTracker();

  bool isLookupCascading;
  if (tracker) {
    if (auto maybeLookupCascade = checkLookupCascading())
      isLookupCascading = maybeLookupCascade.getValue();
    else
      tracker = nullptr;
  }

  // Visit all of the nominal types we know about, discovering any others
  // we need along the way.
  while (!stack.empty()) {
    auto current = stack.back();
    stack.pop_back();

    if (tracker)
      tracker->addUsedMember({current, member.getBaseName()},isLookupCascading);

    // Make sure we've resolved implicit constructors, if we need them.
    if (member.getBaseName() == ctx.Id_init && typeResolver)
      typeResolver->resolveImplicitConstructors(current);

    // Look for results within the current nominal type and its extensions.
    bool currentIsProtocol = isa<ProtocolDecl>(current);
    for (auto decl : current->lookupDirect(member)) {
      // If we're performing a type lookup, don't even attempt to validate
      // the decl if its not a type.
      if ((options & NL_OnlyTypes) && !isa<TypeDecl>(decl))
        continue;

      // Resolve the declaration signature when we find the
      // declaration.
      if (typeResolver && !decl->isBeingTypeChecked()) {
        typeResolver->resolveDeclSignature(decl);

        if (!decl->hasType())
          continue;
      }

      if (isAcceptableDecl(current, decl))
        decls.push_back(decl);
    }

    // If we're not supposed to visit our supertypes, we're done.
    if ((options & NL_VisitSupertypes) == 0)
      continue;

    // Visit superclass.
    if (auto classDecl = dyn_cast<ClassDecl>(current)) {
      // If we're looking for initializers, only look at the superclass if the
      // current class permits inheritance. Even then, only find complete
      // object initializers.
      bool visitSuperclass = true;
      if (member.getBaseName() == ctx.Id_init) {
        if (classDecl->inheritsSuperclassInitializers(typeResolver))
          onlyCompleteObjectInits = true;
        else
          visitSuperclass = false;
      }

      if (visitSuperclass) {
        if (auto superclassType = classDecl->getSuperclass())
          if (auto superclassDecl = superclassType->getClassOrBoundGenericClass())
            if (visited.insert(superclassDecl).second)
              stack.push_back(superclassDecl);
      }
    }

    // If we're not looking at a protocol and we're not supposed to
    // visit the protocols that this type conforms to, skip the next
    // step.
    if (!wantProtocolMembers && !currentIsProtocol)
      continue;

    SmallVector<ProtocolDecl *, 4> protocols;
    for (auto proto : current->getAllProtocols()) {
      if (visited.insert(proto).second) {
        stack.push_back(proto);
      }
    }

    // For a class, we don't need to visit the protocol members of the
    // superclass: that's already handled.
    if (isa<ClassDecl>(current))
      wantProtocolMembers = false;
  }

  // If we want to perform lookup into all classes, do so now.
  if (wantLookupInAllClasses) {
    if (tracker)
      tracker->addDynamicLookupName(member.getBaseName(), isLookupCascading);

    // Collect all of the visible declarations.
    SmallVector<ValueDecl *, 4> allDecls;
    forAllVisibleModules(this, [&](Module::ImportedModule import) {
      import.second->lookupClassMember(import.first, member, allDecls);
    });

    // For each declaration whose context is not something we've
    // already visited above, add it to the list of declarations.
    llvm::SmallPtrSet<ValueDecl *, 4> knownDecls;
    for (auto decl : allDecls) {
      // If we're performing a type lookup, don't even attempt to validate
      // the decl if its not a type.
      if ((options & NL_OnlyTypes) && !isa<TypeDecl>(decl))
        continue;

      if (typeResolver && !decl->isBeingTypeChecked()) {
        typeResolver->resolveDeclSignature(decl);
        if (!decl->hasType())
          continue;
      }

      // If the declaration has an override, name lookup will also have
      // found the overridden method. Skip this declaration, because we
      // prefer the overridden method.
      if (decl->getOverriddenDecl())
        continue;

      auto dc = decl->getDeclContext();
      auto nominal = dyn_cast<NominalTypeDecl>(dc);
      if (!nominal) {
        auto ext = cast<ExtensionDecl>(dc);
        nominal = ext->getExtendedType()->getAnyNominal();
        assert(nominal && "Couldn't find nominal type?");
      }

      // If we didn't visit this nominal type above, add this
      // declaration to the list.
      if (!visited.count(nominal) && knownDecls.insert(decl).second &&
          isAcceptableDecl(nominal, decl))
        decls.push_back(decl);
    }
  }

  // If we're supposed to remove overridden declarations, do so now.
  if (options & NL_RemoveOverridden)
    removeOverriddenDecls(decls);

  // If we're supposed to remove shadowed/hidden declarations, do so now.
  Module *M = getParentModule();
  if (options & NL_RemoveNonVisible)
    removeShadowedDecls(decls, M, typeResolver);

  if (auto *debugClient = M->getDebugClient())
    filterForDiscriminator(decls, debugClient);

  // We're done. Report success/failure.
  return !decls.empty();
}

void DeclContext::lookupAllObjCMethods(
       ObjCSelector selector,
       SmallVectorImpl<AbstractFunctionDecl *> &results) const {
  // Collect all of the methods with this selector.
  forAllVisibleModules(this, [&](Module::ImportedModule import) {
    import.second->lookupObjCMethods(selector, results);
  });

  // Filter out duplicates.
  llvm::SmallPtrSet<AbstractFunctionDecl *, 8> visited;
  results.erase(
    std::remove_if(results.begin(), results.end(),
                   [&](AbstractFunctionDecl *func) -> bool {
                     return !visited.insert(func).second;
                   }),
    results.end());
}
