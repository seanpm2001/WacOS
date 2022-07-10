//===--- Expr.cpp - Swift Language Expression ASTs ------------------------===//
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
//  This file implements the Expr class and subclasses.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/Expr.h"
#include "swift/Basic/Unicode.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/Decl.h" // FIXME: Bad dependency
#include "swift/AST/Stmt.h"
#include "swift/AST/AST.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/AvailabilitySpec.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/TypeLoc.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Twine.h"
using namespace swift;

StringRef swift::getFunctionRefKindStr(FunctionRefKind refKind) {
  switch (refKind) {
  case FunctionRefKind::Unapplied:
    return "unapplied";
  case FunctionRefKind::SingleApply:
    return "single";
  case FunctionRefKind::DoubleApply:
    return "double";
  case FunctionRefKind::Compound:
    return "compound";
  }
}

//===----------------------------------------------------------------------===//
// Expr methods.
//===----------------------------------------------------------------------===//

// Only allow allocation of Stmts using the allocator in ASTContext.
void *Expr::operator new(size_t Bytes, ASTContext &C,
                         unsigned Alignment) {
  return C.Allocate(Bytes, Alignment);
}

StringRef Expr::getKindName(ExprKind K) {
  switch (K) {
#define EXPR(Id, Parent) case ExprKind::Id: return #Id;
#include "swift/AST/ExprNodes.def"
  }
  llvm_unreachable("bad ExprKind");
}

template <class T> static SourceLoc getStartLocImpl(const T *E);
template <class T> static SourceLoc getEndLocImpl(const T *E);
template <class T> static SourceLoc getLocImpl(const T *E);

// Helper functions to check statically whether a method has been
// overridden from its implementation in Expr.  The sort of thing you
// need when you're avoiding v-tables.
namespace {
  template <typename ReturnType, typename Class>
  constexpr bool isOverriddenFromExpr(ReturnType (Class::*)() const) {
    return true;
  }
  template <typename ReturnType>
  constexpr bool isOverriddenFromExpr(ReturnType (Expr::*)() const) {
    return false;
  }

  template <bool IsOverridden> struct Dispatch;

  /// Dispatch down to a concrete override.
  template <> struct Dispatch<true> {
    template <class T> static SourceLoc getStartLoc(const T *E) {
      return E->getStartLoc();
    }
    template <class T> static SourceLoc getEndLoc(const T *E) {
      return E->getEndLoc();
    }
    template <class T> static SourceLoc getLoc(const T *E) {
      return E->getLoc();
    }
    template <class T> static SourceRange getSourceRange(const T *E) {
      return E->getSourceRange();
    }
  };

  /// Default implementations for when a method isn't overridden.
  template <> struct Dispatch<false> {
    template <class T> static SourceLoc getStartLoc(const T *E) {
      return E->getSourceRange().Start;
    }
    template <class T> static SourceLoc getEndLoc(const T *E) {
      return E->getSourceRange().End;
    }
    template <class T> static SourceLoc getLoc(const T *E) {
      return getStartLocImpl(E);
    }
    template <class T> static SourceRange getSourceRange(const T *E) {
      return { E->getStartLoc(), E->getEndLoc() };
    }
  };
}

template <class T> static SourceRange getSourceRangeImpl(const T *E) {
  static_assert(isOverriddenFromExpr(&T::getSourceRange) ||
                (isOverriddenFromExpr(&T::getStartLoc) &&
                 isOverriddenFromExpr(&T::getEndLoc)),
                "Expr subclass must implement either getSourceRange() "
                "or getStartLoc()/getEndLoc()");
  return Dispatch<isOverriddenFromExpr(&T::getSourceRange)>::getSourceRange(E);
}

SourceRange Expr::getSourceRange() const {
  switch (getKind()) {
#define EXPR(ID, PARENT)                                           \
  case ExprKind::ID: return getSourceRangeImpl(cast<ID##Expr>(this));
#include "swift/AST/ExprNodes.def"
  }
  
  llvm_unreachable("expression type not handled!");
}

template <class T> static SourceLoc getStartLocImpl(const T *E) {
  return Dispatch<isOverriddenFromExpr(&T::getStartLoc)>::getStartLoc(E);
}
SourceLoc Expr::getStartLoc() const {
  switch (getKind()) {
#define EXPR(ID, PARENT)                                           \
  case ExprKind::ID: return getStartLocImpl(cast<ID##Expr>(this));
#include "swift/AST/ExprNodes.def"
  }

  llvm_unreachable("expression type not handled!");
}

template <class T> static SourceLoc getEndLocImpl(const T *E) {
  return Dispatch<isOverriddenFromExpr(&T::getEndLoc)>::getEndLoc(E);
}
SourceLoc Expr::getEndLoc() const {
  switch (getKind()) {
#define EXPR(ID, PARENT)                                           \
  case ExprKind::ID: return getEndLocImpl(cast<ID##Expr>(this));
#include "swift/AST/ExprNodes.def"
  }

  llvm_unreachable("expression type not handled!");
}

template <class T> static SourceLoc getLocImpl(const T *E) {
  return Dispatch<isOverriddenFromExpr(&T::getLoc)>::getLoc(E);
}
SourceLoc Expr::getLoc() const {
  switch (getKind()) {
#define EXPR(ID, PARENT)                                           \
  case ExprKind::ID: return getLocImpl(cast<ID##Expr>(this));
#include "swift/AST/ExprNodes.def"
  }

  llvm_unreachable("expression type not handled!");
}

Expr *Expr::getSemanticsProvidingExpr() {
  if (IdentityExpr *IE = dyn_cast<IdentityExpr>(this))
    return IE->getSubExpr()->getSemanticsProvidingExpr();

  if (TryExpr *TE = dyn_cast<TryExpr>(this))
    return TE->getSubExpr()->getSemanticsProvidingExpr();

  if (DefaultValueExpr *DE = dyn_cast<DefaultValueExpr>(this))
    return DE->getSubExpr()->getSemanticsProvidingExpr();
  
  return this;
}

Expr *Expr::getValueProvidingExpr() {
  Expr *E = getSemanticsProvidingExpr();

  if (auto TE = dyn_cast<ForceTryExpr>(this))
    return TE->getSubExpr()->getValueProvidingExpr();

  // TODO:
  //   - tuple literal projection, which may become interestingly idiomatic

  return E;
}

DeclRefExpr *Expr::getMemberOperatorRef() {
  auto expr = this;
  if (!expr->isImplicit()) return nullptr;

  auto dotSyntax = dyn_cast<DotSyntaxCallExpr>(expr);
  if (!dotSyntax) return nullptr;

  auto operatorRef = dyn_cast<DeclRefExpr>(dotSyntax->getFn());
  if (!operatorRef) return nullptr;

  auto func = dyn_cast<FuncDecl>(operatorRef->getDecl());
  if (!func) return nullptr;

  if (!func->isOperator()) return nullptr;

  return operatorRef;
}

/// Propagate l-value use information to children.
void Expr::propagateLValueAccessKind(AccessKind accessKind,
                                     bool allowOverwrite) {
  /// A visitor class which walks an entire l-value expression.
  class PropagateAccessKind
       : public ExprVisitor<PropagateAccessKind, void, AccessKind> {
#ifndef NDEBUG
    bool AllowOverwrite;
#endif
  public:
    PropagateAccessKind(bool allowOverwrite)
#ifndef NDEBUG
      : AllowOverwrite(allowOverwrite)
#endif
    {}

    void visit(Expr *E, AccessKind kind) {
      assert((AllowOverwrite || !E->hasLValueAccessKind()) &&
             "l-value access kind has already been set");

      assert(E->getType()->isAssignableType() &&
             "setting access kind on non-l-value");
      E->setLValueAccessKind(kind);

      // Propagate this to sub-expressions.
      ASTVisitor::visit(E, kind);
    }

#define NON_LVALUE_EXPR(KIND)                                           \
    void visit##KIND##Expr(KIND##Expr *, AccessKind accessKind) {       \
      llvm_unreachable("not an l-value");                               \
    }
#define LEAF_LVALUE_EXPR(KIND)                                          \
    void visit##KIND##Expr(KIND##Expr *E, AccessKind accessKind) {}
#define COMPLETE_PHYSICAL_LVALUE_EXPR(KIND, ACCESSOR)                   \
    void visit##KIND##Expr(KIND##Expr *E, AccessKind accessKind) {      \
      visit(E->ACCESSOR, accessKind);                                   \
    }
#define PARTIAL_PHYSICAL_LVALUE_EXPR(KIND, ACCESSOR)                    \
    void visit##KIND##Expr(KIND##Expr *E, AccessKind accessKind) {      \
      visit(E->ACCESSOR, getPartialAccessKind(accessKind));             \
    }

    void visitMemberRefExpr(MemberRefExpr *E, AccessKind accessKind) {
      if (!E->getBase()->getType()->isLValueType()) return;
      visit(E->getBase(), getBaseAccessKind(E->getMember(), accessKind));
    }
    void visitSubscriptExpr(SubscriptExpr *E, AccessKind accessKind) {
      if (!E->getBase()->getType()->isLValueType()) return;
      visit(E->getBase(), getBaseAccessKind(E->getDecl(), accessKind));
    }

    static AccessKind getPartialAccessKind(AccessKind accessKind) {
      return (accessKind == AccessKind::Read
                ? accessKind : AccessKind::ReadWrite);
    }

    static AccessKind getBaseAccessKind(ConcreteDeclRef member,
                                        AccessKind accessKind) {
      // We assume writes are partial writes, so the result is always
      // either Read or ReadWrite.
      auto memberDecl = cast<AbstractStorageDecl>(member.getDecl());

      // If we're reading and the getter is mutating, or we're writing
      // and the setter is mutating, this is readwrite.
      if ((accessKind != AccessKind::Write &&
           memberDecl->isGetterMutating()) ||
          (accessKind != AccessKind::Read &&
           !memberDecl->isSetterNonMutating())) {
        return AccessKind::ReadWrite;
      }

      return AccessKind::Read;
    }

    void visitTupleExpr(TupleExpr *E, AccessKind accessKind) {
      for (auto elt : E->getElements()) {
        visit(elt, accessKind);
      }
    }

    void visitOpenExistentialExpr(OpenExistentialExpr *E,
                                  AccessKind accessKind) {
      bool opaqueValueHadAK = E->getOpaqueValue()->hasLValueAccessKind();
      AccessKind oldOpaqueValueAK =
        (opaqueValueHadAK ? E->getOpaqueValue()->getLValueAccessKind()
                          : AccessKind::Read);

      visit(E->getSubExpr(), accessKind);

      // Propagate the new access kind from the OVE to the original existential
      // if we just set or changed it on the OVE.
      if (E->getOpaqueValue()->hasLValueAccessKind()) {
        auto newOpaqueValueAK = E->getOpaqueValue()->getLValueAccessKind();
        if (!opaqueValueHadAK || newOpaqueValueAK != oldOpaqueValueAK)
          visit(E->getExistentialValue(), newOpaqueValueAK);
      }
    }

    LEAF_LVALUE_EXPR(DeclRef)
    LEAF_LVALUE_EXPR(DiscardAssignment)
    LEAF_LVALUE_EXPR(DynamicLookup)
    LEAF_LVALUE_EXPR(OpaqueValue)
    LEAF_LVALUE_EXPR(EditorPlaceholder)

    COMPLETE_PHYSICAL_LVALUE_EXPR(AnyTry, getSubExpr())
    PARTIAL_PHYSICAL_LVALUE_EXPR(BindOptional, getSubExpr())
    COMPLETE_PHYSICAL_LVALUE_EXPR(DotSyntaxBaseIgnored, getRHS());
    PARTIAL_PHYSICAL_LVALUE_EXPR(ForceValue, getSubExpr())
    COMPLETE_PHYSICAL_LVALUE_EXPR(Identity, getSubExpr())
    PARTIAL_PHYSICAL_LVALUE_EXPR(TupleElement, getBase())

    NON_LVALUE_EXPR(Error)
    NON_LVALUE_EXPR(Literal)
    NON_LVALUE_EXPR(SuperRef)
    NON_LVALUE_EXPR(Type)
    NON_LVALUE_EXPR(OtherConstructorDeclRef)
    NON_LVALUE_EXPR(Collection)
    NON_LVALUE_EXPR(CaptureList)
    NON_LVALUE_EXPR(AbstractClosure)
    NON_LVALUE_EXPR(InOut)
    NON_LVALUE_EXPR(DynamicType)
    NON_LVALUE_EXPR(RebindSelfInConstructor)
    NON_LVALUE_EXPR(Apply)
    NON_LVALUE_EXPR(ImplicitConversion)
    NON_LVALUE_EXPR(ExplicitCast)
    NON_LVALUE_EXPR(OptionalEvaluation)
    NON_LVALUE_EXPR(If)
    NON_LVALUE_EXPR(Assign)
    NON_LVALUE_EXPR(DefaultValue)
    NON_LVALUE_EXPR(CodeCompletion)
    NON_LVALUE_EXPR(ObjCSelector)
    NON_LVALUE_EXPR(ObjCKeyPath)
    NON_LVALUE_EXPR(EnumIsCase)

#define UNCHECKED_EXPR(KIND, BASE) \
    NON_LVALUE_EXPR(KIND)
#include "swift/AST/ExprNodes.def"

#undef PHYSICAL_LVALUE_EXPR
#undef LEAF_LVALUE_EXPR
#undef NON_LVALUE_EXPR
  };

  PropagateAccessKind(allowOverwrite).visit(this, accessKind);
}

ConcreteDeclRef Expr::getReferencedDecl() const {
  switch (getKind()) {
  // No declaration reference.
  #define NO_REFERENCE(Id) case ExprKind::Id: return ConcreteDeclRef()
  #define SIMPLE_REFERENCE(Id, Getter)          \
    case ExprKind::Id:                          \
      return cast<Id##Expr>(this)->Getter()
  #define PASS_THROUGH_REFERENCE(Id, GetSubExpr)                      \
    case ExprKind::Id:                                                \
      return cast<Id##Expr>(this)->GetSubExpr()->getReferencedDecl()

  NO_REFERENCE(Error);
  NO_REFERENCE(NilLiteral);
  NO_REFERENCE(IntegerLiteral);
  NO_REFERENCE(FloatLiteral);
  NO_REFERENCE(BooleanLiteral);
  NO_REFERENCE(StringLiteral);
  NO_REFERENCE(InterpolatedStringLiteral);
  NO_REFERENCE(ObjectLiteral);
  NO_REFERENCE(MagicIdentifierLiteral);
  NO_REFERENCE(DiscardAssignment);

  SIMPLE_REFERENCE(DeclRef, getDeclRef);
  SIMPLE_REFERENCE(SuperRef, getSelf);

  case ExprKind::Type: {
    auto typeRepr = cast<TypeExpr>(this)->getTypeRepr();
    if (!typeRepr) return ConcreteDeclRef();
    auto ident = dyn_cast<IdentTypeRepr>(typeRepr);
    if (!ident) return ConcreteDeclRef();
    return ident->getComponentRange().back()->getBoundDecl();
  }

  SIMPLE_REFERENCE(OtherConstructorDeclRef, getDeclRef);

  PASS_THROUGH_REFERENCE(DotSyntaxBaseIgnored, getRHS);

  // FIXME: Return multiple results?
  case ExprKind::OverloadedDeclRef:
    return ConcreteDeclRef();

  NO_REFERENCE(UnresolvedDeclRef);

  SIMPLE_REFERENCE(MemberRef, getMember);
  SIMPLE_REFERENCE(DynamicMemberRef, getMember);
  SIMPLE_REFERENCE(DynamicSubscript, getMember);

  PASS_THROUGH_REFERENCE(UnresolvedSpecialize, getSubExpr);

  NO_REFERENCE(UnresolvedMember);
  NO_REFERENCE(UnresolvedDot);
  NO_REFERENCE(Sequence);
  PASS_THROUGH_REFERENCE(Paren, getSubExpr);
  PASS_THROUGH_REFERENCE(DotSelf, getSubExpr);
  PASS_THROUGH_REFERENCE(Try, getSubExpr);
  PASS_THROUGH_REFERENCE(ForceTry, getSubExpr);
  PASS_THROUGH_REFERENCE(OptionalTry, getSubExpr);

  NO_REFERENCE(Tuple);
  NO_REFERENCE(Array);
  NO_REFERENCE(Dictionary);

  case ExprKind::Subscript: {
    auto subscript = cast<SubscriptExpr>(this);
    if (subscript->hasDecl()) return subscript->getDecl();
    return ConcreteDeclRef();
  }

  NO_REFERENCE(TupleElement);
  NO_REFERENCE(CaptureList);
  NO_REFERENCE(Closure);

  PASS_THROUGH_REFERENCE(AutoClosure, getSingleExpressionBody);
  PASS_THROUGH_REFERENCE(InOut, getSubExpr);

  NO_REFERENCE(DynamicType);

  PASS_THROUGH_REFERENCE(RebindSelfInConstructor, getSubExpr);

  NO_REFERENCE(OpaqueValue);
  PASS_THROUGH_REFERENCE(BindOptional, getSubExpr);
  PASS_THROUGH_REFERENCE(OptionalEvaluation, getSubExpr);
  PASS_THROUGH_REFERENCE(ForceValue, getSubExpr);
  PASS_THROUGH_REFERENCE(OpenExistential, getSubExpr);

  NO_REFERENCE(Call);
  NO_REFERENCE(PrefixUnary);
  NO_REFERENCE(PostfixUnary);
  NO_REFERENCE(Binary);
  NO_REFERENCE(DotSyntaxCall);

  PASS_THROUGH_REFERENCE(ConstructorRefCall, getFn);
  PASS_THROUGH_REFERENCE(Load, getSubExpr);
  NO_REFERENCE(TupleShuffle);
  NO_REFERENCE(UnresolvedTypeConversion);
  PASS_THROUGH_REFERENCE(FunctionConversion, getSubExpr);
  PASS_THROUGH_REFERENCE(CovariantFunctionConversion, getSubExpr);
  PASS_THROUGH_REFERENCE(CovariantReturnConversion, getSubExpr);
  PASS_THROUGH_REFERENCE(MetatypeConversion, getSubExpr);
  PASS_THROUGH_REFERENCE(CollectionUpcastConversion, getSubExpr);
  PASS_THROUGH_REFERENCE(Erasure, getSubExpr);
  PASS_THROUGH_REFERENCE(AnyHashableErasure, getSubExpr);
  PASS_THROUGH_REFERENCE(DerivedToBase, getSubExpr);
  PASS_THROUGH_REFERENCE(ArchetypeToSuper, getSubExpr);
  PASS_THROUGH_REFERENCE(InjectIntoOptional, getSubExpr);
  PASS_THROUGH_REFERENCE(ClassMetatypeToObject, getSubExpr);
  PASS_THROUGH_REFERENCE(ExistentialMetatypeToObject, getSubExpr);
  PASS_THROUGH_REFERENCE(ProtocolMetatypeToObject, getSubExpr);
  PASS_THROUGH_REFERENCE(InOutToPointer, getSubExpr);
  PASS_THROUGH_REFERENCE(ArrayToPointer, getSubExpr);
  PASS_THROUGH_REFERENCE(StringToPointer, getSubExpr);
  PASS_THROUGH_REFERENCE(PointerToPointer, getSubExpr);
  PASS_THROUGH_REFERENCE(LValueToPointer, getSubExpr);
  PASS_THROUGH_REFERENCE(ForeignObjectConversion, getSubExpr);
  PASS_THROUGH_REFERENCE(UnevaluatedInstance, getSubExpr);
  NO_REFERENCE(Coerce);
  NO_REFERENCE(ForcedCheckedCast);
  NO_REFERENCE(ConditionalCheckedCast);
  NO_REFERENCE(Is);

  NO_REFERENCE(Arrow);
  NO_REFERENCE(If);
  NO_REFERENCE(EnumIsCase);
  NO_REFERENCE(Assign);
  NO_REFERENCE(DefaultValue);
  NO_REFERENCE(CodeCompletion);
  NO_REFERENCE(UnresolvedPattern);
  NO_REFERENCE(EditorPlaceholder);
  NO_REFERENCE(ObjCSelector);
  NO_REFERENCE(ObjCKeyPath);

#undef SIMPLE_REFERENCE
#undef NO_REFERENCE
#undef PASS_THROUGH_REFERENCE
  }

  return ConcreteDeclRef();
}

/// Enumerate each immediate child expression of this node, invoking the
/// specific functor on it.  This ignores statements and other non-expression
/// children.
void Expr::
forEachImmediateChildExpr(const std::function<Expr*(Expr*)> &callback) {
  struct ChildWalker : ASTWalker {
    const std::function<Expr*(Expr*)> &callback;
    Expr *ThisNode;
    
    ChildWalker(const std::function<Expr*(Expr*)> &callback, Expr *ThisNode)
      : callback(callback), ThisNode(ThisNode) {}
    
    std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
      // When looking at the current node, of course we want to enter it.  We
      // also don't want to enumerate it.
      if (E == ThisNode)
        return { true, E };

      // Otherwise we must be a child of our expression, enumerate it!
      return { false, callback(E) };
    }
    
    std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
      return { false, S };
    }

    std::pair<bool, Pattern*> walkToPatternPre(Pattern *P) override {
      return { false, P };
    }
    bool walkToDeclPre(Decl *D) override { return false; }
    bool walkToTypeReprPre(TypeRepr *T) override { return false; }
    bool walkToTypeLocPre(TypeLoc &TL) override { return false; }
  };
  
  this->walk(ChildWalker(callback, this));
}

/// Enumerate each immediate child expression of this node, invoking the
/// specific functor on it.  This ignores statements and other non-expression
/// children.
void Expr::forEachChildExpr(const std::function<Expr*(Expr*)> &callback) {
  struct ChildWalker : ASTWalker {
    const std::function<Expr*(Expr*)> &callback;

    ChildWalker(const std::function<Expr*(Expr*)> &callback)
    : callback(callback) {}

    std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
      // Enumerate the node!
      return { true, callback(E) };
    }

    std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
      return { false, S };
    }

    std::pair<bool, Pattern*> walkToPatternPre(Pattern *P) override {
      return { false, P };
    }
    bool walkToDeclPre(Decl *D) override { return false; }
    bool walkToTypeReprPre(TypeRepr *T) override { return false; }
    bool walkToTypeLocPre(TypeLoc &TL) override { return false; }
  };

  this->walk(ChildWalker(callback));
}

Initializer *Expr::findExistingInitializerContext() {
  struct FindExistingInitializer : ASTWalker {
    Initializer *TheInitializer = nullptr;
    std::pair<bool,Expr*> walkToExprPre(Expr *E) override {
      assert(!TheInitializer && "continuing to walk after finding context?");
      if (auto closure = dyn_cast<AbstractClosureExpr>(E)) {
        TheInitializer = cast<Initializer>(closure->getParent());
        return { false, nullptr };
      }
      return { true, E };
    }
  } finder;
  walk(finder);
  return finder.TheInitializer;
}

bool Expr::isTypeReference() const {
  // If the result isn't a metatype, there's nothing else to do.
  if (!getType()->is<AnyMetatypeType>())
    return false;
  
  const Expr *expr = this;
  do {
    // Skip syntax.
    expr = expr->getSemanticsProvidingExpr();

    // Direct reference to a type.
    if (auto declRef = dyn_cast<DeclRefExpr>(expr))
      if (isa<TypeDecl>(declRef->getDecl()))
        return true;
    if (isa<TypeExpr>(expr))
      return true;

    // A "." expression that refers to a member.
    if (auto memberRef = dyn_cast<MemberRefExpr>(expr))
      return isa<TypeDecl>(memberRef->getMember().getDecl());

    // When the base of a "." expression is ignored, look at the member.
    if (auto ignoredDot = dyn_cast<DotSyntaxBaseIgnoredExpr>(expr)) {
      expr = ignoredDot->getRHS();
      continue;
    }

    // Anything else is not statically derived.
    return false;
  } while (true);

}

bool Expr::isStaticallyDerivedMetatype() const {
  // The type must first be a type reference.
  if (!isTypeReference())
    return false;

  // Archetypes are never statically derived.
  return !getType()->getAs<AnyMetatypeType>()->getInstanceType()
    ->is<ArchetypeType>();
}

bool Expr::isSuperExpr() const {
  const Expr *expr = this;
  do {
    expr = expr->getSemanticsProvidingExpr();

    if (isa<SuperRefExpr>(expr))
      return true;

    if (auto derivedToBase = dyn_cast<DerivedToBaseExpr>(expr)) {
      expr = derivedToBase->getSubExpr();
      continue;
    }
    if (auto metatypeConversion = dyn_cast<MetatypeConversionExpr>(expr)) {
      expr = metatypeConversion->getSubExpr();
      continue;
    }

    return false;
  } while (true);
}

bool Expr::canAppendCallParentheses() const {
  switch (getKind()) {
  case ExprKind::Error:
  case ExprKind::CodeCompletion:
    return false;

  case ExprKind::NilLiteral:
  case ExprKind::IntegerLiteral:
  case ExprKind::FloatLiteral:
  case ExprKind::BooleanLiteral:
  case ExprKind::StringLiteral:
  case ExprKind::InterpolatedStringLiteral:
  case ExprKind::MagicIdentifierLiteral:
  case ExprKind::ObjCSelector:
  case ExprKind::ObjCKeyPath:
    return true;

  case ExprKind::ObjectLiteral:
    return true;

  case ExprKind::DiscardAssignment:
    // Legal but pointless.
    return true;

  case ExprKind::DeclRef:
    return !cast<DeclRefExpr>(this)->getDecl()->getName().isOperator();

  case ExprKind::SuperRef:
  case ExprKind::Type:
  case ExprKind::OtherConstructorDeclRef:
  case ExprKind::DotSyntaxBaseIgnored:
    return true;

  case ExprKind::OverloadedDeclRef: {
    auto *overloadedExpr = cast<OverloadedDeclRefExpr>(this);
    if (overloadedExpr->getDecls().empty())
      return false;
    return !overloadedExpr->getDecls().front()->getName().isOperator();
  }

  case ExprKind::UnresolvedDeclRef:
    return cast<UnresolvedDeclRefExpr>(this)->getName().isOperator();

  case ExprKind::MemberRef:
  case ExprKind::DynamicMemberRef:
  case ExprKind::DynamicSubscript:
  case ExprKind::UnresolvedSpecialize:
  case ExprKind::UnresolvedMember:
  case ExprKind::UnresolvedDot:
    return true;

  case ExprKind::Sequence:
    return false;

  case ExprKind::Paren:
  case ExprKind::DotSelf:
  case ExprKind::Tuple:
  case ExprKind::Array:
  case ExprKind::Dictionary:
  case ExprKind::Subscript:
  case ExprKind::TupleElement:
    return true;

  case ExprKind::CaptureList:
  case ExprKind::Closure:
  case ExprKind::AutoClosure:
    return false;

  case ExprKind::DynamicType:
    return true;

  case ExprKind::Try:
  case ExprKind::ForceTry:
  case ExprKind::OptionalTry:
  case ExprKind::InOut:
    return false;

  case ExprKind::RebindSelfInConstructor:
  case ExprKind::OpaqueValue:
  case ExprKind::BindOptional:
  case ExprKind::OptionalEvaluation:
    return false;

  case ExprKind::ForceValue:
    return true;

  case ExprKind::OpenExistential:
    return false;

  case ExprKind::Call:
  case ExprKind::PostfixUnary:
  case ExprKind::DotSyntaxCall:
  case ExprKind::ConstructorRefCall:
    return true;

  case ExprKind::PrefixUnary:
  case ExprKind::Binary:
    return false;

  case ExprKind::Load:
  case ExprKind::TupleShuffle:
  case ExprKind::UnresolvedTypeConversion:
  case ExprKind::FunctionConversion:
  case ExprKind::CovariantFunctionConversion:
  case ExprKind::CovariantReturnConversion:
  case ExprKind::MetatypeConversion:
  case ExprKind::CollectionUpcastConversion:
  case ExprKind::Erasure:
  case ExprKind::AnyHashableErasure:
  case ExprKind::DerivedToBase:
  case ExprKind::ArchetypeToSuper:
  case ExprKind::InjectIntoOptional:
  case ExprKind::ClassMetatypeToObject:
  case ExprKind::ExistentialMetatypeToObject:
  case ExprKind::ProtocolMetatypeToObject:
  case ExprKind::InOutToPointer:
  case ExprKind::ArrayToPointer:
  case ExprKind::StringToPointer:
  case ExprKind::PointerToPointer:
  case ExprKind::LValueToPointer:
  case ExprKind::ForeignObjectConversion:
  case ExprKind::UnevaluatedInstance:
  case ExprKind::EnumIsCase:
    // Implicit conversion nodes have no syntax of their own; defer to the
    // subexpression.
    return cast<ImplicitConversionExpr>(this)->getSubExpr()
      ->canAppendCallParentheses();

  case ExprKind::ForcedCheckedCast:
  case ExprKind::ConditionalCheckedCast:
  case ExprKind::Is:
  case ExprKind::Coerce:
    return false;

  case ExprKind::Arrow:
  case ExprKind::If:
  case ExprKind::Assign:
  case ExprKind::DefaultValue:
  case ExprKind::UnresolvedPattern:
  case ExprKind::EditorPlaceholder:
    return false;
  }
}

llvm::DenseMap<Expr *, Expr *> Expr::getParentMap() {
  class RecordingTraversal : public ASTWalker {
  public:
    llvm::DenseMap<Expr *, Expr *> &ParentMap;

    explicit RecordingTraversal(llvm::DenseMap<Expr *, Expr *> &parentMap)
      : ParentMap(parentMap) { }

    virtual std::pair<bool, Expr *> walkToExprPre(Expr *E) {
      if (auto parent = Parent.getAsExpr())
        ParentMap[E] = parent;

      return { true, E };
    }
  };

  llvm::DenseMap<Expr *, Expr *> parentMap;
  RecordingTraversal traversal(parentMap);
  walk(traversal);
  return parentMap;
}

llvm::DenseMap<Expr *, unsigned> Expr::getDepthMap() {
  class RecordingTraversal : public ASTWalker {
  public:
    llvm::DenseMap<Expr *, unsigned> &DepthMap;
    unsigned Depth = 0;

    explicit RecordingTraversal(llvm::DenseMap<Expr *, unsigned> &depthMap)
      : DepthMap(depthMap) { }

    virtual std::pair<bool, Expr *> walkToExprPre(Expr *E) {
      DepthMap[E] = Depth;
      Depth++;
      return { true, E };
    }

    virtual Expr *walkToExprPost(Expr *E) {
      Depth--;
      return E;
    }
  };

  llvm::DenseMap<Expr *, unsigned> depthMap;
  RecordingTraversal traversal(depthMap);
  walk(traversal);
  return depthMap;
}

llvm::DenseMap<Expr *, unsigned> Expr::getPreorderIndexMap() {
  class RecordingTraversal : public ASTWalker {
  public:
    llvm::DenseMap<Expr *, unsigned> &IndexMap;
    unsigned Index = 0;

    explicit RecordingTraversal(llvm::DenseMap<Expr *, unsigned> &indexMap)
      : IndexMap(indexMap) { }

    virtual std::pair<bool, Expr *> walkToExprPre(Expr *E) {
      IndexMap[E] = Index;
      Index++;
      return { true, E };
    }
  };

  llvm::DenseMap<Expr *, unsigned> indexMap;
  RecordingTraversal traversal(indexMap);
  walk(traversal);
  return indexMap;
}

//===----------------------------------------------------------------------===//
// Support methods for Exprs.
//===----------------------------------------------------------------------===//

static LiteralExpr *shallowCloneImpl(const NilLiteralExpr *E, ASTContext &Ctx) {
  return new (Ctx) NilLiteralExpr(E->getLoc());
}

static LiteralExpr *
shallowCloneImpl(const IntegerLiteralExpr *E, ASTContext &Ctx) {
  auto res = new (Ctx) IntegerLiteralExpr(E->getDigitsText(),
                                          E->getSourceRange().End);
  if (E->isNegative())
    res->setNegative(E->getSourceRange().Start);
  return res;
}

static LiteralExpr *shallowCloneImpl(const FloatLiteralExpr *E, ASTContext &Ctx) {
  auto res = new (Ctx) FloatLiteralExpr(E->getDigitsText(),
                                        E->getSourceRange().End);
  if (E->isNegative())
    res->setNegative(E->getSourceRange().Start);
  return res;
}
static LiteralExpr *
shallowCloneImpl(const BooleanLiteralExpr *E, ASTContext &Ctx) {
  return new (Ctx) BooleanLiteralExpr(E->getValue(), E->getLoc());
}
static LiteralExpr *shallowCloneImpl(const StringLiteralExpr *E, ASTContext &Ctx) {
  auto res = new (Ctx) StringLiteralExpr(E->getValue(), E->getSourceRange());
  res->setEncoding(E->getEncoding());
  return res;
}

static LiteralExpr *
shallowCloneImpl(const InterpolatedStringLiteralExpr *E, ASTContext &Ctx) {
  auto res = new (Ctx) InterpolatedStringLiteralExpr(E->getLoc(),
                const_cast<InterpolatedStringLiteralExpr*>(E)->getSegments());
  res->setSemanticExpr(E->getSemanticExpr());
  return res;
}


static LiteralExpr *
shallowCloneImpl(const MagicIdentifierLiteralExpr *E, ASTContext &Ctx) {
  auto res = new (Ctx) MagicIdentifierLiteralExpr(E->getKind(),
                                                  E->getSourceRange().End);
  if (res->isString())
    res->setStringEncoding(E->getStringEncoding());
  return res;
}

static LiteralExpr *
shallowCloneImpl(const ObjectLiteralExpr *E, ASTContext &Ctx) {
  auto res = ObjectLiteralExpr::create(Ctx, E->getStartLoc(),
                                       E->getLiteralKind(),
                                       E->getArg(), E->isImplicit());
  res->setSemanticExpr(E->getSemanticExpr());
  return res;
}

// Make an exact copy of this AST node.
LiteralExpr *LiteralExpr::shallowClone(ASTContext &Ctx) const {
  LiteralExpr *Result = nullptr;
  switch (getKind()) {
  default: llvm_unreachable("Unknown literal type!");
#define DISPATCH_CLONE(KIND) \
  case ExprKind::KIND: \
    Result = shallowCloneImpl(cast<KIND##Expr>(this), Ctx); \
    break;

  DISPATCH_CLONE(NilLiteral)
  DISPATCH_CLONE(IntegerLiteral)
  DISPATCH_CLONE(FloatLiteral)
  DISPATCH_CLONE(BooleanLiteral)
  DISPATCH_CLONE(StringLiteral)
  DISPATCH_CLONE(InterpolatedStringLiteral)
  DISPATCH_CLONE(ObjectLiteral)
  DISPATCH_CLONE(MagicIdentifierLiteral)
#undef DISPATCH_CLONE
  }
  
  Result->setType(getType());
  Result->setImplicit(isImplicit());
  return Result;
}




static APInt getIntegerLiteralValue(bool IsNegative, StringRef Text,
                                    unsigned BitWidth) {
  llvm::APInt Value(BitWidth, 0);
  // swift encodes octal differently from C
  bool IsCOctal = Text.size() > 1 && Text[0] == '0' && isdigit(Text[1]);
  bool Error = Text.getAsInteger(IsCOctal ? 10 : 0, Value);
  assert(!Error && "Invalid IntegerLiteral formed"); (void)Error;
  if (IsNegative)
    Value = -Value;
  if (Value.getBitWidth() != BitWidth)
    Value = Value.sextOrTrunc(BitWidth);
  return Value;
}

APInt IntegerLiteralExpr::getValue(StringRef Text, unsigned BitWidth) {
  return getIntegerLiteralValue(/*IsNegative=*/false, Text, BitWidth);
}

APInt IntegerLiteralExpr::getValue() const {
  assert(!getType().isNull() && "Semantic analysis has not completed");
  assert(!getType()->is<ErrorType>() && "Should have a valid type");
  return getIntegerLiteralValue(
      isNegative(), getDigitsText(),
      getType()->castTo<BuiltinIntegerType>()->getGreatestWidth());
}

static APFloat getFloatLiteralValue(bool IsNegative, StringRef Text,
                                    const llvm::fltSemantics &Semantics) {
  APFloat Val(Semantics);
  APFloat::opStatus Res =
    Val.convertFromString(Text, llvm::APFloat::rmNearestTiesToEven);
  assert(Res != APFloat::opInvalidOp && "Sema didn't reject invalid number");
  (void)Res;
  if (IsNegative) {
    auto NegVal = APFloat::getZero(Semantics, /*negative*/ true);
    Res = NegVal.subtract(Val, llvm::APFloat::rmNearestTiesToEven);
    assert(Res != APFloat::opInvalidOp && "Sema didn't reject invalid number");
    (void)Res;
    return NegVal;
  }
  return Val;
}

APFloat FloatLiteralExpr::getValue(StringRef Text,
                                   const llvm::fltSemantics &Semantics) {
  return getFloatLiteralValue(/*IsNegative*/false, Text, Semantics);
}

llvm::APFloat FloatLiteralExpr::getValue() const {
  assert(!getType().isNull() && "Semantic analysis has not completed");
  assert(!getType()->is<ErrorType>() && "Should have a valid type");

  return getFloatLiteralValue(isNegative(), getDigitsText(),
                  getType()->castTo<BuiltinFloatType>()->getAPFloatSemantics());
}

StringLiteralExpr::StringLiteralExpr(StringRef Val, SourceRange Range,
                                     bool Implicit)
    : LiteralExpr(ExprKind::StringLiteral, Implicit), Val(Val),
      Range(Range) {
  StringLiteralExprBits.Encoding = static_cast<unsigned>(UTF8);
  StringLiteralExprBits.IsSingleUnicodeScalar =
      unicode::isSingleUnicodeScalar(Val);
  StringLiteralExprBits.IsSingleExtendedGraphemeCluster =
      unicode::isSingleExtendedGraphemeCluster(Val);
}

static ArrayRef<Identifier>
getArgumentLabelsFromArgument(Expr *arg, SmallVectorImpl<Identifier> &scratch,
                              SmallVectorImpl<SourceLoc> *sourceLocs = nullptr,
                              bool *hasTrailingClosure = nullptr){
  if (sourceLocs) sourceLocs->clear();
  if (hasTrailingClosure) *hasTrailingClosure = false;

  // A parenthesized expression is a single, unlabeled argument.
  if (auto paren = dyn_cast<ParenExpr>(arg)) {
    scratch.clear();
    scratch.push_back(Identifier());
    if (hasTrailingClosure) *hasTrailingClosure = paren->hasTrailingClosure();
    return scratch;
  }

  // A tuple expression stores its element names, if they exist.
  if (auto tuple = dyn_cast<TupleExpr>(arg)) {
    if (sourceLocs && tuple->hasElementNameLocs()) {
      sourceLocs->append(tuple->getElementNameLocs().begin(),
                         tuple->getElementNameLocs().end());
    }

    if (hasTrailingClosure) *hasTrailingClosure = tuple->hasTrailingClosure();

    if (tuple->hasElementNames()) {
      assert(tuple->getElementNames().size() == tuple->getNumElements());
      return tuple->getElementNames();
    }

    scratch.assign(tuple->getNumElements(), Identifier());
    return scratch;
  }

  // Otherwise, use the type information.
  auto type = arg->getType();
  if (isa<ParenType>(type.getPointer())) {
    scratch.clear();
    scratch.push_back(Identifier());
    return scratch;    
  }

  // FIXME: Should be a dyn_cast.
  if (auto tupleTy = type->getAs<TupleType>()) {
    scratch.clear();
    for (const auto &elt : tupleTy->getElements())
      scratch.push_back(elt.getName());
    return scratch;
  }

  // FIXME: Shouldn't get here.
  scratch.clear();
  scratch.push_back(Identifier());
  return scratch;    
}

/// Compute the type of an argument to a call (or call-like) AST
static void computeSingleArgumentType(ASTContext &ctx, Expr *arg,
                                      bool implicit) {
  // Propagate 'implicit' to the argument.
  if (implicit)
    arg->setImplicit(true);

  // Handle parenthesized expressions.
  if (auto paren = dyn_cast<ParenExpr>(arg)) {
    if (auto type = paren->getSubExpr()->getType()) {
      arg->setType(ParenType::get(ctx, type));
    }
    return;
  }

  // Handle tuples.
  auto tuple = dyn_cast<TupleExpr>(arg);
  SmallVector<TupleTypeElt, 4> typeElements;
  for (unsigned i = 0, n = tuple->getNumElements(); i != n; ++i) {
    auto type = tuple->getElement(i)->getType();
    if (!type) return;

    typeElements.push_back(TupleTypeElt(type, tuple->getElementName(i)));
  }
  arg->setType(TupleType::get(typeElements, ctx));
}

/// Pack the argument information into a single argument, to match the
/// representation expected by the AST.
///
/// \param argLabels The argument labels, which might be updated by this
/// function.
///
/// \param argLabelLocs The argument label locations, which might be updated by
/// this function.
static Expr *packSingleArgument(
    ASTContext &ctx,
    SourceLoc lParenLoc,
    ArrayRef<Expr *> args,
    ArrayRef<Identifier> &argLabels,
    ArrayRef<SourceLoc> &argLabelLocs,
    SourceLoc rParenLoc,
    Expr *trailingClosure,
    bool implicit,
    SmallVectorImpl<Identifier> &argLabelsScratch,
    SmallVectorImpl<SourceLoc> &argLabelLocsScratch) {
  // Clear out our scratch space.
  argLabelsScratch.clear();
  argLabelLocsScratch.clear();

  // Construct a TupleExpr or ParenExpr, as appropriate, for the argument.
  if (!trailingClosure) {
    // Do we have a single, unlabeled argument?
    if (args.size() == 1 && (argLabels.empty() || argLabels[0].empty())) {
      auto arg = new (ctx) ParenExpr(lParenLoc, args[0], rParenLoc,
                                     /*hasTrailingClosure=*/false);
      computeSingleArgumentType(ctx, arg, implicit);
      argLabelsScratch.push_back(Identifier());
      argLabels = argLabelsScratch;
      argLabelLocs = { };
      return arg;
    }

    // Make sure we have argument labels.
    if (argLabels.empty()) {
      argLabelsScratch.assign(args.size(), Identifier());
      argLabels = argLabelsScratch;
    }

    // Construct the argument tuple.
    if (argLabels.empty() && !args.empty()) {
      argLabelsScratch.assign(args.size(), Identifier());
      argLabels = argLabelsScratch;
    }
      
    auto arg = TupleExpr::create(ctx, lParenLoc, args, argLabels, argLabelLocs,
                                 rParenLoc, /*hasTrailingClosure=*/false,
                                 /*implicit=*/false);
    computeSingleArgumentType(ctx, arg, implicit);
    return arg;
  }

  // If we have no other arguments, represent the trailing closure as a
  // parenthesized expression.
  if (args.size() == 0) {
    auto arg = new (ctx) ParenExpr(lParenLoc, trailingClosure, rParenLoc,
                                   /*hasTrailingClosure=*/true);
    computeSingleArgumentType(ctx, arg, implicit);
    argLabelsScratch.push_back(Identifier());
    argLabels = argLabelsScratch;
    argLabelLocs = { };
    return arg;
  }

  assert(argLabels.empty() || args.size() == argLabels.size());

  // Form a tuple, including the trailing closure.
  SmallVector<Expr *, 4> argsScratch;
  argsScratch.reserve(args.size() + 1);
  argsScratch.append(args.begin(), args.end());
  argsScratch.push_back(trailingClosure);
  args = argsScratch;

  argLabelsScratch.reserve(args.size());
  if (argLabels.empty()) {
    argLabelsScratch.assign(args.size(), Identifier());
  } else {
    argLabelsScratch.append(argLabels.begin(), argLabels.end());
    argLabelsScratch.push_back(Identifier());
  }
  argLabels = argLabelsScratch;

  if (!argLabelLocs.empty()) {
    argLabelLocsScratch.reserve(argLabelLocs.size() + 1);
    argLabelLocsScratch.append(argLabelLocs.begin(), argLabelLocs.end());
    argLabelLocsScratch.push_back(SourceLoc());
    argLabelLocs = argLabelLocsScratch;
  }

  auto arg = TupleExpr::create(ctx, lParenLoc, args, argLabels,
                               argLabelLocs, rParenLoc,
                               /*hasTrailingClosure=*/true,
                               /*implicit=*/false);
  computeSingleArgumentType(ctx, arg, implicit);

  return arg;
}

ObjectLiteralExpr::ObjectLiteralExpr(SourceLoc PoundLoc, LiteralKind LitKind,
                                     Expr *Arg,
                                     ArrayRef<Identifier> argLabels,
                                     ArrayRef<SourceLoc> argLabelLocs,
                                     bool hasTrailingClosure,
                                     bool implicit)
    : LiteralExpr(ExprKind::ObjectLiteral, implicit), 
      Arg(Arg), SemanticExpr(nullptr), PoundLoc(PoundLoc) {
  ObjectLiteralExprBits.LitKind = static_cast<unsigned>(LitKind);
  assert(getLiteralKind() == LitKind);
  ObjectLiteralExprBits.NumArgLabels = argLabels.size();
  ObjectLiteralExprBits.HasArgLabelLocs = !argLabelLocs.empty();
  ObjectLiteralExprBits.HasTrailingClosure = hasTrailingClosure;
  initializeCallArguments(argLabels, argLabelLocs, hasTrailingClosure);  
}

ObjectLiteralExpr *ObjectLiteralExpr::create(ASTContext &ctx,
                                             SourceLoc poundLoc,
                                             LiteralKind kind,
                                             Expr *arg,
                                             bool implicit) {
  // Inspect the argument to dig out the argument labels, their location, and
  // whether there is a trailing closure.
  SmallVector<Identifier, 4> argLabelsScratch;
  SmallVector<SourceLoc, 4> argLabelLocs;
  bool hasTrailingClosure = false;
  auto argLabels = getArgumentLabelsFromArgument(arg, argLabelsScratch,
                                                 &argLabelLocs,
                                                 &hasTrailingClosure);

  size_t size = totalSizeToAlloc(argLabels, argLabelLocs, hasTrailingClosure);

  void *memory = ctx.Allocate(size, alignof(ObjectLiteralExpr));
  return new (memory) ObjectLiteralExpr(poundLoc, kind, arg, argLabels,
                                        argLabelLocs, hasTrailingClosure,
                                        implicit);
}

ObjectLiteralExpr *ObjectLiteralExpr::create(ASTContext &ctx,
                                             SourceLoc poundLoc,
                                             LiteralKind kind,
                                             SourceLoc lParenLoc,
                                             ArrayRef<Expr *> args,
                                             ArrayRef<Identifier> argLabels,
                                             ArrayRef<SourceLoc> argLabelLocs,
                                             SourceLoc rParenLoc,
                                             Expr *trailingClosure,
                                             bool implicit) {
  SmallVector<Identifier, 4> argLabelsScratch;
  SmallVector<SourceLoc, 4> argLabelLocsScratch;
  Expr *arg = packSingleArgument(ctx, lParenLoc, args, argLabels, argLabelLocs,
                                 rParenLoc, trailingClosure, implicit,
                                 argLabelsScratch, argLabelLocsScratch);

  size_t size = totalSizeToAlloc(argLabels, argLabelLocs,
                                 trailingClosure != nullptr);

  void *memory = ctx.Allocate(size, alignof(ObjectLiteralExpr));
  return new (memory) ObjectLiteralExpr(poundLoc, kind, arg, argLabels,
                                        argLabelLocs,
                                        trailingClosure != nullptr, implicit);
}

StringRef ObjectLiteralExpr::getLiteralKindRawName() const {
  switch (getLiteralKind()) {
#define POUND_OBJECT_LITERAL(Name, Desc, Proto) case Name: return #Name;
#include "swift/Parse/Tokens.def"    
  }
  llvm_unreachable("unspecified literal");
}

StringRef ObjectLiteralExpr::getLiteralKindPlainName() const {
  switch (getLiteralKind()) {
#define POUND_OBJECT_LITERAL(Name, Desc, Proto) case Name: return Desc;
#include "swift/Parse/Tokens.def"    
  }
  llvm_unreachable("unspecified literal");
}

void DeclRefExpr::setSpecialized() {
  if (isSpecialized())
    return;

  ConcreteDeclRef ref = getDeclRef();
  void *Mem = ref.getDecl()->getASTContext().Allocate(sizeof(SpecializeInfo),
                                                      alignof(SpecializeInfo));
  auto Spec = new (Mem) SpecializeInfo;
  Spec->D = ref;
  DOrSpecialized = Spec;
}

void DeclRefExpr::setGenericArgs(ArrayRef<TypeRepr*> GenericArgs) {
  ValueDecl *D = getDecl();
  assert(D);
  setSpecialized();
  getSpecInfo()->GenericArgs = D->getASTContext().AllocateCopy(GenericArgs);
}

ConstructorDecl *OtherConstructorDeclRefExpr::getDecl() const {
  return cast_or_null<ConstructorDecl>(Ctor.getDecl());
}

MemberRefExpr::MemberRefExpr(Expr *base, SourceLoc dotLoc,
                             ConcreteDeclRef member, DeclNameLoc nameLoc,
                             bool Implicit, AccessSemantics semantics)
  : Expr(ExprKind::MemberRef, Implicit), Base(base),
    Member(member), DotLoc(dotLoc), NameLoc(nameLoc) {
   
  MemberRefExprBits.Semantics = (unsigned) semantics;
  MemberRefExprBits.IsSuper = false;
}

Type OverloadSetRefExpr::getBaseType() const {
  if (isa<OverloadedDeclRefExpr>(this))
    return Type();
  
  llvm_unreachable("Unhandled overloaded set reference expression");
}

bool OverloadSetRefExpr::hasBaseObject() const {
  if (Type BaseTy = getBaseType())
    return !BaseTy->is<AnyMetatypeType>();

  return false;
}

SequenceExpr *SequenceExpr::create(ASTContext &ctx, ArrayRef<Expr*> elements) {
  assert(elements.size() & 1 && "even number of elements in sequence");
  void *Buffer = ctx.Allocate(sizeof(SequenceExpr) +
                              elements.size() * sizeof(Expr*),
                              alignof(SequenceExpr));
  return ::new(Buffer) SequenceExpr(elements);
}

SourceLoc TupleExpr::getStartLoc() const {
  if (LParenLoc.isValid()) return LParenLoc;
  if (getNumElements() == 0) return SourceLoc();
  return getElement(0)->getStartLoc();
}

SourceLoc TupleExpr::getEndLoc() const {
  if (hasTrailingClosure() || RParenLoc.isInvalid()) {
    if (getNumElements() == 0) return SourceLoc();
    return getElements().back()->getEndLoc();
  }
  return RParenLoc;
}

TupleExpr::TupleExpr(SourceLoc LParenLoc, ArrayRef<Expr *> SubExprs,
                     ArrayRef<Identifier> ElementNames, 
                     ArrayRef<SourceLoc> ElementNameLocs,
                     SourceLoc RParenLoc, bool HasTrailingClosure, 
                     bool Implicit, Type Ty)
  : Expr(ExprKind::Tuple, Implicit, Ty),
    LParenLoc(LParenLoc), RParenLoc(RParenLoc),
    NumElements(SubExprs.size())
{
  TupleExprBits.HasTrailingClosure = HasTrailingClosure;
  TupleExprBits.HasElementNames = !ElementNames.empty();
  TupleExprBits.HasElementNameLocations = !ElementNameLocs.empty();
  
  assert(LParenLoc.isValid() == RParenLoc.isValid() &&
         "Mismatched parenthesis location information validity");
  assert(ElementNames.empty() || ElementNames.size() == SubExprs.size());
  assert(ElementNameLocs.empty() || 
         ElementNames.size() == ElementNameLocs.size());

  // Copy elements.
  std::uninitialized_copy(SubExprs.begin(), SubExprs.end(),
                          getTrailingObjects<Expr *>());

  // Copy element names, if provided.
  if (hasElementNames()) {
    std::uninitialized_copy(ElementNames.begin(), ElementNames.end(),
                            getTrailingObjects<Identifier>());
  }

  // Copy element name locations, if provided.
  if (hasElementNameLocs()) {
    std::uninitialized_copy(ElementNameLocs.begin(), ElementNameLocs.end(),
                            getTrailingObjects<SourceLoc>());
  }
}

TupleExpr *TupleExpr::create(ASTContext &ctx,
                             SourceLoc LParenLoc, 
                             ArrayRef<Expr *> SubExprs,
                             ArrayRef<Identifier> ElementNames, 
                             ArrayRef<SourceLoc> ElementNameLocs,
                             SourceLoc RParenLoc, bool HasTrailingClosure, 
                             bool Implicit, Type Ty) {
  size_t size =
      totalSizeToAlloc<Expr *, Identifier, SourceLoc>(SubExprs.size(),
                                                      ElementNames.size(),
                                                      ElementNameLocs.size());
  void *mem = ctx.Allocate(size, alignof(TupleExpr));
  return new (mem) TupleExpr(LParenLoc, SubExprs, ElementNames, ElementNameLocs,
                             RParenLoc, HasTrailingClosure, Implicit, Ty);
}

TupleExpr *TupleExpr::createEmpty(ASTContext &ctx, SourceLoc LParenLoc, 
                                  SourceLoc RParenLoc, bool Implicit) {
  return create(ctx, LParenLoc, { }, { }, { }, RParenLoc, 
                /*HasTrailingClosure=*/false, Implicit, 
                TupleType::getEmpty(ctx));
}

TupleExpr *TupleExpr::createImplicit(ASTContext &ctx, ArrayRef<Expr *> SubExprs,
                                     ArrayRef<Identifier> ElementNames) {
  return create(ctx, SourceLoc(), SubExprs, ElementNames, { }, SourceLoc(),
                /*HasTrailingClosure=*/false, /*Implicit=*/true, Type());
}


ArrayExpr *ArrayExpr::create(ASTContext &C, SourceLoc LBracketLoc,
                             ArrayRef<Expr*> Elements,
                             ArrayRef<SourceLoc> CommaLocs,
                             SourceLoc RBracketLoc, Type Ty) {
  // Copy the element list into the ASTContext.
  auto NewElements = C.AllocateCopy(Elements);
  auto NewCommas = C.AllocateCopy(CommaLocs);
  return new (C) ArrayExpr(LBracketLoc, NewElements, NewCommas, RBracketLoc,Ty);
}

DictionaryExpr *DictionaryExpr::create(ASTContext &C, SourceLoc LBracketLoc,
                             ArrayRef<Expr*> Elements, SourceLoc RBracketLoc,
                             Type Ty) {
  // Copy the element list into the ASTContext.
  auto NewElements = C.AllocateCopy(Elements);
  return new (C) DictionaryExpr(LBracketLoc, NewElements, RBracketLoc, Ty);
}

static ValueDecl *getCalledValue(Expr *E) {
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl();

  Expr *E2 = E->getValueProvidingExpr();
  if (E != E2) return getCalledValue(E2);
  return nullptr;
}

ValueDecl *ApplyExpr::getCalledValue() const {
  return ::getCalledValue(Fn);
}

SubscriptExpr::SubscriptExpr(Expr *base, Expr *index,
                             ArrayRef<Identifier> argLabels,
                             ArrayRef<SourceLoc> argLabelLocs,
                             bool hasTrailingClosure,
                             ConcreteDeclRef decl,
                             bool implicit, AccessSemantics semantics)
    : Expr(ExprKind::Subscript, implicit, Type()),
      TheDecl(decl), Base(base), Index(index) {
  SubscriptExprBits.Semantics = (unsigned) semantics;
  SubscriptExprBits.IsSuper = false;
  SubscriptExprBits.NumArgLabels = argLabels.size();
  SubscriptExprBits.HasArgLabelLocs = !argLabelLocs.empty();
  SubscriptExprBits.HasTrailingClosure = hasTrailingClosure;
  initializeCallArguments(argLabels, argLabelLocs, hasTrailingClosure);
}

SubscriptExpr *SubscriptExpr::create(ASTContext &ctx, Expr *base, Expr *index,
                                     ConcreteDeclRef decl, bool implicit,
                                     AccessSemantics semantics) {
  // Inspect the argument to dig out the argument labels, their location, and
  // whether there is a trailing closure.
  SmallVector<Identifier, 4> argLabelsScratch;
  SmallVector<SourceLoc, 4> argLabelLocs;
  bool hasTrailingClosure = false;
  auto argLabels = getArgumentLabelsFromArgument(index, argLabelsScratch,
                                                 &argLabelLocs,
                                                 &hasTrailingClosure);

  size_t size = totalSizeToAlloc(argLabels, argLabelLocs, hasTrailingClosure);

  void *memory = ctx.Allocate(size, alignof(SubscriptExpr));
  return new (memory) SubscriptExpr(base, index, argLabels, argLabelLocs,
                                    hasTrailingClosure, decl, implicit,
                                    semantics);
}

SubscriptExpr *SubscriptExpr::create(ASTContext &ctx, Expr *base,
                                     SourceLoc lSquareLoc,
                                     ArrayRef<Expr *> indexArgs,
                                     ArrayRef<Identifier> indexArgLabels,
                                     ArrayRef<SourceLoc> indexArgLabelLocs,
                                     SourceLoc rSquareLoc,
                                     Expr *trailingClosure,
                                     ConcreteDeclRef decl,
                                     bool implicit,
                                     AccessSemantics semantics) {
  SmallVector<Identifier, 4> indexArgLabelsScratch;
  SmallVector<SourceLoc, 4> indexArgLabelLocsScratch;
  Expr *index = packSingleArgument(ctx, lSquareLoc, indexArgs, indexArgLabels,
                                   indexArgLabelLocs, rSquareLoc,
                                   trailingClosure, implicit,
                                   indexArgLabelsScratch,
                                   indexArgLabelLocsScratch);

  size_t size = totalSizeToAlloc(indexArgLabels, indexArgLabelLocs,
                                 trailingClosure != nullptr);

  void *memory = ctx.Allocate(size, alignof(SubscriptExpr));
  return new (memory) SubscriptExpr(base, index, indexArgLabels,
                                    indexArgLabelLocs,
                                    trailingClosure != nullptr,
                                    decl, implicit, semantics);
}

DynamicSubscriptExpr::DynamicSubscriptExpr(Expr *base, Expr *index,
                                           ArrayRef<Identifier> argLabels,
                                           ArrayRef<SourceLoc> argLabelLocs,
                                           bool hasTrailingClosure,
                                           ConcreteDeclRef member,
                                           bool implicit)
    : DynamicLookupExpr(ExprKind::DynamicSubscript),
      Base(base), Index(index), Member(member) {
  DynamicSubscriptExprBits.NumArgLabels = argLabels.size();
  DynamicSubscriptExprBits.HasArgLabelLocs = !argLabelLocs.empty();
  DynamicSubscriptExprBits.HasTrailingClosure = hasTrailingClosure;
  initializeCallArguments(argLabels, argLabelLocs, hasTrailingClosure);
  if (implicit) setImplicit(implicit);
}

DynamicSubscriptExpr *
DynamicSubscriptExpr::create(ASTContext &ctx, Expr *base, Expr *index,
                             ConcreteDeclRef decl, bool implicit) {
  // Inspect the argument to dig out the argument labels, their location, and
  // whether there is a trailing closure.
  SmallVector<Identifier, 4> argLabelsScratch;
  SmallVector<SourceLoc, 4> argLabelLocs;
  bool hasTrailingClosure = false;
  auto argLabels = getArgumentLabelsFromArgument(index, argLabelsScratch,
                                                 &argLabelLocs,
                                                 &hasTrailingClosure);

  size_t size = totalSizeToAlloc(argLabels, argLabelLocs, hasTrailingClosure);

  void *memory = ctx.Allocate(size, alignof(DynamicSubscriptExpr));
  return new (memory) DynamicSubscriptExpr(base, index, argLabels, argLabelLocs,
                                           hasTrailingClosure, decl, implicit);
}

DynamicSubscriptExpr *
DynamicSubscriptExpr::create(ASTContext &ctx, Expr *base, SourceLoc lSquareLoc,
                             ArrayRef<Expr *> indexArgs,
                             ArrayRef<Identifier> indexArgLabels,
                             ArrayRef<SourceLoc> indexArgLabelLocs,
                             SourceLoc rSquareLoc,
                             Expr *trailingClosure,
                             ConcreteDeclRef decl,
                             bool implicit) {
  SmallVector<Identifier, 4> indexArgLabelsScratch;
  SmallVector<SourceLoc, 4> indexArgLabelLocsScratch;
  Expr *index = packSingleArgument(ctx, lSquareLoc, indexArgs, indexArgLabels,
                                   indexArgLabelLocs, rSquareLoc,
                                   trailingClosure, implicit,
                                   indexArgLabelsScratch,
                                   indexArgLabelLocsScratch);

  size_t size = totalSizeToAlloc(indexArgLabels, indexArgLabelLocs,
                                 trailingClosure != nullptr);

  void *memory = ctx.Allocate(size, alignof(DynamicSubscriptExpr));
  return new (memory) DynamicSubscriptExpr(base, index, indexArgLabels,
                                           indexArgLabelLocs,
                                           trailingClosure != nullptr,
                                           decl, implicit);
}

UnresolvedMemberExpr::UnresolvedMemberExpr(SourceLoc dotLoc,
                                           DeclNameLoc nameLoc,
                                           DeclName name, Expr *argument,
                                           ArrayRef<Identifier> argLabels,
                                           ArrayRef<SourceLoc> argLabelLocs,
                                           bool hasTrailingClosure,
                                           bool implicit)
  : Expr(ExprKind::UnresolvedMember, implicit),
    DotLoc(dotLoc), NameLoc(nameLoc), Name(name), Argument(argument) {
  UnresolvedMemberExprBits.HasArguments = (argument != nullptr);
  UnresolvedMemberExprBits.NumArgLabels = argLabels.size();
  UnresolvedMemberExprBits.HasArgLabelLocs = !argLabelLocs.empty();
  UnresolvedMemberExprBits.HasTrailingClosure = hasTrailingClosure;
  initializeCallArguments(argLabels, argLabelLocs, hasTrailingClosure);
}

UnresolvedMemberExpr *UnresolvedMemberExpr::create(ASTContext &ctx,
                                                   SourceLoc dotLoc,
                                                   DeclNameLoc nameLoc,
                                                   DeclName name,
                                                   bool implicit) {
  size_t size = totalSizeToAlloc({ }, { }, /*hasTrailingClosure=*/false);

  void *memory = ctx.Allocate(size, alignof(UnresolvedMemberExpr));
  return new (memory) UnresolvedMemberExpr(dotLoc, nameLoc, name, nullptr,
                                           { }, { },
                                           /*hasTrailingClosure=*/false,
                                           implicit);
}

UnresolvedMemberExpr *
UnresolvedMemberExpr::create(ASTContext &ctx, SourceLoc dotLoc,
                             DeclNameLoc nameLoc, DeclName name,
                             SourceLoc lParenLoc,
                             ArrayRef<Expr *> args,
                             ArrayRef<Identifier> argLabels,
                             ArrayRef<SourceLoc> argLabelLocs,
                             SourceLoc rParenLoc,
                             Expr *trailingClosure,
                             bool implicit) {
  SmallVector<Identifier, 4> argLabelsScratch;
  SmallVector<SourceLoc, 4> argLabelLocsScratch;
  Expr *arg = packSingleArgument(ctx, lParenLoc, args, argLabels,
                                 argLabelLocs, rParenLoc,
                                 trailingClosure, implicit,
                                 argLabelsScratch,
                                 argLabelLocsScratch);

  size_t size = totalSizeToAlloc(argLabels, argLabelLocs,
                                 trailingClosure != nullptr);

  void *memory = ctx.Allocate(size, alignof(UnresolvedMemberExpr));
  return new (memory) UnresolvedMemberExpr(dotLoc, nameLoc, name, arg,
                                           argLabels, argLabelLocs,
                                           trailingClosure != nullptr,
                                           implicit);
}

ArrayRef<Identifier> ApplyExpr::getArgumentLabels(
    SmallVectorImpl<Identifier> &scratch) const {
  // Unary operators and 'self' applications have a single, unlabeled argument.
  if (isa<PrefixUnaryExpr>(this) || isa<PostfixUnaryExpr>(this) ||
      isa<SelfApplyExpr>(this)) {
    scratch.clear();
    scratch.push_back(Identifier());
    return scratch;
  }

  // Binary operators have two unlabeled arguments.
  if (isa<BinaryExpr>(this)) {
    scratch.clear();
    scratch.reserve(2);
    scratch.push_back(Identifier());
    scratch.push_back(Identifier());
    return scratch;    
  }

  // For calls, get the argument labels directly.
  auto call = cast<CallExpr>(this);
  return call->getArgumentLabels();
}

bool ApplyExpr::hasTrailingClosure() const {
  if (auto call = dyn_cast<CallExpr>(this))
    return call->hasTrailingClosure();

  return false;
}

CallExpr::CallExpr(Expr *fn, Expr *arg, bool Implicit,
                   ArrayRef<Identifier> argLabels,
                   ArrayRef<SourceLoc> argLabelLocs,
                   bool hasTrailingClosure,
                   Type ty)
    : ApplyExpr(ExprKind::Call, fn, arg, Implicit, ty)
{
  CallExprBits.NumArgLabels = argLabels.size();
  CallExprBits.HasArgLabelLocs = !argLabelLocs.empty();
  CallExprBits.HasTrailingClosure = hasTrailingClosure;
  initializeCallArguments(argLabels, argLabelLocs, hasTrailingClosure);
}

CallExpr *CallExpr::create(ASTContext &ctx, Expr *fn, Expr *arg,
                           ArrayRef<Identifier> argLabels,
                           ArrayRef<SourceLoc> argLabelLocs,
                           bool hasTrailingClosure,
                           bool implicit, Type type) {
  SmallVector<Identifier, 4> argLabelsScratch;
  SmallVector<SourceLoc, 4> argLabelLocsScratch;
  if (argLabels.empty()) {
    // Inspect the argument to dig out the argument labels, their location, and
    // whether there is a trailing closure.
    argLabels = getArgumentLabelsFromArgument(arg, argLabelsScratch,
                                              &argLabelLocsScratch,
                                              &hasTrailingClosure);
    argLabelLocs = argLabelLocsScratch;
  }

  size_t size = totalSizeToAlloc(argLabels, argLabelLocs, hasTrailingClosure);

  void *memory = ctx.Allocate(size, alignof(CallExpr));
  return new (memory) CallExpr(fn, arg, implicit, argLabels, argLabelLocs,
                               hasTrailingClosure, type);
}

CallExpr *CallExpr::create(ASTContext &ctx, Expr *fn,
                           SourceLoc lParenLoc,
                           ArrayRef<Expr *> args,
                           ArrayRef<Identifier> argLabels,
                           ArrayRef<SourceLoc> argLabelLocs,
                           SourceLoc rParenLoc,
                           Expr *trailingClosure,
                           bool implicit) {
  SmallVector<Identifier, 4> argLabelsScratch;
  SmallVector<SourceLoc, 4> argLabelLocsScratch;
  Expr *arg = packSingleArgument(ctx, lParenLoc, args, argLabels, argLabelLocs,
                                 rParenLoc, trailingClosure, implicit,
                                 argLabelsScratch, argLabelLocsScratch);

  size_t size = totalSizeToAlloc(argLabels, argLabelLocs,
                                 trailingClosure != nullptr);

  void *memory = ctx.Allocate(size, alignof(CallExpr));
  return new (memory) CallExpr(fn, arg, implicit, argLabels, argLabelLocs,
                               trailingClosure != nullptr, Type());
}

Expr *CallExpr::getDirectCallee() const {
  auto fn = getFn();
  while (true) {
    fn = fn->getSemanticsProvidingExpr();

    if (auto force = dyn_cast<ForceValueExpr>(fn)) {
      fn = force->getSubExpr();
      continue;
    }

    if (auto bind = dyn_cast<BindOptionalExpr>(fn)) {
      fn = bind->getSubExpr();
      continue;
    }

    return fn;
  }
}

RebindSelfInConstructorExpr::RebindSelfInConstructorExpr(Expr *SubExpr,
                                                         VarDecl *Self)
  : Expr(ExprKind::RebindSelfInConstructor, /*Implicit=*/true,
         TupleType::getEmpty(Self->getASTContext())),
    SubExpr(SubExpr), Self(Self)
{}

OtherConstructorDeclRefExpr *
RebindSelfInConstructorExpr::getCalledConstructor(bool &isChainToSuper) const {
  // Dig out the OtherConstructorRefExpr. Note that this is the reverse
  // of what we do in pre-checking.
  Expr *candidate = getSubExpr();
  while (true) {
    // Look through identity expressions.
    if (auto identity = dyn_cast<IdentityExpr>(candidate)) {
      candidate = identity->getSubExpr();
      continue;
    }

    // Look through force-value expressions.
    if (auto force = dyn_cast<ForceValueExpr>(candidate)) {
      candidate = force->getSubExpr();
      continue;
    }

    // Look through all try expressions.
    if (auto tryExpr = dyn_cast<AnyTryExpr>(candidate)) {
      candidate = tryExpr->getSubExpr();
      continue;
    }

    break;
  }

  // We hit an application, find the constructor reference.
  OtherConstructorDeclRefExpr *otherCtorRef;
  const ApplyExpr *apply;
  do {
    apply = cast<ApplyExpr>(candidate);
    candidate = apply->getFn();
    auto candidateUnwrapped = candidate->getSemanticsProvidingExpr();
    otherCtorRef = dyn_cast<OtherConstructorDeclRefExpr>(candidateUnwrapped);
  } while (!otherCtorRef);

  isChainToSuper = apply->getArg()->isSuperExpr();
  return otherCtorRef;
}

void AbstractClosureExpr::setParameterList(ParameterList *P) {
  parameterList = P;
  // Change the DeclContext of any parameters to be this closure.
  if (P)
    P->setDeclContextOfParamDecls(this);
}


Type AbstractClosureExpr::getResultType() const {
  if (getType()->is<ErrorType>())
    return getType();

  return getType()->castTo<FunctionType>()->getResult();
}

bool AbstractClosureExpr::isBodyThrowing() const {
  if (getType()->is<ErrorType>())
    return false;

  return getType()->castTo<FunctionType>()->getExtInfo().throws();
}

bool AbstractClosureExpr::hasSingleExpressionBody() const {
  if (auto closure = dyn_cast<ClosureExpr>(this))
    return closure->hasSingleExpressionBody();

  return true;
}

#define FORWARD_SOURCE_LOCS_TO(CLASS, NODE) \
  SourceRange CLASS::getSourceRange() const {     \
    return (NODE)->getSourceRange();              \
  }                                               \
  SourceLoc CLASS::getStartLoc() const {          \
    return (NODE)->getStartLoc();                 \
  }                                               \
  SourceLoc CLASS::getEndLoc() const {            \
    return (NODE)->getEndLoc();                   \
  }                                               \
  SourceLoc CLASS::getLoc() const {               \
    return (NODE)->getStartLoc();                 \
  }

FORWARD_SOURCE_LOCS_TO(ClosureExpr, Body.getPointer())

Expr *ClosureExpr::getSingleExpressionBody() const {
  assert(hasSingleExpressionBody() && "Not a single-expression body");
  if (!isVoidConversionClosure()) {
    return cast<ReturnStmt>(getBody()->getElement(0).get<Stmt *>())
             ->getResult();
  } else {
    return getBody()->getElement(0).get<Expr *>();
  }
}

void ClosureExpr::setSingleExpressionBody(Expr *NewBody) {
  assert(hasSingleExpressionBody() && "Not a single-expression body");
  if (!isVoidConversionClosure()) {
    cast<ReturnStmt>(getBody()->getElement(0).get<Stmt *>())
      ->setResult(NewBody);
  } else {
    return getBody()->setElement(0, NewBody);
  }
}

FORWARD_SOURCE_LOCS_TO(AutoClosureExpr, Body)

void AutoClosureExpr::setBody(Expr *E) {
  auto &Context = getASTContext();
  auto *RS = new (Context) ReturnStmt(SourceLoc(), E);
  Body = BraceStmt::create(Context, E->getStartLoc(), { RS }, E->getEndLoc());
}

Expr *AutoClosureExpr::getSingleExpressionBody() const {
  return cast<ReturnStmt>(Body->getElement(0).get<Stmt *>())->getResult();
}

FORWARD_SOURCE_LOCS_TO(UnresolvedPatternExpr, subPattern)

TypeExpr::TypeExpr(TypeLoc TyLoc)
  : Expr(ExprKind::Type, /*implicit*/false), Info(TyLoc) {
  Type Ty = TyLoc.getType();
  if (Ty && Ty->hasCanonicalTypeComputed())
    setType(MetatypeType::get(Ty, Ty->getASTContext()));
}

TypeExpr::TypeExpr(Type Ty)
  : Expr(ExprKind::Type, /*implicit*/true), Info(TypeLoc::withoutLoc(Ty)) {
  if (Ty->hasCanonicalTypeComputed())
    setType(MetatypeType::get(Ty, Ty->getASTContext()));
}

// The type of a TypeExpr is always a metatype type.  Return the instance
// type or null if not set yet.
Type TypeExpr::getInstanceType() const {
  if (!getType() || getType()->is<ErrorType>())
    return Type();
  
  return getType()->castTo<MetatypeType>()->getInstanceType();
}


/// Return a TypeExpr for a simple identifier and the specified location.
TypeExpr *TypeExpr::createForDecl(SourceLoc Loc, TypeDecl *Decl,
                                  bool isImplicit) {
  ASTContext &C = Decl->getASTContext();
  assert(Loc.isValid());
  auto *Repr = new (C) SimpleIdentTypeRepr(Loc, Decl->getName());
  Repr->setValue(Decl);
  auto result = new (C) TypeExpr(TypeLoc(Repr, Type()));
  if (isImplicit)
    result->setImplicit();
  return result;
}

TypeExpr *TypeExpr::createForSpecializedDecl(SourceLoc Loc, TypeDecl *D,
                                             ArrayRef<TypeRepr*> args,
                                             SourceRange AngleLocs) {
  ASTContext &C = D->getASTContext();
  assert(Loc.isValid());
  auto *Repr = new (C) GenericIdentTypeRepr(Loc, D->getName(),
                                            args, AngleLocs);
  Repr->setValue(D);
  return new (C) TypeExpr(TypeLoc(Repr, Type()));
}


// Create an implicit TypeExpr, with location information even though it
// shouldn't have one.  This is presently used to work around other location
// processing bugs.  If you have an implicit location, use createImplicit.
TypeExpr *TypeExpr::createImplicitHack(SourceLoc Loc, Type Ty, ASTContext &C) {
  // FIXME: This is horrible.
  if (Loc.isInvalid()) return createImplicit(Ty, C);
  auto *Repr = new (C) FixedTypeRepr(Ty, Loc);
  auto *Res = new (C) TypeExpr(TypeLoc(Repr, Ty));
  Res->setImplicit();
  Res->setType(MetatypeType::get(Ty, C));
  return Res;
}


ArchetypeType *OpenExistentialExpr::getOpenedArchetype() const {
  auto type = getOpaqueValue()->getType()->getRValueType();
  while (auto metaTy = type->getAs<MetatypeType>())
    type = metaTy->getInstanceType();
  return type->castTo<ArchetypeType>();
}

ObjCKeyPathExpr::ObjCKeyPathExpr(SourceLoc keywordLoc, SourceLoc lParenLoc,
                         ArrayRef<Identifier> names,
                         ArrayRef<SourceLoc> nameLocs,
                         SourceLoc rParenLoc)
  : Expr(ExprKind::ObjCKeyPath, /*Implicit=*/nameLocs.empty()),
    KeywordLoc(keywordLoc), LParenLoc(lParenLoc), RParenLoc(rParenLoc)
{
  // Copy components (which are all names).
  ObjCKeyPathExprBits.NumComponents = names.size();
  for (auto idx : indices(names))
    getComponentsMutable()[idx] = names[idx];

  assert(nameLocs.empty() || nameLocs.size() == names.size());
  ObjCKeyPathExprBits.HaveSourceLocations = !nameLocs.empty();
  if (ObjCKeyPathExprBits.HaveSourceLocations) {
    memcpy(getNameLocsMutable().data(), nameLocs.data(),
           nameLocs.size() * sizeof(SourceLoc));
  }
}

Identifier ObjCKeyPathExpr::getComponentName(unsigned i) const {
  if (auto decl = getComponentDecl(i))
    return decl->getFullName().getBaseName();

  return getComponents()[i].get<Identifier>();
}

ObjCKeyPathExpr *ObjCKeyPathExpr::create(ASTContext &ctx,
                                 SourceLoc keywordLoc, SourceLoc lParenLoc,
                                 ArrayRef<Identifier> names,
                                 ArrayRef<SourceLoc> nameLocs,
                                 SourceLoc rParenLoc) {
  unsigned size = sizeof(ObjCKeyPathExpr)
    + names.size() * sizeof(Identifier)
    + nameLocs.size() * sizeof(SourceLoc);
  void *mem = ctx.Allocate(size, alignof(ObjCKeyPathExpr));
  return new (mem) ObjCKeyPathExpr(keywordLoc, lParenLoc, names, nameLocs,
                                   rParenLoc);
}
