//===--- TypeCheckAttr.cpp - Type Checking for Attributes -----------------===//
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
// This file implements semantic analysis for attributes.
//
//===----------------------------------------------------------------------===//

#include "TypeChecker.h"
#include "MiscDiagnostics.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/Types.h"
#include "swift/Parse/Lexer.h"
#include "swift/ClangImporter/ClangModule.h" // FIXME: SDK overlay semantics

using namespace swift;

namespace {
/// This visits each attribute on a decl early, before the majority of type
/// checking has been performed for the decl.  The visitor should return true if
/// the attribute is invalid and should be marked as such.
class AttributeEarlyChecker : public AttributeVisitor<AttributeEarlyChecker> {
  TypeChecker &TC;
  Decl *D;

public:
  AttributeEarlyChecker(TypeChecker &TC, Decl *D) : TC(TC), D(D) {}

  /// This emits a diagnostic with a fixit to remove the attribute.
  template<typename ...ArgTypes>
  void diagnoseAndRemoveAttr(DeclAttribute *attr, ArgTypes &&...Args) {
    TC.diagnose(attr->getLocation(), std::forward<ArgTypes>(Args)...)
      .fixItRemove(attr->getRangeWithAt());
    attr->setInvalid();
  }

  /// Deleting this ensures that all attributes are covered by the visitor
  /// below.
  bool visitDeclAttribute(DeclAttribute *A) = delete;

#define IGNORED_ATTR(X) void visit##X##Attr(X##Attr *) {}
  IGNORED_ATTR(CDecl)
  IGNORED_ATTR(SILGenName)
  IGNORED_ATTR(Available)
  IGNORED_ATTR(Convenience)
  IGNORED_ATTR(Effects)
  IGNORED_ATTR(Exported)
  IGNORED_ATTR(FixedLayout)
  IGNORED_ATTR(Infix)
  IGNORED_ATTR(Inline)
  IGNORED_ATTR(NSApplicationMain)
  IGNORED_ATTR(NSCopying)
  IGNORED_ATTR(NonObjC)
  IGNORED_ATTR(ObjC)
  IGNORED_ATTR(ObjCBridged)
  IGNORED_ATTR(ObjCNonLazyRealization)
  IGNORED_ATTR(Optional)
  IGNORED_ATTR(Postfix)
  IGNORED_ATTR(Prefix)
  IGNORED_ATTR(RawDocComment)
  IGNORED_ATTR(Required)
  IGNORED_ATTR(RequiresStoredPropertyInits)
  IGNORED_ATTR(Rethrows)
  IGNORED_ATTR(Semantics)
  IGNORED_ATTR(Specialize)
  IGNORED_ATTR(Swift3Migration)
  IGNORED_ATTR(SwiftNativeObjCRuntimeBase)
  IGNORED_ATTR(SynthesizedProtocol)
  IGNORED_ATTR(Testable)
  IGNORED_ATTR(UIApplicationMain)
  IGNORED_ATTR(UnsafeNoObjCTaggedPointer)
  IGNORED_ATTR(Versioned)
  IGNORED_ATTR(ShowInInterface)
  IGNORED_ATTR(DiscardableResult)
#undef IGNORED_ATTR

  // @noreturn has been replaced with a 'Never' return type.
  void visitNoReturnAttr(NoReturnAttr *attr) {
    if (auto FD = dyn_cast<FuncDecl>(D)) {
      auto &SM = TC.Context.SourceMgr;

      auto diag = TC.diagnose(attr->getLocation(),
                              diag::noreturn_not_supported);
      auto range = attr->getRangeWithAt();
      if (range.isValid())
        range.End = range.End.getAdvancedLoc(1);
      diag.fixItRemove(range);

      auto *last = FD->getParameterList(FD->getNumParameterLists() - 1);

      // If the declaration already has a result type, we're going
      // to change it to 'Never'.
      bool hadResultType = false;
      bool isEndOfLine = false;
      SourceLoc resultLoc;
      if (FD->getBodyResultTypeLoc().hasLocation()) {
        const auto &typeLoc = FD->getBodyResultTypeLoc();
        hadResultType = true;
        resultLoc = typeLoc.getSourceRange().Start;

      // If the function 'throws', insert the result type after the
      // 'throws'.
      } else {
        if (FD->getThrowsLoc().isValid()) {
          resultLoc = FD->getThrowsLoc();

        // Otherwise, insert the result type after the final parameter
        // list.
        } else if (last->getRParenLoc().isValid()) {
          resultLoc = last->getRParenLoc();
        }

        if (Lexer::getLocForEndOfToken(SM, resultLoc).getAdvancedLoc(1) ==
            Lexer::getLocForEndOfLine(SM, resultLoc))
          isEndOfLine = true;

        resultLoc = Lexer::getLocForEndOfToken(SM, resultLoc);
      }

      if (hadResultType) {
        diag.fixItReplace(resultLoc, "Never");
      } else {
        std::string fix = " -> Never";

        if (!isEndOfLine)
          fix = fix + " ";

        diag.fixItInsert(resultLoc, fix);
      }

      FD->getBodyResultTypeLoc() = TypeLoc::withoutLoc(
          TC.Context.getNeverType());
    }
  }

  void visitAlignmentAttr(AlignmentAttr *attr) {
    // Alignment must be a power of two.
    unsigned value = attr->Value;
    if (value == 0 || (value & (value - 1)) != 0)
      TC.diagnose(attr->getLocation(), diag::alignment_not_power_of_two);
  }

  void visitAutoClosureAttr(AutoClosureAttr *attr) {
    TC.checkAutoClosureAttr(cast<ParamDecl>(D), attr);
  }
  void visitNoEscapeAttr(NoEscapeAttr *attr) {
    TC.checkNoEscapeAttr(cast<ParamDecl>(D), attr);
  }

  void visitEscapingAttr(EscapingAttr *attr) {
    auto *PD = cast<ParamDecl>(D);
    auto *FTy = PD->getType()->getAs<FunctionType>();
    if (FTy == 0) {
      TC.diagnose(attr->getLocation(), diag::escaping_function_type);
      attr->setInvalid();
    }
  }

  void visitTransparentAttr(TransparentAttr *attr);
  void visitMutationAttr(DeclAttribute *attr);
  void visitMutatingAttr(MutatingAttr *attr) { visitMutationAttr(attr); }
  void visitNonMutatingAttr(NonMutatingAttr *attr) { visitMutationAttr(attr); }
  void visitDynamicAttr(DynamicAttr *attr);

  void visitOwnershipAttr(OwnershipAttr *attr) {
    TC.checkOwnershipAttr(cast<VarDecl>(D), attr);
  }

  void visitFinalAttr(FinalAttr *attr) {
    // Reject combining 'final' with 'open'.
    if (auto accessibility = D->getAttrs().getAttribute<AccessibilityAttr>()) {
      if (accessibility->getAccess() == Accessibility::Open) {
        TC.diagnose(attr->getLocation(), diag::open_decl_cannot_be_final,
                    D->getDescriptiveKind());
        return;
      }
    }

    // Accept and remove the 'final' attribute from members of protocol
    // extensions.
    if (D->getDeclContext()->getAsProtocolExtensionContext()) {
      D->getAttrs().removeAttribute(attr);
    }
  }

  void visitIndirectAttr(IndirectAttr *attr) {
    if (auto caseDecl = dyn_cast<EnumElementDecl>(D)) {
      // An indirect case should have a payload.
      if (caseDecl->getArgumentTypeLoc().isNull())
        TC.diagnose(attr->getLocation(),
                    diag::indirect_case_without_payload, caseDecl->getName());
      // If the enum is already indirect, its cases don't need to be.
      else if (caseDecl->getParentEnum()->getAttrs()
                 .hasAttribute<IndirectAttr>())
        TC.diagnose(attr->getLocation(),
                    diag::indirect_case_in_indirect_enum);
    }
  }

  void visitWarnUnqualifiedAccessAttr(WarnUnqualifiedAccessAttr *attr) {
    if (!D->getDeclContext()->isTypeContext()) {
      diagnoseAndRemoveAttr(attr, diag::attr_methods_only, attr);
    }
  }

  void visitIBActionAttr(IBActionAttr *attr);
  void visitLazyAttr(LazyAttr *attr);
  void visitIBDesignableAttr(IBDesignableAttr *attr);
  void visitIBInspectableAttr(IBInspectableAttr *attr);
  void visitGKInspectableAttr(GKInspectableAttr *attr);
  void visitIBOutletAttr(IBOutletAttr *attr);
  void visitLLDBDebuggerFunctionAttr(LLDBDebuggerFunctionAttr *attr);
  void visitNSManagedAttr(NSManagedAttr *attr);
  void visitOverrideAttr(OverrideAttr *attr);
  void visitAccessibilityAttr(AccessibilityAttr *attr);
  void visitSetterAccessibilityAttr(SetterAccessibilityAttr *attr);
  bool visitAbstractAccessibilityAttr(AbstractAccessibilityAttr *attr);
  void visitSILStoredAttr(SILStoredAttr *attr);
};
} // end anonymous namespace

void AttributeEarlyChecker::visitTransparentAttr(TransparentAttr *attr) {
  if (auto *ED = dyn_cast<ExtensionDecl>(D)) {
    CanType ExtendedTy = ED->getExtendedType()->getCanonicalType();
    const NominalTypeDecl *ExtendedNominal = ExtendedTy->getAnyNominal();
    // Only Struct and Enum extensions can be transparent.
    if (!isa<StructDecl>(ExtendedNominal) && !isa<EnumDecl>(ExtendedNominal))
      return diagnoseAndRemoveAttr(attr,diag::transparent_on_invalid_extension);
    return;
  }
  
  DeclContext *Ctx = D->getDeclContext();
  // Protocol declarations cannot be transparent.
  if (isa<ProtocolDecl>(Ctx))
    return diagnoseAndRemoveAttr(attr,
                                 diag::transparent_in_protocols_not_supported);
  // Class declarations cannot be transparent.
  if (isa<ClassDecl>(Ctx)) {
    
    // @transparent is always ok on implicitly generated accessors: they can
    // be dispatched (even in classes) when the references are within the
    // class themself.
    if (!(isa<FuncDecl>(D) && cast<FuncDecl>(D)->isAccessor() &&
        D->isImplicit()))
      return diagnoseAndRemoveAttr(attr,
                                   diag::transparent_in_classes_not_supported);
  }
  
  if (auto *VD = dyn_cast<VarDecl>(D)) {
    // Stored properties and variables can't be transparent.
    if (VD->hasStorage())
      return diagnoseAndRemoveAttr(attr, diag::transparent_stored_property);
  }
}

void AttributeEarlyChecker::visitMutationAttr(DeclAttribute *attr) {
  FuncDecl *FD = cast<FuncDecl>(D);

  auto contextTy = FD->getDeclContext()->getDeclaredTypeInContext();
  if (!contextTy)
    return diagnoseAndRemoveAttr(attr, diag::mutating_invalid_global_scope);

  if (contextTy->hasReferenceSemantics())
    return diagnoseAndRemoveAttr(attr, diag::mutating_invalid_classes);
  
  // Verify we don't have both mutating and nonmutating.
  if (FD->getAttrs().hasAttribute<MutatingAttr>())
    if (auto *NMA = FD->getAttrs().getAttribute<NonMutatingAttr>()) {
      diagnoseAndRemoveAttr(NMA, diag::functions_mutating_and_not);
      if (NMA == attr) return;
    }
  
  // Verify that we don't have a static function.
  if (FD->isStatic())
    return diagnoseAndRemoveAttr(attr, diag::static_functions_not_mutating);
}

void AttributeEarlyChecker::visitDynamicAttr(DynamicAttr *attr) {
  // Only instance members of classes can be dynamic.
  auto contextTy = D->getDeclContext()->getDeclaredTypeInContext();
  if (!contextTy || !contextTy->getClassOrBoundGenericClass())
    return diagnoseAndRemoveAttr(attr, diag::dynamic_not_in_class);
    
  // Members cannot be both dynamic and final.
  if (D->getAttrs().hasAttribute<FinalAttr>())
    return diagnoseAndRemoveAttr(attr, diag::dynamic_with_final);
}


void AttributeEarlyChecker::visitIBActionAttr(IBActionAttr *attr) {
  // Only instance methods returning () can be IBActions.
  const FuncDecl *FD = cast<FuncDecl>(D);
  if (!FD->getDeclContext()->getAsClassOrClassExtensionContext() ||
      FD->isStatic() || FD->isAccessor())
    return diagnoseAndRemoveAttr(attr, diag::invalid_ibaction_decl);

}

void AttributeEarlyChecker::visitIBDesignableAttr(IBDesignableAttr *attr) {
  if (auto *ED = dyn_cast<ExtensionDecl>(D)) {
    CanType extendedTy = ED->getExtendedType()->getCanonicalType();
    if (!isa<ClassDecl>(extendedTy->getAnyNominal()))
      return diagnoseAndRemoveAttr(attr, diag::invalid_ibdesignable_extension);
  }
}

void AttributeEarlyChecker::visitIBInspectableAttr(IBInspectableAttr *attr) {
  // Only instance properties can be 'IBInspectable'.
  auto *VD = cast<VarDecl>(D);
  if (!VD->getDeclContext()->getAsClassOrClassExtensionContext() ||
      VD->isStatic())
    return diagnoseAndRemoveAttr(attr, diag::invalid_ibinspectable,
                                 attr->getAttrName());
}

void AttributeEarlyChecker::visitGKInspectableAttr(GKInspectableAttr *attr) {
  // Only instance properties can be 'GKInspectable'.
  auto *VD = cast<VarDecl>(D);
  if (!VD->getDeclContext()->getAsClassOrClassExtensionContext() ||
      VD->isStatic())
    return diagnoseAndRemoveAttr(attr, diag::invalid_ibinspectable,
                                 attr->getAttrName());
}

void AttributeEarlyChecker::visitSILStoredAttr(SILStoredAttr *attr) {
  auto *VD = cast<VarDecl>(D);
  if (VD->getDeclContext()->getAsClassOrClassExtensionContext())
    return;
  auto ctx = VD->getDeclContext()->getDeclaredTypeInContext();
  if (ctx && ctx->getStructOrBoundGenericStruct())
    return;
  return diagnoseAndRemoveAttr(attr, diag::invalid_decl_attribute_simple);
}

static Optional<Diag<bool,Type>>
isAcceptableOutletType(Type type, bool &isArray, TypeChecker &TC) {
  if (type->isObjCExistentialType() || type->isAny())
    return None; // @objc existential types are okay

  auto nominal = type->getAnyNominal();

  if (auto classDecl = dyn_cast_or_null<ClassDecl>(nominal)) {
    if (classDecl->isObjC())
      return None; // @objc class types are okay.
    return diag::iboutlet_nonobjc_class;
  }

  if (nominal == TC.Context.getStringDecl()) {
    // String is okay because it is bridged to NSString.
    // FIXME: BridgesTypes.def is almost sufficient for this.
    return None;
  }

  if (nominal == TC.Context.getArrayDecl()) {
    // Arrays of arrays are not allowed.
    if (isArray)
      return diag::iboutlet_nonobject_type;

    isArray = true;

    // Handle Array<T>. T must be an Objective-C class or protocol.
    auto boundTy = type->castTo<BoundGenericStructType>();
    auto boundArgs = boundTy->getGenericArgs();
    assert(boundArgs.size() == 1 && "invalid Array declaration");
    Type elementTy = boundArgs.front();
    return isAcceptableOutletType(elementTy, isArray, TC);
  }

  if (type->isExistentialType())
    return diag::iboutlet_nonobjc_protocol;
  
  // No other types are permitted.
  return diag::iboutlet_nonobject_type;
}


void AttributeEarlyChecker::visitIBOutletAttr(IBOutletAttr *attr) {
  // Only instance properties can be 'IBOutlet'.
  auto *VD = cast<VarDecl>(D);
  if (!VD->getDeclContext()->getAsClassOrClassExtensionContext() ||
      VD->isStatic())
    return diagnoseAndRemoveAttr(attr, diag::invalid_iboutlet);

  if (!VD->isSettable(nullptr))
    return diagnoseAndRemoveAttr(attr, diag::iboutlet_only_mutable);

  // Verify that the field type is valid as an outlet.
  auto type = VD->getType();

  if (VD->isInvalid())
    return;

  // Look through ownership types, and optionals.
  type = type->getReferenceStorageReferent();
  bool wasOptional = false;
  if (Type underlying = type->getAnyOptionalObjectType()) {
    type = underlying;
    wasOptional = true;
  }

  bool isArray = false;
  if (auto isError = isAcceptableOutletType(type, isArray, TC))
    return diagnoseAndRemoveAttr(attr, isError.getValue(),
                                 /*array=*/isArray, type);

  // If the type wasn't optional, an array, or unowned, complain.
  if (!wasOptional && !isArray) {
    auto symbolLoc = Lexer::getLocForEndOfToken(
                       TC.Context.SourceMgr,
                       VD->getTypeSourceRangeForDiagnostics().End);
    TC.diagnose(attr->getLocation(), diag::iboutlet_non_optional,
                type);
    TC.diagnose(symbolLoc, diag::note_make_optional,
                OptionalType::get(type))
      .fixItInsert(symbolLoc, "?");
    TC.diagnose(symbolLoc, diag::note_make_implicitly_unwrapped_optional,
                ImplicitlyUnwrappedOptionalType::get(type))
      .fixItInsert(symbolLoc, "!");
  }
}

void AttributeEarlyChecker::visitNSManagedAttr(NSManagedAttr *attr) {
  // @NSManaged only applies to instance methods and properties within a class.
  if (cast<ValueDecl>(D)->isStatic() ||
      !D->getDeclContext()->getAsClassOrClassExtensionContext()) {
    return diagnoseAndRemoveAttr(attr,
                                 diag::attr_NSManaged_not_instance_member);
  }

  if (auto *method = dyn_cast<FuncDecl>(D)) {
    // Separate out the checks for methods.
    if (method->hasBody())
      return diagnoseAndRemoveAttr(attr, diag::attr_NSManaged_method_body);

    return;
  }

  // Everything below deals with restrictions on @NSManaged properties.
  auto *VD = cast<VarDecl>(D);

  if (VD->isLet())
    return diagnoseAndRemoveAttr(attr, diag::attr_NSManaged_let_property);

  auto diagnoseNotStored = [&](unsigned kind) {
    TC.diagnose(attr->getLocation(), diag::attr_NSManaged_not_stored, kind);
    return attr->setInvalid();
  };

  // @NSManaged properties must be written as stored.
  switch (VD->getStorageKind()) {
  case AbstractStorageDecl::Stored:
    // @NSManaged properties end up being computed; complain if there is
    // an initializer.
    if (VD->getParentInitializer()) {
      TC.diagnose(attr->getLocation(), diag::attr_NSManaged_initial_value)
        .highlight(VD->getParentInitializer()->getSourceRange());
      auto PBD = VD->getParentPatternBinding();
      PBD->setInit(PBD->getPatternEntryIndexForVarDecl(VD), nullptr);
    }
    // Otherwise, ok.
    break;

  case AbstractStorageDecl::StoredWithTrivialAccessors:
    llvm_unreachable("Already created accessors?");

  case AbstractStorageDecl::ComputedWithMutableAddress:
  case AbstractStorageDecl::Computed:
    return diagnoseNotStored(/*computed*/ 0);
  case AbstractStorageDecl::StoredWithObservers:
  case AbstractStorageDecl::InheritedWithObservers:
    return diagnoseNotStored(/*observing*/ 1);
  case AbstractStorageDecl::Addressed:
  case AbstractStorageDecl::AddressedWithTrivialAccessors:
  case AbstractStorageDecl::AddressedWithObservers:
    return diagnoseNotStored(/*addressed*/ 2);
  }

  // @NSManaged properties cannot be @NSCopying
  if (auto *NSCopy = VD->getAttrs().getAttribute<NSCopyingAttr>())
    return diagnoseAndRemoveAttr(NSCopy, diag::attr_NSManaged_NSCopying);

}

void AttributeEarlyChecker::
visitLLDBDebuggerFunctionAttr(LLDBDebuggerFunctionAttr *attr) {
  // This is only legal when debugger support is on.
  if (!D->getASTContext().LangOpts.DebuggerSupport)
    return diagnoseAndRemoveAttr(attr, diag::attr_for_debugger_support_only);
}

void AttributeEarlyChecker::visitOverrideAttr(OverrideAttr *attr) {
  if (!isa<ClassDecl>(D->getDeclContext()) &&
      !isa<ExtensionDecl>(D->getDeclContext()))
    return diagnoseAndRemoveAttr(attr, diag::override_nonclass_decl);
}

void AttributeEarlyChecker::visitLazyAttr(LazyAttr *attr) {
  // lazy may only be used on properties.
  auto *VD = cast<VarDecl>(D);

  // It cannot currently be used on let's since we don't have a mutability model
  // that supports it.
  if (VD->isLet())
    return diagnoseAndRemoveAttr(attr, diag::lazy_not_on_let);

  // lazy is not allowed on a protocol requirement.
  auto varDC = VD->getDeclContext();
  if (isa<ProtocolDecl>(varDC))
    return diagnoseAndRemoveAttr(attr, diag::lazy_not_in_protocol);

  // It only works with stored properties.
  if (!VD->hasStorage())
    return diagnoseAndRemoveAttr(attr, diag::lazy_not_on_computed);

  // lazy is not allowed on a lazily initialized global variable or on a
  // static property (which is already lazily initialized).
  if (VD->isStatic() ||
      (varDC->isModuleScopeContext() &&
       !varDC->getParentSourceFile()->isScriptMode()))
    return diagnoseAndRemoveAttr(attr, diag::lazy_on_already_lazy_global);

  // lazy must have an initializer, and the pattern binding must be a simple
  // one.
  if (!VD->getParentInitializer())
    return diagnoseAndRemoveAttr(attr, diag::lazy_requires_initializer);

  if (!VD->getParentPatternBinding()->getSingleVar())
    return diagnoseAndRemoveAttr(attr, diag::lazy_requires_single_var);

  // TODO: we can't currently support lazy properties on non-type-contexts.
  if (!VD->getDeclContext()->isTypeContext())
    return diagnoseAndRemoveAttr(attr, diag::lazy_must_be_property);

  // TODO: Lazy properties can't yet be observed.
  switch (VD->getStorageKind()) {
  case AbstractStorageDecl::Stored:
  case AbstractStorageDecl::StoredWithTrivialAccessors:
    break;

  case AbstractStorageDecl::StoredWithObservers:
    return diagnoseAndRemoveAttr(attr, diag::lazy_not_observable);

  case AbstractStorageDecl::InheritedWithObservers:
  case AbstractStorageDecl::ComputedWithMutableAddress:
  case AbstractStorageDecl::Computed:
  case AbstractStorageDecl::Addressed:
  case AbstractStorageDecl::AddressedWithTrivialAccessors:
  case AbstractStorageDecl::AddressedWithObservers:
    llvm_unreachable("non-stored variable not filtered out?");
  }
}

bool AttributeEarlyChecker::visitAbstractAccessibilityAttr(
    AbstractAccessibilityAttr *attr) {
  // Accessibility attr may only be used on value decls and extensions.
  if (!isa<ValueDecl>(D) && !isa<ExtensionDecl>(D)) {
    diagnoseAndRemoveAttr(attr, diag::invalid_decl_modifier, attr);
    return true;
  }

  if (auto extension = dyn_cast<ExtensionDecl>(D)) {
    if (!extension->getInherited().empty()) {
      diagnoseAndRemoveAttr(attr, diag::extension_access_with_conformances,
                            attr);
      return true;
    }
  }

  // And not on certain value decls.
  if (isa<DestructorDecl>(D) || isa<EnumElementDecl>(D)) {
    diagnoseAndRemoveAttr(attr, diag::invalid_decl_modifier, attr);
    return true;
  }

  // Or within protocols.
  if (isa<ProtocolDecl>(D->getDeclContext())) {
    diagnoseAndRemoveAttr(attr, diag::access_control_in_protocol, attr);
    return true;
  }

  return false;
}

void AttributeEarlyChecker::visitAccessibilityAttr(AccessibilityAttr *attr) {
  visitAbstractAccessibilityAttr(attr);
}

void AttributeEarlyChecker::visitSetterAccessibilityAttr(
    SetterAccessibilityAttr *attr) {
  auto storage = dyn_cast<AbstractStorageDecl>(D);
  if (!storage)
    return diagnoseAndRemoveAttr(attr, diag::access_control_setter,
                                 attr->getAccess());

  if (visitAbstractAccessibilityAttr(attr))
    return;

  if (!storage->isSettable(storage->getDeclContext())) {
    // This must stay in sync with diag::access_control_setter_read_only.
    enum {
      SK_Constant = 0,
      SK_Variable,
      SK_Property,
      SK_Subscript
    } storageKind;
    if (isa<SubscriptDecl>(storage))
      storageKind = SK_Subscript;
    else if (storage->getDeclContext()->isTypeContext())
      storageKind = SK_Property;
    else if (cast<VarDecl>(storage)->isLet())
      storageKind = SK_Constant;
    else
      storageKind = SK_Variable;
    return diagnoseAndRemoveAttr(attr, diag::access_control_setter_read_only,
                                 attr->getAccess(), storageKind);
  }
}


void TypeChecker::checkDeclAttributesEarly(Decl *D) {
  // Don't perform early attribute validation more than once.
  // FIXME: Crummy way to get idempotency.
  if (D->didEarlyAttrValidation())
    return;

  D->setEarlyAttrValidation();

  AttributeEarlyChecker Checker(*this, D);
  for (auto attr : D->getAttrs()) {
    if (!attr->isValid()) continue;

    // If Attr.def says that the attribute cannot appear on this kind of
    // declaration, diagnose it and disable it.
    if (attr->canAppearOnDecl(D)) {
      // Otherwise, check it.
      Checker.visit(attr);
      continue;
    }

    // Otherwise, this attribute cannot be applied to this declaration.  If the
    // attribute is only valid on one kind of declaration (which is pretty
    // common) give a specific helpful error.
    unsigned PossibleDeclKinds = attr->getOptions() & DeclAttribute::OnAnyDecl;
    StringRef OnlyKind;
    switch (PossibleDeclKinds) {
    case DeclAttribute::OnImport:
      OnlyKind = "import";
      break;
    case DeclAttribute::OnVar:
      OnlyKind = "var";
      break;
    case DeclAttribute::OnFunc:
      OnlyKind = "func";
      break;
    case DeclAttribute::OnClass:
      OnlyKind = "class";
      break;
    case DeclAttribute::OnStruct:
      OnlyKind = "struct";
      break;
    case DeclAttribute::OnConstructor:
      OnlyKind = "init";
      break;
    case DeclAttribute::OnProtocol:
      OnlyKind = "protocol";
      break;
    case DeclAttribute::OnParam:
      OnlyKind = "parameter";
      break;
    default:
      break;
    }

    if (!OnlyKind.empty())
      Checker.diagnoseAndRemoveAttr(attr, diag::attr_only_only_one_decl_kind,
                                    attr, OnlyKind);
    else if (attr->isDeclModifier())
      Checker.diagnoseAndRemoveAttr(attr, diag::invalid_decl_modifier, attr);
    else
      Checker.diagnoseAndRemoveAttr(attr, diag::invalid_decl_attribute, attr);
  }
}

namespace {
class AttributeChecker : public AttributeVisitor<AttributeChecker> {
  TypeChecker &TC;
  Decl *D;

public:
  AttributeChecker(TypeChecker &TC, Decl *D) : TC(TC), D(D) {}

  /// Deleting this ensures that all attributes are covered by the visitor
  /// below.
  void visitDeclAttribute(DeclAttribute *A) = delete;

#define IGNORED_ATTR(CLASS)                                              \
    void visit##CLASS##Attr(CLASS##Attr *) {}

    IGNORED_ATTR(AutoClosure)
    IGNORED_ATTR(Alignment)
    IGNORED_ATTR(SILGenName)
    IGNORED_ATTR(Dynamic)
    IGNORED_ATTR(Exported)
    IGNORED_ATTR(Convenience)
    IGNORED_ATTR(GKInspectable)
    IGNORED_ATTR(IBDesignable)
    IGNORED_ATTR(IBInspectable)
    IGNORED_ATTR(IBOutlet) // checked early.
    IGNORED_ATTR(Indirect)
    IGNORED_ATTR(Inline)
    IGNORED_ATTR(Effects)
    IGNORED_ATTR(FixedLayout)
    IGNORED_ATTR(Lazy)      // checked early.
    IGNORED_ATTR(LLDBDebuggerFunction)
    IGNORED_ATTR(Mutating)
    IGNORED_ATTR(NoEscape)
    IGNORED_ATTR(NonMutating)
    IGNORED_ATTR(NonObjC)
    IGNORED_ATTR(NoReturn)
    IGNORED_ATTR(NSManaged) // checked early.
    IGNORED_ATTR(ObjC)
    IGNORED_ATTR(ObjCBridged)
    IGNORED_ATTR(ObjCNonLazyRealization)
    IGNORED_ATTR(Optional)
    IGNORED_ATTR(Ownership)
    IGNORED_ATTR(Override)
    IGNORED_ATTR(RawDocComment)
    IGNORED_ATTR(Semantics)
    IGNORED_ATTR(Transparent)
    IGNORED_ATTR(SynthesizedProtocol)
    IGNORED_ATTR(RequiresStoredPropertyInits)
    IGNORED_ATTR(SILStored)
    IGNORED_ATTR(Swift3Migration)
    IGNORED_ATTR(Testable)
    IGNORED_ATTR(WarnUnqualifiedAccess)
    IGNORED_ATTR(ShowInInterface)
    IGNORED_ATTR(DiscardableResult)
    IGNORED_ATTR(Escaping)

    // FIXME: We actually do have things to enforce for versioned API.
    IGNORED_ATTR(Versioned)
#undef IGNORED_ATTR

  void visitAvailableAttr(AvailableAttr *attr);
  
  void visitCDeclAttr(CDeclAttr *attr);

  void visitFinalAttr(FinalAttr *attr);
  void visitIBActionAttr(IBActionAttr *attr);
  void visitNSCopyingAttr(NSCopyingAttr *attr);
  void visitRequiredAttr(RequiredAttr *attr);
  void visitRethrowsAttr(RethrowsAttr *attr);

  void visitAccessibilityAttr(AccessibilityAttr *attr);
  void visitSetterAccessibilityAttr(SetterAccessibilityAttr *attr);

  void checkApplicationMainAttribute(DeclAttribute *attr,
                                     Identifier Id_ApplicationDelegate,
                                     Identifier Id_Kit,
                                     Identifier Id_ApplicationMain);
  
  void visitNSApplicationMainAttr(NSApplicationMainAttr *attr);
  void visitUIApplicationMainAttr(UIApplicationMainAttr *attr);

  void visitUnsafeNoObjCTaggedPointerAttr(UnsafeNoObjCTaggedPointerAttr *attr);
  void visitSwiftNativeObjCRuntimeBaseAttr(
                                         SwiftNativeObjCRuntimeBaseAttr *attr);

  void checkOperatorAttribute(DeclAttribute *attr);

  void visitInfixAttr(InfixAttr *attr) { checkOperatorAttribute(attr); }
  void visitPostfixAttr(PostfixAttr *attr) { checkOperatorAttribute(attr); }
  void visitPrefixAttr(PrefixAttr *attr) { checkOperatorAttribute(attr); }

  void visitSpecializeAttr(SpecializeAttr *attr);
};
} // end anonymous namespace


static bool checkObjectOrOptionalObjectType(TypeChecker &TC, Decl *D,
                                            ParamDecl *param) {
  Type ty = param->getType();
  if (auto unwrapped = ty->getAnyOptionalObjectType())
    ty = unwrapped;

  if (auto classDecl = ty->getClassOrBoundGenericClass()) {
    // @objc class types are okay.
    if (!classDecl->isObjC()) {
      TC.diagnose(D, diag::ibaction_nonobjc_class_argument,
                  param->getType())
        .highlight(param->getSourceRange());
      return true;
    }
  } else if (ty->isObjCExistentialType() || ty->isAny()) {
    // @objc existential types are okay, as is Any.
    // Nothing to do.
  } else {
    // No other types are permitted.
    TC.diagnose(D, diag::ibaction_nonobject_argument,
                param->getType())
      .highlight(param->getSourceRange());
    return true;
  }

  return false;
}

static bool isiOS(TypeChecker &TC) {
  return TC.getLangOpts().Target.isiOS();
}

static bool iswatchOS(TypeChecker &TC) {
  return TC.getLangOpts().Target.isWatchOS();
}

static bool isRelaxedIBAction(TypeChecker &TC) {
  return isiOS(TC) || iswatchOS(TC);
}

void AttributeChecker::visitIBActionAttr(IBActionAttr *attr) {
  // IBActions instance methods must have type Class -> (...) -> ().
  auto *FD = cast<FuncDecl>(D);
  Type CurriedTy = FD->getType()->castTo<AnyFunctionType>()->getResult();
  Type ResultTy = CurriedTy->castTo<AnyFunctionType>()->getResult();
  if (!ResultTy->isEqual(TupleType::getEmpty(TC.Context))) {
    TC.diagnose(D, diag::invalid_ibaction_result, ResultTy);
    attr->setInvalid();
    return;
  }

  auto paramList = FD->getParameterList(1);
  bool relaxedIBActionUsedOnOSX = false;
  bool Valid = true;
  switch (paramList->size()) {
  case 0:
    // (iOS only) No arguments.
    if (!isRelaxedIBAction(TC)) {
      relaxedIBActionUsedOnOSX = true;
      break;
    }
    break;
  case 1:
    // One argument. May be a scalar on iOS/watchOS (because of WatchKit).
    if (isRelaxedIBAction(TC)) {
      // Do a rough check to allow any ObjC-representable struct or enum type
      // on iOS.
      Type ty = paramList->get(0)->getType();
      if (auto nominal = ty->getAnyNominal())
        if (isa<StructDecl>(nominal) || isa<EnumDecl>(nominal))
          if (nominal->classifyAsOptionalType() == OTK_None)
            if (ty->isTriviallyRepresentableIn(ForeignLanguage::ObjectiveC,
                                               cast<FuncDecl>(D)))
              break;  // Looks ok.
    }
    if (checkObjectOrOptionalObjectType(TC, D, paramList->get(0)))
      Valid = false;
    break;
  case 2:
    // (iOS/watchOS only) Two arguments, the second of which is a UIEvent.
    // We don't currently enforce the UIEvent part.
    if (!isRelaxedIBAction(TC)) {
      relaxedIBActionUsedOnOSX = true;
      break;
    }
    if (checkObjectOrOptionalObjectType(TC, D, paramList->get(0)))
      Valid = false;
    if (checkObjectOrOptionalObjectType(TC, D, paramList->get(1)))
      Valid = false;
    break;
  default:
    // No platform allows an action signature with more than two arguments.
    TC.diagnose(D, diag::invalid_ibaction_argument_count,
                isRelaxedIBAction(TC));
    Valid = false;
    break;
  }

  if (relaxedIBActionUsedOnOSX) {
    TC.diagnose(D, diag::invalid_ibaction_argument_count,
                /*relaxedIBAction=*/false);
    Valid = false;
  }

  if (!Valid)
    attr->setInvalid();
}

/// Get the innermost enclosing declaration for a declaration.
static Decl *getEnclosingDeclForDecl(Decl *D) {
  // If the declaration is an accessor, treat its storage declaration
  // as the enclosing declaration.
  if (auto *FD = dyn_cast<FuncDecl>(D)) {
    if (FD->isAccessor()) {
      return FD->getAccessorStorageDecl();
    }
  }

  return D->getDeclContext()->getInnermostDeclarationDeclContext();
}

void AttributeChecker::visitAvailableAttr(AvailableAttr *attr) {
  if (TC.getLangOpts().DisableAvailabilityChecking)
    return;

  if (!attr->isActivePlatform(TC.Context) || !attr->Introduced.hasValue())
    return;

  SourceLoc attrLoc = attr->getLocation();

  Optional<Diag<>> MaybeNotAllowed =
      TC.diagnosticIfDeclCannotBePotentiallyUnavailable(D);
  if (MaybeNotAllowed.hasValue()) {
    TC.diagnose(attrLoc, MaybeNotAllowed.getValue());
  }

  // Find the innermost enclosing declaration with an availability
  // range annotation and ensure that this attribute's available version range
  // is fully contained within that declaration's range. If there is no such
  // enclosing declaration, then there is nothing to check.
  Optional<AvailabilityContext> EnclosingAnnotatedRange;
  Decl *EnclosingDecl = getEnclosingDeclForDecl(D);

  while (EnclosingDecl) {
    EnclosingAnnotatedRange =
        AvailabilityInference::annotatedAvailableRange(EnclosingDecl,
                                                       TC.Context);

    if (EnclosingAnnotatedRange.hasValue())
      break;

    EnclosingDecl = getEnclosingDeclForDecl(EnclosingDecl);
  }

  if (!EnclosingDecl)
    return;

  AvailabilityContext AttrRange{
      VersionRange::allGTE(attr->Introduced.getValue())};

  if (!AttrRange.isContainedIn(EnclosingAnnotatedRange.getValue())) {
    TC.diagnose(attr->getLocation(),
                diag::availability_decl_more_than_enclosing);
    TC.diagnose(EnclosingDecl->getLoc(),
                diag::availability_decl_more_than_enclosing_enclosing_here);
  }
}

void AttributeChecker::visitCDeclAttr(CDeclAttr *attr) {
  // Only top-level func decls are currently supported.
  if (D->getDeclContext()->isTypeContext())
    TC.diagnose(attr->getLocation(),
                diag::cdecl_not_at_top_level);
  
  // The name must not be empty.
  if (attr->Name.empty())
    TC.diagnose(attr->getLocation(),
                diag::cdecl_empty_name);
}

void AttributeChecker::visitUnsafeNoObjCTaggedPointerAttr(
                                          UnsafeNoObjCTaggedPointerAttr *attr) {
  // Only class protocols can have the attribute.
  auto proto = dyn_cast<ProtocolDecl>(D);
  if (!proto) {
    TC.diagnose(attr->getLocation(),
                diag::no_objc_tagged_pointer_not_class_protocol);
    attr->setInvalid();
  }
  
  if (!proto->requiresClass()
      && !proto->getAttrs().hasAttribute<ObjCAttr>()) {
    TC.diagnose(attr->getLocation(),
                diag::no_objc_tagged_pointer_not_class_protocol);
    attr->setInvalid();    
  }
}

void AttributeChecker::visitSwiftNativeObjCRuntimeBaseAttr(
                                         SwiftNativeObjCRuntimeBaseAttr *attr) {
  // Only root classes can have the attribute.
  auto theClass = dyn_cast<ClassDecl>(D);
  if (!theClass) {
    TC.diagnose(attr->getLocation(),
                diag::swift_native_objc_runtime_base_not_on_root_class);
    attr->setInvalid();
    return;
  }
  
  if (theClass->hasSuperclass()) {
    TC.diagnose(attr->getLocation(),
                diag::swift_native_objc_runtime_base_not_on_root_class);
    attr->setInvalid();
    return;
  }
}

void AttributeChecker::visitFinalAttr(FinalAttr *attr) {
  // final on classes marks all members with final.
  if (isa<ClassDecl>(D))
    return;

  // 'final' only makes sense in the context of a class
  // declaration or a protocol extension.  Reject it on global functions,
  // structs, enums, etc.
  if (!D->getDeclContext()->getAsClassOrClassExtensionContext() &&
      !D->getDeclContext()->getAsProtocolExtensionContext()) {
    TC.diagnose(attr->getLocation(), diag::member_cannot_be_final);
    return;
  }

  // We currently only support final on var/let, func and subscript
  // declarations.
  if (!isa<VarDecl>(D) && !isa<FuncDecl>(D) && !isa<SubscriptDecl>(D)) {
    TC.diagnose(attr->getLocation(), diag::final_not_allowed_here);
    return;
  }

  if (auto *FD = dyn_cast<FuncDecl>(D)) {
    if (FD->isAccessor() && !attr->isImplicit()) {
      unsigned Kind = 2;
      if (auto *VD = dyn_cast<VarDecl>(FD->getAccessorStorageDecl()))
        Kind = VD->isLet() ? 1 : 0;
      TC.diagnose(attr->getLocation(), diag::final_not_on_accessors, Kind);
      return;
    }
  }
}

/// Return true if this is a builtin operator that cannot be defined in user
/// code.
static bool isBuiltinOperator(StringRef name, DeclAttribute *attr) {
  return ((isa<PrefixAttr>(attr)  && name == "&") ||   // lvalue to inout
          (isa<PostfixAttr>(attr) && name == "!") ||   // optional unwrapping
          (isa<PostfixAttr>(attr) && name == "?") ||   // optional chaining
          (isa<InfixAttr>(attr) && name == "?") ||     // ternary operator
          (isa<PostfixAttr>(attr) && name == ">") ||   // generic argument list
          (isa<PrefixAttr>(attr)  && name == "<"));    // generic argument list
}

void AttributeChecker::checkOperatorAttribute(DeclAttribute *attr) {
  // Check out the operator attributes.  They may be attached to an operator
  // declaration or a function.
  if (auto *OD = dyn_cast<OperatorDecl>(D)) {
    // Reject attempts to define builtin operators.
    if (isBuiltinOperator(OD->getName().str(), attr)) {
      TC.diagnose(D->getStartLoc(), diag::redefining_builtin_operator,
                  attr->getAttrName(), OD->getName().str());
      attr->setInvalid();
      return;
    }

    // Otherwise, the attribute is always ok on an operator.
    return;
  }

  // Operators implementations may only be defined as functions.
  auto *FD = dyn_cast<FuncDecl>(D);
  if (!FD) {
    TC.diagnose(D->getLoc(), diag::operator_not_func);
    attr->setInvalid();
    return;
  }

  // Only functions with an operator identifier can be declared with as an
  // operator.
  if (!FD->getName().isOperator()) {
    TC.diagnose(D->getStartLoc(), diag::attribute_requires_operator_identifier,
                attr->getAttrName());
    attr->setInvalid();
    return;
  }

  // Reject attempts to define builtin operators.
  if (isBuiltinOperator(FD->getName().str(), attr)) {
    TC.diagnose(D->getStartLoc(), diag::redefining_builtin_operator,
                attr->getAttrName(), FD->getName().str());
    attr->setInvalid();
    return;
  }

  // Infix operator is only allowed on operator declarations, not on func.
  if (isa<InfixAttr>(attr)) {
    TC.diagnose(attr->getLocation(), diag::invalid_infix_on_func)
      .fixItRemove(attr->getLocation());
    attr->setInvalid();
    return;
  }

  // Otherwise, must be unary.
  if (!FD->isUnaryOperator()) {
    TC.diagnose(attr->getLocation(), diag::attribute_requires_single_argument,
                attr->getAttrName());
    attr->setInvalid();
    return;
  }
}

void AttributeChecker::visitNSCopyingAttr(NSCopyingAttr *attr) {
  // The @NSCopying attribute is only allowed on stored properties.
  auto *VD = cast<VarDecl>(D);

  // It may only be used on class members.
  auto typeContext = D->getDeclContext()->getDeclaredTypeInContext();
  auto contextTypeDecl =
  typeContext ? typeContext->getNominalOrBoundGenericNominal() : nullptr;
  if (!contextTypeDecl || !isa<ClassDecl>(contextTypeDecl)) {
    TC.diagnose(attr->getLocation(), diag::nscopying_only_on_class_properties);
    attr->setInvalid();
    return;
  }

  if (!VD->isSettable(VD->getDeclContext())) {
    TC.diagnose(attr->getLocation(), diag::nscopying_only_mutable);
    attr->setInvalid();
    return;
  }

  if (!VD->hasStorage()) {
    TC.diagnose(attr->getLocation(), diag::nscopying_only_stored_property);
    attr->setInvalid();
    return;
  }

  assert(VD->getOverriddenDecl() == nullptr &&
         "Can't have value with storage that is an override");

  // Check the type.  It must be must be [unchecked]optional, weak, a normal
  // class, AnyObject, or classbound protocol.
  // must conform to the NSCopying protocol.
  
}

void AttributeChecker::checkApplicationMainAttribute(DeclAttribute *attr,
                                             Identifier Id_ApplicationDelegate,
                                             Identifier Id_Kit,
                                             Identifier Id_ApplicationMain) {
  // %select indexes for ApplicationMain diagnostics.
  enum : unsigned {
    UIApplicationMainClass,
    NSApplicationMainClass,
  };
  
  unsigned applicationMainKind;
  if (isa<UIApplicationMainAttr>(attr))
    applicationMainKind = UIApplicationMainClass;
  else if (isa<NSApplicationMainAttr>(attr))
    applicationMainKind = NSApplicationMainClass;
  else
    llvm_unreachable("not an ApplicationMain attr");
  
  auto *CD = dyn_cast<ClassDecl>(D);
  
  // The applicant not being a class should have been diagnosed by the early
  // checker.
  if (!CD) return;

  // The class cannot be generic.
  if (CD->isGenericContext()) {
    TC.diagnose(attr->getLocation(),
                diag::attr_generic_ApplicationMain_not_supported,
                applicationMainKind);
    attr->setInvalid();
    return;
  }
  
  // @XXApplicationMain classes must conform to the XXApplicationDelegate
  // protocol.
  auto &C = D->getASTContext();

  auto KitModule = C.getLoadedModule(Id_Kit);
  ProtocolDecl *ApplicationDelegateProto = nullptr;
  if (KitModule) {
    auto lookupOptions = defaultUnqualifiedLookupOptions;
    lookupOptions |= NameLookupFlags::OnlyTypes;
    lookupOptions |= NameLookupFlags::KnownPrivate;

    auto lookup = TC.lookupUnqualified(KitModule, Id_ApplicationDelegate,
                                       SourceLoc(),
                                       lookupOptions);
    ApplicationDelegateProto = dyn_cast_or_null<ProtocolDecl>(
                                   lookup.getSingleTypeResult());
  }

  if (!ApplicationDelegateProto ||
      !TC.conformsToProtocol(CD->getDeclaredType(), ApplicationDelegateProto,
                             CD, None)) {
    TC.diagnose(attr->getLocation(),
                diag::attr_ApplicationMain_not_ApplicationDelegate,
                applicationMainKind);
    attr->setInvalid();
  }

  if (attr->isInvalid())
    return;
  
  // Register the class as the main class in the module. If there are multiples
  // they will be diagnosed.
  auto *SF = cast<SourceFile>(CD->getModuleScopeContext());
  if (SF->registerMainClass(CD, attr->getLocation()))
    attr->setInvalid();
  
  // Check that we have the needed symbols in the frameworks.
  auto lookupOptions = defaultUnqualifiedLookupOptions;
  lookupOptions |= NameLookupFlags::KnownPrivate;
  auto lookupMain = TC.lookupUnqualified(KitModule, Id_ApplicationMain,
                                         SourceLoc(), lookupOptions);

  for (const auto &result : lookupMain) {
    TC.validateDecl(result.Decl);
  }
  auto Foundation = TC.Context.getLoadedModule(C.Id_Foundation);
  if (Foundation) {
    auto lookupString = TC.lookupUnqualified(
                          Foundation,
                          C.getIdentifier("NSStringFromClass"),
                          SourceLoc(),
                          lookupOptions);
    for (const auto &result : lookupString) {
      TC.validateDecl(result.Decl);
    }
  }
}

void AttributeChecker::visitNSApplicationMainAttr(NSApplicationMainAttr *attr) {
  auto &C = D->getASTContext();
  checkApplicationMainAttribute(attr,
                                C.getIdentifier("NSApplicationDelegate"),
                                C.getIdentifier("AppKit"),
                                C.getIdentifier("NSApplicationMain"));
}
void AttributeChecker::visitUIApplicationMainAttr(UIApplicationMainAttr *attr) {
  auto &C = D->getASTContext();
  checkApplicationMainAttribute(attr,
                                C.getIdentifier("UIApplicationDelegate"),
                                C.getIdentifier("UIKit"),
                                C.getIdentifier("UIApplicationMain"));
}

/// Determine whether the given context is an extension to an Objective-C class
/// where the class is defined in the Objective-C module and the extension is
/// defined within its module.
static bool isObjCClassExtensionInOverlay(DeclContext *dc) {
  // Check whether we have an extension.
  auto ext = dyn_cast<ExtensionDecl>(dc);
  if (!ext)
    return false;

  // Find the extended class.
  auto classDecl = ext->getExtendedType()->getClassOrBoundGenericClass();
  if (!classDecl)
    return false;

  // The class must be defined in Objective-C.
  if (!classDecl->hasClangNode())
    return false;

  // Find the Clang module unit that stores the class.
  auto classModuleUnit
    = dyn_cast<ClangModuleUnit>(classDecl->getModuleScopeContext());
  if (!classModuleUnit)
    return false;

  // Check whether the extension is in the overlay.
  auto extModule = ext->getDeclContext()->getParentModule();
  return extModule == classModuleUnit->getAdapterModule();
}

void AttributeChecker::visitRequiredAttr(RequiredAttr *attr) {
  // The required attribute only applies to constructors.
  auto ctor = cast<ConstructorDecl>(D);
  auto parentTy = ctor->getExtensionType();
  if (!parentTy) {
    // Constructor outside of nominal type context; we've already complained
    // elsewhere.
    attr->setInvalid();
    return;
  }
  // Only classes can have required constructors.
  if (parentTy->getClassOrBoundGenericClass()) {
    // The constructor must be declared within the class itself.
    // FIXME: Allow an SDK overlay to add a required initializer to a class
    // defined in Objective-C
    if (!isa<ClassDecl>(ctor->getDeclContext()) &&
        !isObjCClassExtensionInOverlay(ctor->getDeclContext())) {
      TC.diagnose(ctor, diag::required_initializer_in_extension, parentTy)
        .highlight(attr->getLocation());
      attr->setInvalid();
      return;
    }
  } else {
    if (!parentTy->is<ErrorType>()) {
      TC.diagnose(ctor, diag::required_initializer_nonclass, parentTy)
        .highlight(attr->getLocation());
    }
    attr->setInvalid();
    return;
  }
}

static bool hasThrowingFunctionParameter(CanType type) {
  // Only consider throwing function types.
  if (auto fnType = dyn_cast<AnyFunctionType>(type)) {
    return fnType->getExtInfo().throws();
  }

  // Look through tuples.
  if (auto tuple = dyn_cast<TupleType>(type)) {
    for (auto eltType : tuple.getElementTypes()) {
      if (hasThrowingFunctionParameter(eltType))
        return true;
    }
    return false;
  }

  // Suppress diagnostics in the presence of errors.
  if (isa<ErrorType>(type)) {
    return true;
  }

  return false;
}

void AttributeChecker::visitRethrowsAttr(RethrowsAttr *attr) {
  // 'rethrows' only applies to functions that take throwing functions
  // as parameters.
  auto fn = cast<AbstractFunctionDecl>(D);
  for (auto paramList : fn->getParameterLists()) {
    for (auto param : *paramList)
      if (hasThrowingFunctionParameter(param->getType()->lookThroughAllAnyOptionalTypes()->getCanonicalType()))
        return;
  }

  TC.diagnose(attr->getLocation(), diag::rethrows_without_throwing_parameter);
  attr->setInvalid();
}

void AttributeChecker::visitAccessibilityAttr(AccessibilityAttr *attr) {
  if (auto extension = dyn_cast<ExtensionDecl>(D)) {
    if (attr->getAccess() == Accessibility::Open) {
      TC.diagnose(attr->getLocation(), diag::access_control_extension_open)
        .fixItReplace(attr->getRange(), "public");
      attr->setInvalid();
      return;
    }

    Type extendedTy = extension->getExtendedType();
    Accessibility typeAccess = extendedTy->getAnyNominal()->getFormalAccess();
    if (attr->getAccess() > typeAccess) {
      TC.diagnose(attr->getLocation(), diag::access_control_extension_more,
                  typeAccess,
                  extendedTy->getAnyNominal()->getDescriptiveKind(),
                  attr->getAccess())
        .fixItRemove(attr->getRange());
      attr->setInvalid();
      return;
    }

  } else if (auto extension = dyn_cast<ExtensionDecl>(D->getDeclContext())) {
    TC.computeDefaultAccessibility(extension);
    Accessibility maxAccess = extension->getMaxAccessibility();
    if (std::min(attr->getAccess(), Accessibility::Public) > maxAccess) {
      if (maxAccess == Accessibility::FilePrivate &&
          !TC.Context.LangOpts.EnableSwift3Private) {
        maxAccess = Accessibility::Private;
      }

      // FIXME: It would be nice to say what part of the requirements actually
      // end up being problematic.
      auto diag =
          TC.diagnose(attr->getLocation(),
                      diag::access_control_ext_requirement_member_more,
                      attr->getAccess(),
                      D->getDescriptiveKind(),
                      maxAccess);
      swift::fixItAccessibility(diag, cast<ValueDecl>(D), maxAccess);
      return;
    }

    auto extAttr = extension->getAttrs().getAttribute<AccessibilityAttr>();
    if (extAttr && attr->getAccess() > extAttr->getAccess()) {
      auto diag = TC.diagnose(attr->getLocation(),
                              diag::access_control_ext_member_more,
                              attr->getAccess(),
                              D->getDescriptiveKind(),
                              extAttr->getAccess());
      swift::fixItAccessibility(diag, cast<ValueDecl>(D), extAttr->getAccess());
      return;
    }
  }

  if (attr->getAccess() == Accessibility::Open) {
    if (!isa<ClassDecl>(D) && !D->isPotentiallyOverridable() &&
        !attr->isInvalid()) {
      TC.diagnose(attr->getLocation(), diag::access_control_open_bad_decl)
        .fixItReplace(attr->getRange(), "public");
      attr->setInvalid();
    }
  }
}

void
AttributeChecker::visitSetterAccessibilityAttr(SetterAccessibilityAttr *attr) {
  auto getterAccess = cast<ValueDecl>(D)->getFormalAccess();
  if (attr->getAccess() > getterAccess) {
    // This must stay in sync with diag::access_control_setter_more.
    enum {
      SK_Variable = 0,
      SK_Property,
      SK_Subscript
    } storageKind;
    if (isa<SubscriptDecl>(D))
      storageKind = SK_Subscript;
    else if (D->getDeclContext()->isTypeContext())
      storageKind = SK_Property;
    else
      storageKind = SK_Variable;
    TC.diagnose(attr->getLocation(), diag::access_control_setter_more,
                getterAccess, storageKind, attr->getAccess());
    attr->setInvalid();
    return;
  }
}

/// Check that the @_specialize type list has the correct number of entries.
/// Resolve each type in the list to a concrete type.
/// Create a Substitution list mapping each nested archetype to a concrete
/// type, and resolve conformances for each generic parameter requirement.
/// Store the Substitution list in a ConcreteDeclRef attached to the attribute.
void AttributeChecker::visitSpecializeAttr(SpecializeAttr *attr) {
  DeclContext *DC = D->getDeclContext();
  auto *FD = cast<AbstractFunctionDecl>(D);
  auto *genericSig = FD->getGenericSignature();

  unsigned numTypes = genericSig->getGenericParams().size();
  if (numTypes != attr->getTypeLocs().size()) {
    TC.diagnose(attr->getLocation(), diag::type_parameter_count_mismatch,
                FD->getName(), numTypes, attr->getTypeLocs().size(),
                numTypes > attr->getTypeLocs().size());
    return;
  }
  // Initialize each TypeLoc in this attribute with a concrete type,
  // and populate a substitution map from GenericTypeParamType to concrete Type.
  TypeSubstitutionMap subMap;
  for (unsigned paramIdx = 0; paramIdx < numTypes; ++paramIdx) {

    auto *genericTypeParamTy = genericSig->getGenericParams()[paramIdx];
    auto &tl = attr->getTypeLocs()[paramIdx];

    auto ty = TC.resolveType(tl.getTypeRepr(), DC, None);
    if (ty && !ty->is<ErrorType>()) {
      if (ty->getCanonicalType()->hasArchetype()) {
        TC.diagnose(attr->getLocation(),
                    diag::cannot_partially_specialize_generic_function);
        return;
      }
      tl.setType(ty, /*validated=*/true);
      subMap[genericTypeParamTy->getCanonicalType().getPointer()] = ty;
    }
  }
  // Build a list of Substitutions.
  //
  // This walks the generic signature's requirements, similar to
  // Solution::computeSubstitutions but with several differences:
  // - It does not operate within the type constraint system.
  // - This is the first point at which diagnostics must be emitted for
  //   bad conformances. Self and super requirements must also be
  //   checked and diagnosed.
  // - This does not make use of Archetypes since it is directly substituting
  //   in place of GenericTypeParams.
  SmallVector<Substitution, 4> substitutions;
  auto currentModule = FD->getParentModule();
  Type currentFromTy;
  Type currentReplacement;
  SmallVector<ProtocolConformanceRef, 4> currentConformances;
  for (const auto &req : genericSig->getRequirements()) {

    switch (req.getKind()) {
    case RequirementKind::WitnessMarker:
      // Flush the current conformances.
      if (currentFromTy) {
        substitutions.push_back({
          currentReplacement,
          DC->getASTContext().AllocateCopy(currentConformances)
        });
        currentConformances.clear();
      }
      // Each witness marker starts a new substitution.
      currentFromTy = req.getFirstType();
      currentReplacement = currentFromTy.subst(currentModule, subMap, None);
      break;

    case RequirementKind::Conformance: {
      assert(currentFromTy->getCanonicalType()
             == req.getFirstType()->getCanonicalType() && "bad WitnessMarker");
      // Get the conformance and record it.
      auto protoType = req.getSecondType()->castTo<ProtocolType>();
      ProtocolConformance *conformance = nullptr;
      bool conforms =
        TC.conformsToProtocol(currentReplacement,
                              protoType->getDecl(),
                              DC,
                              (ConformanceCheckFlags::InExpression|
                               ConformanceCheckFlags::Used),
                              &conformance);
      if (!conforms || !conformance) {
        TC.diagnose(attr->getLocation(),
                    diag::cannot_convert_argument_value_protocol,
                    currentReplacement, protoType);
        // leaks prior conformances
        return;
      }
      currentConformances.push_back(
        ProtocolConformanceRef(protoType->getDecl(), conformance));
      break;
    }
    case RequirementKind::Superclass: {
      // Superclass requirements aren't recorded in substitutions.
      auto firstTy = req.getFirstType().subst(currentModule, subMap, None);
      auto superTy = req.getSecondType().subst(currentModule, subMap, None);
      if (!TC.isSubtypeOf(firstTy, superTy, DC)) {
        TC.diagnose(attr->getLocation(), diag::type_does_not_inherit,
                    FD->getType(), firstTy, superTy);
      }
      break;
    }
    case RequirementKind::SameType: {
      // Same-type requirements are type checked but not recorded in
      // substitutions.
      auto firstTy = req.getFirstType().subst(currentModule, subMap, None);
      auto sameTy = req.getSecondType().subst(currentModule, subMap, None);
      if (!firstTy->isEqual(sameTy)) {
        TC.diagnose(attr->getLocation(), diag::types_not_equal, FD->getType(),
                    firstTy, sameTy);

        return;
      }
      break;
    }
    }
  }
  // Flush the final conformances.
  if (currentFromTy) {
    substitutions.push_back({
      currentReplacement,
      DC->getASTContext().AllocateCopy(currentConformances),
    });
    currentConformances.clear();
  }
  // Package the Substitution list in the SpecializeAttr's ConcreteDeclRef.
  attr->setConcreteDecl(
    ConcreteDeclRef(DC->getASTContext(), FD, substitutions));
}

void TypeChecker::checkDeclAttributes(Decl *D) {
  AttributeChecker Checker(*this, D);

  for (auto attr : D->getAttrs()) {
    if (attr->isValid())
      Checker.visit(attr);
  }
}

void TypeChecker::checkTypeModifyingDeclAttributes(VarDecl *var) {
  if (auto *attr = var->getAttrs().getAttribute<OwnershipAttr>())
    checkOwnershipAttr(var, attr);
  if (auto *attr = var->getAttrs().getAttribute<AutoClosureAttr>()) {
    if (auto *pd = dyn_cast<ParamDecl>(var))
      checkAutoClosureAttr(pd, attr);
    else {
      AttributeEarlyChecker Checker(*this, var);
      Checker.diagnoseAndRemoveAttr(attr, diag::attr_only_only_one_decl_kind,
                                    attr, "parameter");
    }
  }
  if (auto *attr = var->getAttrs().getAttribute<NoEscapeAttr>()) {
    if (auto *pd = dyn_cast<ParamDecl>(var))
      checkNoEscapeAttr(pd, attr);
    else {
      AttributeEarlyChecker Checker(*this, var);
      Checker.diagnoseAndRemoveAttr(attr, diag::attr_only_only_one_decl_kind,
                                    attr, "parameter");
    }
  }
}



void TypeChecker::checkAutoClosureAttr(ParamDecl *PD, AutoClosureAttr *attr) {
  // The paramdecl should have function type, and we restrict it to functions
  // taking ().
  auto *FTy = PD->getType()->getAs<FunctionType>();
  if (FTy == 0) {
    diagnose(attr->getLocation(), diag::autoclosure_function_type);
    attr->setInvalid();
    return;
  }

  // Just stop if we've already applied this attribute.
  if (FTy->isAutoClosure())
    return;

  // This decl attribute has been moved to being a type attribute.
  auto text = attr->isEscaping() ? "@autoclosure @escaping " : "@autoclosure ";
  diagnose(attr->getLocation(), diag::attr_decl_attr_now_on_type,
           "@autoclosure")
    .fixItRemove(attr->getRangeWithAt())
    .fixItInsert(PD->getTypeLoc().getSourceRange().Start, text);

  auto *FuncTyInput = FTy->getInput()->getAs<TupleType>();
  if (!FuncTyInput || FuncTyInput->getNumElements() != 0) {
    diagnose(attr->getLocation(), diag::autoclosure_function_input_nonunit);
    attr->setInvalid();
    return;
  }

  // Change the type to include the autoclosure bit.
  PD->overwriteType(FunctionType::get(FuncTyInput, FTy->getResult(),
                                    FTy->getExtInfo().withIsAutoClosure(true)));

  // Autoclosure may imply noescape, so add a noescape attribute if this is a
  // function parameter.
  if (auto *NEAttr = PD->getAttrs().getAttribute<NoEscapeAttr>()) {
    // If the parameter has both @noescape and @autoclosure, reject the
    // explicit @noescape.
    if (!NEAttr->isImplicit())
      diagnose(NEAttr->getLocation(),
               attr->isEscaping()
                 ? diag::noescape_conflicts_escaping_autoclosure
                 : diag::noescape_implied_by_autoclosure)
        .fixItRemove(NEAttr->getRange());
  } else if (!attr->isEscaping()) {
    auto *newAttr = new (Context) NoEscapeAttr(/*isImplicit*/true);
    PD->getAttrs().add(newAttr);
    checkNoEscapeAttr(PD, newAttr);
  }
}

void TypeChecker::checkNoEscapeAttr(ParamDecl *PD, NoEscapeAttr *attr) {
  // The paramdecl should have function type.
  auto *FTy = PD->getType()->getAs<FunctionType>();
  if (FTy == 0) {
    diagnose(attr->getLocation(), diag::noescape_function_type);
    attr->setInvalid();
    return;
  }

  // This range can be implicit e.g. if we're in the middle of diagnosing
  // @autoclosure.
  auto attrRemovalRange = attr->getRangeWithAt();
  if (attrRemovalRange.isValid()) {
    // Take the attribute, the '@', and the trailing space.
    attrRemovalRange.End = attrRemovalRange.End.getAdvancedLoc(1);
  }
  
  // This decl attribute has been moved to being a type attribute.
  if (!attr->isImplicit()) {
    diagnose(attr->getLocation(), diag::attr_decl_attr_now_on_type, "@noescape")
        .fixItRemove(attrRemovalRange)
        .fixItInsert(PD->getTypeLoc().getSourceRange().Start, "@noescape ");
  }

  // Stop if we've already applied this attribute.
  if (FTy->isNoEscape())
    return;

  // Change the type to include the noescape bit.
  PD->overwriteType(FunctionType::get(FTy->getInput(), FTy->getResult(),
                                      FTy->getExtInfo().withNoEscape(true)));
}


void TypeChecker::checkOwnershipAttr(VarDecl *var, OwnershipAttr *attr) {
  Type type = var->getType();

  // Just stop if we've already processed this declaration.
  if (type->is<ReferenceStorageType>())
    return;

  auto ownershipKind = attr->get();
  assert(ownershipKind != Ownership::Strong &&
         "Cannot specify 'strong' in an ownership attribute");

  // A weak variable must have type R? or R! for some ownership-capable type R.
  Type underlyingType = type;
  if (ownershipKind == Ownership::Weak) {
    if (var->isLet()) {
      diagnose(var->getStartLoc(), diag::invalid_weak_let);
      attr->setInvalid();
      return;
    }

    if (Type objType = type->getAnyOptionalObjectType())
      underlyingType = objType;
    else if (type->allowsOwnership()) {
      // Use this special diagnostic if it's actually a reference type but just
      // isn't Optional.
      if (var->getAttrs().hasAttribute<IBOutletAttr>()) {
        // Let @IBOutlet complain about this; it's more specific.
        attr->setInvalid();
        return;
      }

      diagnose(var->getStartLoc(), diag::invalid_weak_ownership_not_optional,
               OptionalType::get(type));
      attr->setInvalid();

      return;
    } else {
      // This is also an error, but the code below will diagnose it.
    }
  } else if (ownershipKind == Ownership::Strong) {
    // We allow strong on optional-qualified reference types.
    if (Type objType = type->getAnyOptionalObjectType())
      underlyingType = objType;
  }

  if (!underlyingType->allowsOwnership()) {
    // If we have an opaque type, suggest the possibility of adding
    // a class bound.
    if (type->isExistentialType() || type->is<ArchetypeType>()) {
      diagnose(var->getStartLoc(), diag::invalid_ownership_protocol_type,
               (unsigned) ownershipKind, underlyingType);
    } else {
      diagnose(var->getStartLoc(), diag::invalid_ownership_type,
               (unsigned) ownershipKind, underlyingType);
    }
    attr->setInvalid();
    return;
  }

  // Change the type to the appropriate reference storage type.
  var->overwriteType(ReferenceStorageType::get(type, ownershipKind, Context));
}

Optional<Diag<>>
TypeChecker::diagnosticIfDeclCannotBePotentiallyUnavailable(const Decl *D) {
  DeclContext *DC = D->getDeclContext();
  // Do not permit potential availability of script-mode global variables;
  // their initializer expression is not lazily evaluated, so this would
  // not be safe.
  if (isa<VarDecl>(D) && DC->isModuleScopeContext() &&
      DC->getParentSourceFile()->isScriptMode()) {
    return diag::availability_global_script_no_potential;
  }

  // For now, we don't allow stored properties to be potentially unavailable.
  // We will want to support these eventually, but we haven't figured out how
  // this will interact with Definite Initialization, deinitializers and
  // resilience yet.
  if (auto *VD = dyn_cast<VarDecl>(D)) {
    // Globals and statics are lazily initialized, so they are safe
    // for potential unavailability. Note that if D is a global in script
    // mode (which are not lazy) then we will already have returned
    // a diagnosis above.
    bool lazilyInitializedStored = VD->isStatic() ||
                                   VD->getAttrs().hasAttribute<LazyAttr>() ||
                                   DC->isModuleScopeContext();

    if (VD->hasStorage() && !lazilyInitializedStored) {
      return diag::availability_stored_property_no_potential;
    }
  }

  return None;
}
