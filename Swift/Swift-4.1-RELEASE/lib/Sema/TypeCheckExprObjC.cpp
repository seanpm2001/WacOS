//===--- TypeCheckExprObjC.cpp - Type Checking for ObjC Expressions -------===//
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
// This file implements semantic analysis for Objective-C-specific
// expressions.
//
//===----------------------------------------------------------------------===//
#include "TypeChecker.h"
#include "swift/Basic/Range.h"

using namespace swift;

Optional<Type> TypeChecker::checkObjCKeyPathExpr(DeclContext *dc,
                                                 KeyPathExpr *expr,
                                                 bool requireResultType) {
  // TODO: Native keypaths
  assert(expr->isObjC() && "native keypaths not type-checked this way");
  
  // If there is already a semantic expression, do nothing.
  if (expr->getObjCStringLiteralExpr() && !requireResultType) return None;

  // ObjC #keyPath only makes sense when we have the Objective-C runtime.
  if (!Context.LangOpts.EnableObjCInterop) {
    diagnose(expr->getLoc(), diag::expr_keypath_no_objc_runtime);

    expr->setObjCStringLiteralExpr(
      new (Context) StringLiteralExpr("", expr->getSourceRange(),
                                      /*Implicit=*/true));
    return None;
  }

  // The key path string we're forming.
  SmallString<32> keyPathScratch;
  llvm::raw_svector_ostream keyPathOS(keyPathScratch);

  // Captures the state of semantic resolution.
  enum State {
    Beginning,
    ResolvingType,
    ResolvingProperty,
    ResolvingArray,
    ResolvingSet,
    ResolvingDictionary,
  } state = Beginning;
  
  /// Determine whether we are currently resolving a property.
  auto isResolvingProperty = [&] {
    switch (state) {
    case Beginning:
    case ResolvingType:
      return false;

    case ResolvingProperty:
    case ResolvingArray:
    case ResolvingSet:
    case ResolvingDictionary:
      return true;
    }

    llvm_unreachable("Unhandled State in switch.");
  };

  // The type of AnyObject, which is used whenever we don't have
  // sufficient type information.
  Type anyObjectType = Context.getAnyObjectType();

  // Local function to update the state after we've resolved a
  // component.
  Type currentType;
  auto updateState = [&](bool isProperty, Type newType) {
    // Strip off optionals.
    newType = newType->lookThroughAllAnyOptionalTypes();

    // If updating to a type, just set the new type; there's nothing
    // more to do.
    if (!isProperty) {
      assert(state == Beginning || state == ResolvingType);
      state = ResolvingType;
      currentType = newType;
      return;
    }

    // We're updating to a property. Determine whether we're looking
    // into a bridged Swift collection of some sort.
    if (auto boundGeneric = newType->getAs<BoundGenericType>()) {
      auto nominal = boundGeneric->getDecl();

      // Array<T>
      if (nominal == Context.getArrayDecl()) {
        // Further lookups into the element type.
        state = ResolvingArray;
        currentType = boundGeneric->getGenericArgs()[0];
        return;
      }

      // Set<T>
      if (nominal == Context.getSetDecl()) {
        // Further lookups into the element type.
        state = ResolvingSet;
        currentType = boundGeneric->getGenericArgs()[0];
        return;
      }

      // Dictionary<K, V>
      if (nominal == Context.getDictionaryDecl()) {
        // Key paths look into the keys of a dictionary; further
        // lookups into the value type.
        state = ResolvingDictionary;
        currentType = boundGeneric->getGenericArgs()[1];
        return;
      }
    }

    // Determine whether we're looking into a Foundation collection.
    if (auto classDecl = newType->getClassOrBoundGenericClass()) {
      if (classDecl->isObjC() && classDecl->hasClangNode()) {
        SmallString<32> scratch;
        StringRef objcClassName = classDecl->getObjCRuntimeName(scratch);

        // NSArray
        if (objcClassName == "NSArray") {
          // The element type is unknown, so use AnyObject.
          state = ResolvingArray;
          currentType = anyObjectType;
          return;
        }

        // NSSet
        if (objcClassName == "NSSet") {
          // The element type is unknown, so use AnyObject.
          state = ResolvingSet;
          currentType = anyObjectType;
          return;
        }

        // NSDictionary
        if (objcClassName == "NSDictionary") {
          // Key paths look into the keys of a dictionary; there's no
          // type to help us here.
          state = ResolvingDictionary;
          currentType = anyObjectType;
          return;
        }
      }
    }

    // It's just a property.
    state = ResolvingProperty;
    currentType = newType;
  };
  
  // Local function to perform name lookup for the current index.
  auto performLookup = [&](DeclBaseName componentName,
                           SourceLoc componentNameLoc,
                           Type &lookupType) -> LookupResult {
    if (state == Beginning)
      return lookupUnqualified(dc, componentName, componentNameLoc);

    assert(currentType && "Non-beginning state must have a type");
    if (!currentType->mayHaveMembers())
      return LookupResult();

    // Determine the type in which the lookup should occur. If we have
    // a bridged value type, this will be the Objective-C class to
    // which it is bridged.
    if (auto bridgedClass = Context.getBridgedToObjC(dc, currentType))
      lookupType = bridgedClass;
    else
      lookupType = currentType;

    // Look for a member with the given name within this type.
    return lookupMember(dc, lookupType, componentName);
  };

  // Local function to print a component to the string.
  bool needDot = false;
  auto printComponent = [&](DeclBaseName component) {
    if (needDot)
      keyPathOS << ".";
    else
      needDot = true;

    keyPathOS << component;
  };

  bool isInvalid = false;
  SmallVector<KeyPathExpr::Component, 4> resolvedComponents;
  
  for (auto &component : expr->getComponents()) {
    auto componentNameLoc = component.getLoc();
    
    // ObjC keypaths only support named segments.
    // TODO: Perhaps we can map subscript components to dictionary keys.
    switch (auto kind = component.getKind()) {
    case KeyPathExpr::Component::Kind::Invalid:
      continue;

    case KeyPathExpr::Component::Kind::UnresolvedProperty:
      break;
    case KeyPathExpr::Component::Kind::UnresolvedSubscript:
    case KeyPathExpr::Component::Kind::OptionalChain:
    case KeyPathExpr::Component::Kind::OptionalForce:
      diagnose(componentNameLoc,
               diag::expr_unsupported_objc_key_path_component,
               (unsigned)kind);
      continue;
    case KeyPathExpr::Component::Kind::OptionalWrap:
    case KeyPathExpr::Component::Kind::Property:
    case KeyPathExpr::Component::Kind::Subscript:
      llvm_unreachable("already resolved!");
    }
    
    auto componentFullName = component.getUnresolvedDeclName();
    if (!componentFullName.isSimpleName()) {
      diagnose(componentNameLoc,
               diag::expr_unsupported_objc_key_path_compound_name);
      continue;
    }
    auto componentName = componentFullName.getBaseName();

    // If we are resolving into a dictionary, any component is
    // well-formed because the keys are unknown dynamically.
    if (state == ResolvingDictionary) {
      // Just print the component unchanged; there's no checking we
      // can do here.
      printComponent(componentName);

      // From here, we're resolving a property. Use the current type.
      updateState(/*isProperty=*/true, currentType);

      continue;
    }

    // Look for this component.
    Type lookupType;
    LookupResult lookup = performLookup(componentName, componentNameLoc,
                                        lookupType);

    // If we didn't find anything, try to apply typo-correction.
    bool resultsAreFromTypoCorrection = false;
    if (!lookup) {
      performTypoCorrection(dc, DeclRefKind::Ordinary, lookupType,
                            componentName, componentNameLoc,
                            (lookupType ? defaultMemberTypeLookupOptions
                                        : defaultUnqualifiedLookupOptions),
                            lookup);

      if (currentType)
        diagnose(componentNameLoc, diag::could_not_find_type_member,
                 currentType, componentName);
      else
        diagnose(componentNameLoc, diag::use_unresolved_identifier,
                 componentName, false);

      // Note all the correction candidates.
      for (auto &result : lookup) {
        noteTypoCorrection(componentName, DeclNameLoc(componentNameLoc),
                           result.getValueDecl());
      }

      isInvalid = true;
      if (!lookup) break;

      // Remember that these are from typo correction.
      resultsAreFromTypoCorrection = true;
    }

    // If we have more than one result, filter out unavailable or
    // obviously unusable candidates.
    if (lookup.size() > 1) {
      lookup.filter([&](LookupResultEntry result) -> bool {
          // Drop unavailable candidates.
          if (result.getValueDecl()->getAttrs().isUnavailable(Context))
            return false;

          // Drop non-property, non-type candidates.
          if (!isa<VarDecl>(result.getValueDecl()) &&
              !isa<TypeDecl>(result.getValueDecl()))
            return false;

          return true;
      });
    }

    // If we *still* have more than one result, fail.
    if (lookup.size() > 1) {
      // Don't diagnose ambiguities if the results are from typo correction.
      if (resultsAreFromTypoCorrection)
        break;

      if (lookupType)
        diagnose(componentNameLoc, diag::ambiguous_member_overload_set,
                 componentName);
      else
        diagnose(componentNameLoc, diag::ambiguous_decl_ref,
                 componentName);

      for (auto result : lookup) {
        diagnose(result.getValueDecl(), diag::decl_declared_here,
                 result.getValueDecl()->getFullName());
      }
      isInvalid = true;
      break;
    }

    auto found = lookup.front().getValueDecl();

    // Handle property references.
    if (auto var = dyn_cast<VarDecl>(found)) {
      validateDecl(var);

      // Resolve this component to the variable we found.
      auto varRef = ConcreteDeclRef(var);
      auto resolved =
        KeyPathExpr::Component::forProperty(varRef, Type(), componentNameLoc);
      resolvedComponents.push_back(resolved);
      updateState(/*isProperty=*/true,
                  var->getInterfaceType()->getRValueObjectType());

      // Check that the property is @objc.
      if (!var->isObjC()) {
        diagnose(componentNameLoc, diag::expr_keypath_non_objc_property,
                 componentName);
        if (var->getLoc().isValid() && var->getDeclContext()->isTypeContext()) {
          diagnose(var, diag::make_decl_objc,
                   var->getDescriptiveKind())
            .fixItInsert(var->getAttributeInsertionLoc(false),
                         "@objc ");
        }
      } else if (auto attr = var->getAttrs().getAttribute<ObjCAttr>()) {
        // If this attribute was inferred based on deprecated Swift 3 rules,
        // complain.
        if (attr->isSwift3Inferred() &&
            Context.LangOpts.WarnSwift3ObjCInference ==
              Swift3ObjCInferenceWarnings::Minimal) {
          diagnose(componentNameLoc, diag::expr_keypath_swift3_objc_inference,
                   var->getFullName(),
                   var->getDeclContext()
                    ->getAsNominalTypeOrNominalTypeExtensionContext()
                   ->getName());
          diagnose(var, diag::make_decl_objc, var->getDescriptiveKind())
            .fixItInsert(var->getAttributeInsertionLoc(false),
                         "@objc ");
        }
      } else {
        // FIXME: Warn about non-KVC-compliant getter/setter names?
      }

      // Print the Objective-C property name.
      printComponent(var->getObjCPropertyName());
      continue;
    }

    // Handle type references.
    if (auto type = dyn_cast<TypeDecl>(found)) {
      // We cannot refer to a type via a property.
      if (isResolvingProperty()) {
        diagnose(componentNameLoc, diag::expr_keypath_type_of_property,
                 componentName, currentType);
        isInvalid = true;
        break;
      }

      // We cannot refer to a generic type.
      if (type->getDeclaredInterfaceType()->hasTypeParameter()) {
        diagnose(componentNameLoc, diag::expr_keypath_generic_type,
                 componentName);
        isInvalid = true;
        break;
      }

      Type newType;
      if (lookupType && !lookupType->isAnyObject()) {
        newType = lookupType->getTypeOfMember(dc->getParentModule(), type,
                                              type->getDeclaredInterfaceType());
      } else {
        newType = type->getDeclaredInterfaceType();
      }
      if (!newType) {
        isInvalid = true;
        break;
      }

      updateState(/*isProperty=*/false, newType);
      continue;
    }

    // Declarations that cannot be part of a key-path.
    diagnose(componentNameLoc, diag::expr_keypath_not_property,
             found->getDescriptiveKind(), found->getFullName());
    isInvalid = true;
    break;
  }
  // A successful check of an ObjC keypath shouldn't add or remove components,
  // currently.
  if (resolvedComponents.size() == expr->getComponents().size())
    expr->resolveComponents(Context, resolvedComponents);

  // Check for an empty key-path string.
  auto keyPathString = keyPathOS.str();
  if (keyPathString.empty() && !isInvalid)
    diagnose(expr->getLoc(), diag::expr_keypath_empty);

  // Set the string literal expression for the ObjC key path.
  if (!expr->getObjCStringLiteralExpr()) {
    expr->setObjCStringLiteralExpr(
      new (Context) StringLiteralExpr(Context.AllocateCopy(keyPathString),
                                      expr->getSourceRange(),
                                      /*Implicit=*/true));
  }

  if (!currentType) return None;
  return currentType;
}
