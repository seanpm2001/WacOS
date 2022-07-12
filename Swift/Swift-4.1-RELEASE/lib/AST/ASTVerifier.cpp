//===--- Verifier.cpp - AST Invariant Verification ------------------------===//
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
//  This file implements a verifier of AST invariants.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/ASTContext.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/AccessScope.h"
#include "swift/AST/Decl.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/AST/Expr.h"
#include "swift/AST/ForeignErrorConvention.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/Module.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/PrettyStackTrace.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/Stmt.h"
#include "swift/Basic/SourceManager.h"
#include "swift/Subsystems.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <type_traits>
using namespace swift;

namespace {

template<typename T>
struct ASTNodeBase {};

#define EXPR(ID, PARENT) \
    template<> \
    struct ASTNodeBase<ID ## Expr *> { \
      typedef PARENT BaseTy; \
    };
#define ABSTRACT_EXPR(ID, PARENT) EXPR(ID, PARENT)
#include "swift/AST/ExprNodes.def"

#define STMT(ID, PARENT) \
    template<> \
    struct ASTNodeBase<ID ## Stmt *> { \
      typedef PARENT BaseTy; \
    };
#include "swift/AST/StmtNodes.def"

#define DECL(ID, PARENT) \
    template<> \
    struct ASTNodeBase<ID ## Decl *> { \
      typedef PARENT BaseTy; \
    };
#define ABSTRACT_DECL(ID, PARENT) DECL(ID, PARENT)
#include "swift/AST/DeclNodes.def"

#define PATTERN(ID, PARENT) \
    template<> \
    struct ASTNodeBase<ID ## Pattern *> { \
      typedef PARENT BaseTy; \
    };
#include "swift/AST/PatternNodes.def"

template <typename Ty>
struct is_apply_expr
    : public std::integral_constant<
          bool,
          std::is_same<Ty, CallExpr>::value ||
              std::is_same<Ty, PrefixUnaryExpr>::value ||
              std::is_same<Ty, PostfixUnaryExpr>::value ||
              std::is_same<Ty, BinaryExpr>::value ||
              std::is_same<Ty, DotSyntaxCallExpr>::value ||
              std::is_same<Ty, ConstructorRefCallExpr>::value> {};

template <typename Ty>
struct is_autoclosure_expr
    : public std::integral_constant<bool,
                                    std::is_same<Ty, AutoClosureExpr>::value> {
};

template <typename Ty>
struct is_apply_or_autoclosure_expr
    : public std::integral_constant<
          bool, is_apply_expr<Ty>::value || is_autoclosure_expr<Ty>::value> {};

template <typename Verifier, typename Kind>
std::pair<bool, Expr *> dispatchVisitPreExprHelper(
    Verifier &V,
    typename std::enable_if<
        is_apply_expr<typename std::remove_pointer<Kind>::type>::value,
        Kind>::type node) {
  if (V.shouldVerify(node)) {
    // Record any inout_to_pointer or array_to_pointer that we see in
    // the proper position.
    V.maybeRecordValidPointerConversion(node, node->getArg());
    return {true, node};
  }
  V.cleanup(node);
  return {false, node};
}

template <typename Verifier, typename Kind>
std::pair<bool, Expr *> dispatchVisitPreExprHelper(
    Verifier &V,
    typename std::enable_if<
        is_autoclosure_expr<typename std::remove_pointer<Kind>::type>::value,
        Kind>::type node) {
  if (V.shouldVerify(node)) {
    // Record any inout_to_pointer or array_to_pointer that we see in
    // the proper position.
    V.maybeRecordValidPointerConversion(node, node->getSingleExpressionBody());
    return {true, node};
  }
  V.cleanup(node);
  return {false, node};
}

template <typename Verifier, typename Kind>
std::pair<bool, Expr *> dispatchVisitPreExprHelper(
    Verifier &V, typename std::enable_if<
                     !is_apply_or_autoclosure_expr<
                         typename std::remove_pointer<Kind>::type>::value,
                     Kind>::type node) {
  if (V.shouldVerify(node)) {
    return {true, node};
  }
  V.cleanup(node);
  return {false, node};
}

/// Describes a generic environment that might be lazily deserialized.
///
/// This class abstracts over a declaration context that may have a generic
/// environment, ensuring that we don't deserialize the environment.
struct LazyGenericEnvironment {
  llvm::PointerUnion<DeclContext *, GenericEnvironment *> storage;

  explicit operator bool() const {
    if (storage.dyn_cast<GenericEnvironment *>())
      return true;

    if (auto dc = storage.dyn_cast<DeclContext *>())
      return dc->getGenericSignatureOfContext();

    return false;
  }

  bool isLazy() const {
    if (auto dc = storage.dyn_cast<DeclContext *>())
      return dc->contextHasLazyGenericEnvironment();

    return false;
  }

  bool containsPrimaryArchetype(ArchetypeType *archetype) const {
    // Assume true so we don't deserialize.
    if (isLazy()) return true;

    if (auto genericEnv = storage.dyn_cast<GenericEnvironment *>())
      return archetype->getGenericEnvironment() == genericEnv;

    if (auto dc = storage.dyn_cast<DeclContext *>()) {
      if (auto genericEnv = dc->getGenericEnvironmentOfContext())
        return archetype->getGenericEnvironment() == genericEnv;
    }

    return false;
  }
};

class Verifier : public ASTWalker {
  PointerUnion<ModuleDecl *, SourceFile *> M;
  ASTContext &Ctx;
  llvm::raw_ostream &Out;
  const bool HadError;
  SmallVector<bool, 8> InImplicitBraceStmt;

  /// \brief The stack of functions we're visiting.
  SmallVector<DeclContext *, 4> Functions;

  /// \brief The stack of scopes we're visiting.
  using ScopeLike = llvm::PointerUnion<DeclContext *, BraceStmt *>;
  SmallVector<ScopeLike, 4> Scopes;

  /// The stack of generic environments.
  SmallVector<LazyGenericEnvironment, 2> GenericEnv;

  /// \brief The stack of optional evaluations active at this point.
  SmallVector<OptionalEvaluationExpr *, 4> OptionalEvaluations;

  /// \brief The set of opaque value expressions active at this point.
  llvm::DenseMap<OpaqueValueExpr *, unsigned> OpaqueValues;

  /// The set of opened existential archetypes that are currently
  /// active.
  llvm::DenseSet<ArchetypeType *> OpenedExistentialArchetypes;

  /// The set of inout to pointer expr that match the following pattern:
  ///
  /// (call-expr
  ///    (brace-stmt
  ///      ... maybe other arguments ...
  ///      (inject_into_optional
  ///        (inout_to_pointer ...))
  ///      ... maybe other arguments ...))
  ///
  /// Any other inout to pointer expr that we see is invalid and the verifier
  /// will assert.
  llvm::DenseSet<InOutToPointerExpr *> ValidInOutToPointerExprs;
  llvm::DenseSet<ArrayToPointerExpr *> ValidArrayToPointerExprs;

  /// A key into ClosureDiscriminators is a combination of a
  /// ("canonicalized") local DeclContext* and a flag for whether to
  /// use the explicit closure sequence (false) or the implicit
  /// closure sequence (true).
  typedef llvm::PointerIntPair<DeclContext *, 1, bool> ClosureDiscriminatorKey;
  llvm::DenseMap<ClosureDiscriminatorKey, llvm::SmallBitVector>
      ClosureDiscriminators;
  DeclContext *CanonicalTopLevelContext = nullptr;

  Verifier(PointerUnion<ModuleDecl *, SourceFile *> M, DeclContext *DC)
      : M(M),
        Ctx(M.is<ModuleDecl *>() ? M.get<ModuleDecl *>()->getASTContext()
                                 : M.get<SourceFile *>()->getASTContext()),
        Out(llvm::errs()), HadError(Ctx.hadError()) {
    Scopes.push_back(DC);
    GenericEnv.push_back({DC});
  }

public:
  Verifier(ModuleDecl *M, DeclContext *DC)
      : Verifier(PointerUnion<ModuleDecl *, SourceFile *>(M), DC) {}
  Verifier(SourceFile &SF, DeclContext *DC) : Verifier(&SF, DC) {}

  static Verifier forDecl(const Decl *D) {
    DeclContext *DC = D->getDeclContext();
    DeclContext *topDC = DC->getModuleScopeContext();
    if (auto SF = dyn_cast<SourceFile>(topDC))
      return Verifier(*SF, DC);
    return Verifier(topDC->getParentModule(), DC);
  }

  std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
    switch (E->getKind()) {
#define DISPATCH(ID) return dispatchVisitPreExpr(static_cast<ID##Expr*>(E))
#define EXPR(ID, PARENT) \
      case ExprKind::ID: \
        DISPATCH(ID);
#define UNCHECKED_EXPR(ID, PARENT) \
      case ExprKind::ID: \
        assert((HadError || !M.is<SourceFile*>() || \
                M.get<SourceFile*>()->ASTStage < SourceFile::TypeChecked) && \
               #ID "in wrong phase");\
        DISPATCH(ID);
#include "swift/AST/ExprNodes.def"
#undef DISPATCH
      }
      llvm_unreachable("not all cases handled!");
    }

    Expr *walkToExprPost(Expr *E) override {
      switch (E->getKind()) {
#define DISPATCH(ID) return dispatchVisitPost(static_cast<ID##Expr*>(E))
#define EXPR(ID, PARENT) \
      case ExprKind::ID: \
        DISPATCH(ID);
#define UNCHECKED_EXPR(ID, PARENT) \
      case ExprKind::ID: \
        assert((HadError || !M.is<SourceFile*>() || \
                M.get<SourceFile*>()->ASTStage < SourceFile::TypeChecked) && \
               #ID "in wrong phase");\
        DISPATCH(ID);
#include "swift/AST/ExprNodes.def"
#undef DISPATCH
      }
      llvm_unreachable("not all cases handled!");
    }

    std::pair<bool, Stmt *> walkToStmtPre(Stmt *S) override {
      switch (S->getKind()) {
#define DISPATCH(ID) return dispatchVisitPreStmt(static_cast<ID##Stmt*>(S))
#define STMT(ID, PARENT) \
      case StmtKind::ID: \
        DISPATCH(ID);
#include "swift/AST/StmtNodes.def"
#undef DISPATCH
      }
      llvm_unreachable("not all cases handled!");
    }

    Stmt *walkToStmtPost(Stmt *S) override {
      switch (S->getKind()) {
#define DISPATCH(ID) return dispatchVisitPost(static_cast<ID##Stmt*>(S))
#define STMT(ID, PARENT) \
      case StmtKind::ID: \
        DISPATCH(ID);
#include "swift/AST/StmtNodes.def"
#undef DISPATCH
      }
      llvm_unreachable("not all cases handled!");
    }

    std::pair<bool, Pattern*> walkToPatternPre(Pattern *P) override {
      switch (P->getKind()) {
#define DISPATCH(ID) \
        return dispatchVisitPrePattern(static_cast<ID##Pattern*>(P))
#define PATTERN(ID, PARENT) \
      case PatternKind::ID: \
        DISPATCH(ID);
#include "swift/AST/PatternNodes.def"
#undef DISPATCH
      }
      llvm_unreachable("not all cases handled!");
    }

    Pattern *walkToPatternPost(Pattern *P) override {
      switch (P->getKind()) {
#define DISPATCH(ID) \
        return dispatchVisitPost(static_cast<ID##Pattern*>(P))
#define PATTERN(ID, PARENT) \
      case PatternKind::ID: \
        DISPATCH(ID);
#include "swift/AST/PatternNodes.def"
#undef DISPATCH
      }
      llvm_unreachable("not all cases handled!");
    }

    bool walkToDeclPre(Decl *D) override {
      switch (D->getKind()) {
#define DISPATCH(ID) return dispatchVisitPre(static_cast<ID##Decl*>(D))
#define DECL(ID, PARENT) \
      case DeclKind::ID: \
        DISPATCH(ID);
#include "swift/AST/DeclNodes.def"
#undef DISPATCH
      }
      llvm_unreachable("not all cases handled!");
    }

    bool walkToDeclPost(Decl *D) override {
      switch (D->getKind()) {
#define DISPATCH(ID) return dispatchVisitPost(static_cast<ID##Decl*>(D))
#define DECL(ID, PARENT) \
      case DeclKind::ID: \
        DISPATCH(ID);
#include "swift/AST/DeclNodes.def"
#undef DISPATCH
      }

      llvm_unreachable("Unhandled declaration kind");
    }

    /// Helper template for dispatching pre-visitation.
    /// If we're visiting in pre-order, don't validate the node yet;
    /// just check whether we should stop further descent.
    template <class T> bool dispatchVisitPre(T node) {
      if (shouldVerify(node))
        return true;
      cleanup(node);
      return false;
    }

    /// Helper template for dispatching pre-visitation.
    ///
    /// If we're visiting in pre-order, don't validate the node yet;
    /// just check whether we should stop further descent.
    template <class T> std::pair<bool, Expr *> dispatchVisitPreExpr(T node) {
      return dispatchVisitPreExprHelper<Verifier, T>(*this, node);
    }

    /// Helper template for dispatching pre-visitation.
    /// If we're visiting in pre-order, don't validate the node yet;
    /// just check whether we should stop further descent.
    template <class T> std::pair<bool, Stmt *> dispatchVisitPreStmt(T node) {
      if (shouldVerify(node))
        return { true, node };
      cleanup(node);
      return { false, node };
    }

    /// Helper template for dispatching pre-visitation.
    /// If we're visiting in pre-order, don't validate the node yet;
    /// just check whether we should stop further descent.
    template <class T>
    std::pair<bool, Pattern *> dispatchVisitPrePattern(T node) {
      if (shouldVerify(node))
        return { true, node };
      cleanup(node);
      return { false, node };
    }

    /// Helper template for dispatching post-visitation.
    template <class T> T dispatchVisitPost(T node) {
      // Verify source ranges if the AST node was parsed from source.
      auto *SF = M.dyn_cast<SourceFile *>();
      if (SF) {
        // If we are inside an implicit BraceStmt, don't verify source
        // locations.  LLDB creates implicit BraceStmts which contain a mix of
        // generated/user-written code.
        if (InImplicitBraceStmt.empty() || !InImplicitBraceStmt.back())
          checkSourceRanges(node);
      }

      // Check that nodes marked invalid have the correct type.
      checkErrors(node);

      // Always verify the node as a parsed node.
      verifyParsed(node);

      // If we've bound names already, verify as a bound node.
      if (!SF || SF->ASTStage >= SourceFile::NameBound)
        verifyBound(node);

      // If we've checked types already, do some extra verification.
      if (!SF || SF->ASTStage >= SourceFile::TypeChecked) {
        verifyCheckedAlways(node);
        if (!HadError && shouldVerifyChecked(node))
          verifyChecked(node);
      }

      // Clean up anything that we've placed into a stack to check.
      cleanup(node);

      // Always continue.
      return node;
    }

    // Default cases for whether we should verify within the given subtree.
    bool shouldVerify(Expr *E) { return true; }
    bool shouldVerify(Stmt *S) { return true; }
    bool shouldVerify(Pattern *S) { return true; }
    bool shouldVerify(Decl *S) { return true; }

    // Default cases for whether we should verify a checked subtree.
    bool shouldVerifyChecked(Expr *E) {
      if (!E->getType()) {
        // For @objc enums, we serialize the pre-type-checked integer
        // literal raw values, and thus when they are deserialized
        // they do not have a type on them.
        if (!isa<IntegerLiteralExpr>(E)) {
          Out << "expression has no type\n";
          E->print(Out);
          abort();
        }
      }
      return true;
    }
    bool shouldVerifyChecked(Stmt *S) { return true; }
    bool shouldVerifyChecked(Pattern *S) { return S->hasType(); }
    bool shouldVerifyChecked(Decl *S) { return true; }

    // Only verify functions if they have bodies we can safely walk.
    // FIXME: This is a bit of a hack; we should be able to check the
    // invariants of a parsed body as well.
    bool shouldVerify(AbstractFunctionDecl *afd) {
      switch (afd->getBodyKind()) {
      case AbstractFunctionDecl::BodyKind::None:
      case AbstractFunctionDecl::BodyKind::TypeChecked:
      case AbstractFunctionDecl::BodyKind::Skipped:
      case AbstractFunctionDecl::BodyKind::MemberwiseInitializer:
        return true;

      case AbstractFunctionDecl::BodyKind::Unparsed:
      case AbstractFunctionDecl::BodyKind::Parsed:
      case AbstractFunctionDecl::BodyKind::Synthesize:
        if (auto SF = dyn_cast<SourceFile>(afd->getModuleScopeContext())) {
          return SF->ASTStage < SourceFile::TypeChecked;
        }

        return false;
      }
    }

    // Default cases for cleaning up as we exit a node.
    void cleanup(Expr *E) { }
    void cleanup(Stmt *S) { }
    void cleanup(Pattern *P) { }
    void cleanup(Decl *D) { }
    
    // Base cases for the various stages of verification.
    void verifyParsed(Expr *E) {}
    void verifyParsed(Stmt *S) {}
    void verifyParsed(Pattern *P) {}
    void verifyParsed(Decl *D) {
      if (!D->getDeclContext()) {
        Out << "every Decl should have a DeclContext";
        PrettyStackTraceDecl debugStack("verifying DeclContext", D);
        abort();
      }
    }
    template<typename T>
    void verifyParsedBase(T ASTNode) {
      verifyParsed(cast<typename ASTNodeBase<T>::BaseTy>(ASTNode));
    }

    void verifyBound(Expr *E) {}
    void verifyBound(Stmt *S) {}
    void verifyBound(Pattern *P) {}
    void verifyBound(Decl *D) {}

    /// @{
    /// These verification functions are always run on type checked ASTs
    /// (even if there were errors).
    void verifyCheckedAlways(Expr *E) {
      if (E->getType())
        verifyChecked(E->getType());
    }
    void verifyCheckedAlways(Stmt *S) {}
    void verifyCheckedAlways(Pattern *P) {
      if (P->hasType() && !P->getDelayedInterfaceType())
        verifyChecked(P->getType());
    }
    void verifyCheckedAlways(Decl *D) {
    }

    template<typename T>
    void verifyCheckedAlwaysBase(T ASTNode) {
      verifyCheckedAlways(cast<typename ASTNodeBase<T>::BaseTy>(ASTNode));
    }
    /// @}

    /// @{
    /// These verification functions are run on type checked ASTs if there were
    /// no errors.
    void verifyChecked(Expr *E) {
      // Some imported expressions don't have types, even in checked mode.
      // TODO: eliminate all these
      if (!E->getType()) {
        // For @objc enums, we serialize the pre-type-checked integer
        // literal raw values, and thus when they are deserialized
        // they do not have a type on them.
        if (!isa<IntegerLiteralExpr>(E)) {
          Out << "expression has no type\n";
          E->print(Out);
          abort();
        }
        return;
      }

      // Require an access kind to be set on every l-value expression.
      // Note that the empty tuple type is assignable but usually isn't
      // an l-value, so we have to be conservative there.
      if (E->getType()->hasLValueType() != E->hasLValueAccessKind() &&
          !(E->hasLValueAccessKind() && E->getType()->isAssignableType())) {
        Out << "l-value expression does not have l-value access kind set\n";
        E->print(Out);
        abort();
      }
    }
    void verifyChecked(Stmt *S) {}
    void verifyChecked(Pattern *P) { }
    void verifyChecked(Decl *D) {}

    void verifyChecked(Type type) {
      llvm::SmallPtrSet<ArchetypeType *, 4> visitedArchetypes;
      verifyChecked(type, visitedArchetypes);
    }

    void verifyChecked(
           Type type,
           llvm::SmallPtrSet<ArchetypeType *, 4> &visitedArchetypes) {
      if (!type)
        return;

      // Check for type variables that escaped the type checker.
      if (type->hasTypeVariable()) {
        Out << "a type variable escaped the type checker\n";
        abort();
      }

      bool foundError = type.findIf([&](Type type) -> bool {
        if (auto archetype = type->getAs<ArchetypeType>()) {
          // Only visit each archetype once.
          if (!visitedArchetypes.insert(archetype).second)
            return false;

          // We should know about archetypes corresponding to opened
          // existential archetypes.
          if (archetype->getOpenedExistentialType()) {
            if (OpenedExistentialArchetypes.count(archetype) == 0) {
              Out << "Found opened existential archetype "
                  << archetype->getString()
                  << " outside enclosing OpenExistentialExpr\n";
              return true;
            }

            return false;
          }

          // Otherwise, the archetype needs to be from this scope.
          if (GenericEnv.empty() || !GenericEnv.back()) {
            Out << "AST verification error: archetype outside of generic "
                   "context: " << archetype->getString() << "\n";
            return true;
          }

          // Get the primary archetype.
          auto *parent = archetype->getPrimary();

          if (!GenericEnv.back().containsPrimaryArchetype(parent)) {
            Out << "AST verification error: archetype "
                << archetype->getString() << " not allowed in this context\n";

            if (auto env = parent->getGenericEnvironment()) {
              if (auto owningDC = env->getOwningDeclContext()) {
                llvm::errs() << "archetype came from:\n";
                owningDC->dumpContext();
                llvm::errs() << "\n";
              }
            }

            return true;
          }

          // Make sure that none of the nested types are dependent.
          for (const auto &nested : archetype->getKnownNestedTypes()) {
            if (!nested.second)
              continue;
            
            if (auto nestedType = nested.second) {
              if (nestedType->hasTypeParameter()) {
                Out << "Nested type " << nested.first.str()
                    << " of archetype " << archetype->getString()
                    << " is dependent type " << nestedType->getString()
                    << "\n";
                return true;
              }
            }

            verifyChecked(nested.second, visitedArchetypes);
          }
        }

        return false;
      });
      
      if (foundError)
        abort();
    }

    template<typename T>
    void verifyCheckedBase(T ASTNode) {
      verifyChecked(cast<typename ASTNodeBase<T>::BaseTy>(ASTNode));
    }
    /// @}

    // Specialized verifiers.

    void pushScope(DeclContext *scope) {
      Scopes.push_back(scope);
      GenericEnv.push_back({scope});
    }
    void pushScope(BraceStmt *scope) {
      Scopes.push_back(scope);
    }
    void popScope(DeclContext *scope) {
      assert(Scopes.back().get<DeclContext*>() == scope);
      assert(GenericEnv.back().storage.get<DeclContext *>() == scope);
      Scopes.pop_back();
      GenericEnv.pop_back();
    }
    void popScope(BraceStmt *scope) {
      assert(Scopes.back().get<BraceStmt*>() == scope);
      Scopes.pop_back();
    }

    void pushFunction(DeclContext *functionScope) {
      pushScope(functionScope);
      Functions.push_back(functionScope);
    }
    void popFunction(DeclContext *functionScope) {
      assert(Functions.back() == functionScope);
      Functions.pop_back();
      popScope(functionScope);
    }

#define FUNCTION_LIKE(NODE)                                     \
    bool shouldVerify(NODE *fn) {                               \
      pushFunction(fn);                                         \
      return shouldVerify(cast<ASTNodeBase<NODE*>::BaseTy>(fn));\
    }                                                           \
    void cleanup(NODE *fn) {                                    \
      popFunction(fn);                                          \
    }
#define SCOPE_LIKE(NODE)                                        \
    bool shouldVerify(NODE *fn) {                               \
      pushScope(fn);                                            \
      if (fn->hasLazyMembers())                                 \
        return false;                                           \
      return shouldVerify(cast<ASTNodeBase<NODE*>::BaseTy>(fn));\
    }                                                           \
    void cleanup(NODE *fn) {                                    \
      popScope(fn);                                             \
    }

    FUNCTION_LIKE(AbstractClosureExpr)
    FUNCTION_LIKE(ConstructorDecl)
    FUNCTION_LIKE(DestructorDecl)
    FUNCTION_LIKE(FuncDecl)
    SCOPE_LIKE(NominalTypeDecl)
    SCOPE_LIKE(ExtensionDecl)

#undef SCOPE_LIKE
#undef FUNCTION_LIKE

    bool shouldVerify(BraceStmt *BS) {
      pushScope(BS);
      InImplicitBraceStmt.push_back(BS->isImplicit());
      return shouldVerify(cast<Stmt>(BS));
    }

    void cleanup(BraceStmt *BS) {
      InImplicitBraceStmt.pop_back();
      popScope(BS);
    }

    bool shouldVerify(OpenExistentialExpr *expr) {
      if (!shouldVerify(cast<Expr>(expr)))
        return false;

      // In rare instances we clear the opaque value because we no
      // longer have a subexpression that references it.
      if (!expr->getOpaqueValue())
        return true;

      assert(!OpaqueValues.count(expr->getOpaqueValue()));
      OpaqueValues[expr->getOpaqueValue()] = 0;
      assert(OpenedExistentialArchetypes.count(expr->getOpenedArchetype())==0);
      OpenedExistentialArchetypes.insert(expr->getOpenedArchetype());
      return true;
    }

    void cleanup(OpenExistentialExpr *expr) {
      // In rare instances we clear the opaque value because we no
      // longer have a subexpression that references it.
      if (!expr->getOpaqueValue())
        return;

      assert(OpaqueValues.count(expr->getOpaqueValue()));
      OpaqueValues.erase(expr->getOpaqueValue());
      assert(OpenedExistentialArchetypes.count(expr->getOpenedArchetype())==1);
      OpenedExistentialArchetypes.erase(expr->getOpenedArchetype());
    }

    bool shouldVerify(MakeTemporarilyEscapableExpr *expr) {
      if (!shouldVerify(cast<Expr>(expr)))
        return false;
      
      assert(!OpaqueValues.count(expr->getOpaqueValue()));
      OpaqueValues[expr->getOpaqueValue()] = 0;
      return true;
    }
    
    void cleanup(MakeTemporarilyEscapableExpr *expr) {
      assert(OpaqueValues.count(expr->getOpaqueValue()));
      OpaqueValues.erase(expr->getOpaqueValue());
    }

    // Keep a stack of the currently-live optional evaluations.
    bool shouldVerify(OptionalEvaluationExpr *expr) {
      if (!shouldVerify(cast<Expr>(expr)))
        return false;

      OptionalEvaluations.push_back(expr);
      return true;
    }
    void cleanup(OptionalEvaluationExpr *expr) {
      assert(OptionalEvaluations.back() == expr);
      OptionalEvaluations.pop_back();
    }

    // Register the OVEs in a collection upcast.
    bool shouldVerify(CollectionUpcastConversionExpr *expr) {
      if (!shouldVerify(cast<Expr>(expr)))
        return false;

      if (auto keyConversion = expr->getKeyConversion())
        OpaqueValues[keyConversion.OrigValue] = 0;
      if (auto valueConversion = expr->getValueConversion())
        OpaqueValues[valueConversion.OrigValue] = 0;
      return true;
    }
    void cleanup(CollectionUpcastConversionExpr *expr) {
      if (auto keyConversion = expr->getKeyConversion())
        OpaqueValues.erase(keyConversion.OrigValue);
      if (auto valueConversion = expr->getValueConversion())
        OpaqueValues.erase(valueConversion.OrigValue);
    }

    /// Canonicalize the given DeclContext pointer, in terms of
    /// producing something that can be looked up in
    /// ClosureDiscriminators.
    DeclContext *getCanonicalDeclContext(DeclContext *DC) {
      // All we really need to do is use a single TopLevelCodeDecl.
      if (auto topLevel = dyn_cast<TopLevelCodeDecl>(DC)) {
        if (!CanonicalTopLevelContext)
          CanonicalTopLevelContext = topLevel;
        return CanonicalTopLevelContext;
      }

      // TODO: check for uniqueness of initializer contexts?

      return DC;
    }

    /// Return the appropriate discriminator set for a closure expression.
    llvm::SmallBitVector &getClosureDiscriminators(AbstractClosureExpr *closure) {
      auto dc = getCanonicalDeclContext(closure->getParent());
      bool isAutoClosure = isa<AutoClosureExpr>(closure);
      return ClosureDiscriminators[ClosureDiscriminatorKey(dc, isAutoClosure)];
    }

    void verifyCheckedAlways(ValueDecl *D) {
      if (D->hasInterfaceType())
        verifyChecked(D->getInterfaceType());

      if (D->hasAccess()) {
        PrettyStackTraceDecl debugStack("verifying access", D);
        if (D->getFormalAccessScope().isPublic() &&
            D->getFormalAccess() < AccessLevel::Public) {
          Out << "non-public decl has no formal access scope\n";
          D->dump(Out);
          abort();
        }
        if (D->getEffectiveAccess() == AccessLevel::Private) {
          Out << "effective access should use 'fileprivate' for 'private'\n";
          D->dump(Out);
          abort();
        }
      }

      if (auto Overridden = D->getOverriddenDecl()) {
        if (D->getDeclContext() == Overridden->getDeclContext()) {
          PrettyStackTraceDecl debugStack("verifying overridden", D);
          Out << "cannot override a decl in the same DeclContext";
          D->dump(Out);
          Overridden->dump(Out);
          abort();
        }
      }
      
      if (D->didEarlyAttrValidation() &&
          D->getAttrs().hasAttribute<OverrideAttr>()) {
        if (!D->isInvalid() && D->hasInterfaceType() &&
            !isa<ClassDecl>(D->getDeclContext()) &&
            !isa<ExtensionDecl>(D->getDeclContext())) {
          PrettyStackTraceDecl debugStack("verifying override", D);
          Out << "'override' attribute outside of a class\n";
          D->dump(Out);
          abort();
        }
      }

      
      verifyCheckedAlwaysBase(D);
    }

    void verifyCheckedAlways(NominalTypeDecl *D) {
      verifyCheckedAlwaysBase(D);
    }

    bool shouldVerifyChecked(ThrowStmt *S) {
      return shouldVerifyChecked(S->getSubExpr());
    }

    void verifyChecked(ThrowStmt *S) {
      checkSameType(S->getSubExpr()->getType(),
                    checkExceptionTypeExists("throw expression"),
                    "throw operand");
      verifyCheckedBase(S);
    }

    bool shouldVerifyChecked(CatchStmt *S) {
      return shouldVerifyChecked(S->getErrorPattern());
    }

    void verifyChecked(CatchStmt *S) {
      checkSameType(S->getErrorPattern()->getType(),
                    checkExceptionTypeExists("catch statement"),
                    "catch pattern");
      verifyCheckedBase(S);
    }

    bool shouldVerifyChecked(ReturnStmt *S) {
      return !S->hasResult() || shouldVerifyChecked(S->getResult());
    }

    void verifyChecked(ReturnStmt *S) {
      auto func = Functions.back();
      Type resultType;
      if (auto *FD = dyn_cast<FuncDecl>(func)) {
        resultType = FD->getResultInterfaceType();
        resultType = FD->mapTypeIntoContext(resultType);
      } else if (auto closure = dyn_cast<AbstractClosureExpr>(func)) {
        resultType = closure->getResultType();
      } else {
        resultType = TupleType::getEmpty(Ctx);
      }
      
      if (S->hasResult()) {
        auto result = S->getResult();
        auto returnType = result->getType();
        // Make sure that the return has the same type as the function.
        checkSameType(resultType, returnType, "return type");
      } else {
        // Make sure that the function has a Void result type.
        checkSameType(resultType, TupleType::getEmpty(Ctx), "return type");
      }

      verifyCheckedBase(S);
    }
    void verifyChecked(DeferStmt *S) {
      auto FT = S->getTempDecl()->getInterfaceType()->castTo<AnyFunctionType>();
      assert(FT->isNoEscape() && "Defer statements must not escape");
      verifyCheckedBase(S);
    }

    void verifyChecked(FailStmt *S) {
      // Dig out the initializer we're in (if we are).
      ConstructorDecl *ctor = nullptr;
      if (!Functions.empty()) {
        ctor = dyn_cast<ConstructorDecl>(Functions.back());
      }

      // Fail statements are only permitted in initializers.
      if (!ctor) {
        Out << "'fail' statement outside of initializer\n";
        abort();
      }

      if (ctor->getFailability() == OTK_None && !ctor->isInvalid()) {
        Out << "non-failable initializer contains a 'fail' statement\n";
        ctor->dump(Out);
        abort();
      }
    }

    void checkConditionElement(const StmtConditionElement &elt) {
      switch (elt.getKind()) {
      case StmtConditionElement::CK_Availability: break;
      case StmtConditionElement::CK_Boolean: {
        auto *E = elt.getBoolean();
        if (shouldVerifyChecked(E))
          checkSameType(E->getType(), BuiltinIntegerType::get(1, Ctx),
                        "condition type");
        break;
      }

      case StmtConditionElement::CK_PatternBinding:
        if (shouldVerifyChecked(elt.getPattern()) &&
            shouldVerifyChecked(elt.getInitializer())) {
          checkSameType(elt.getPattern()->getType(),
                    elt.getInitializer()->getType(),
                    "conditional binding type");
        }
        break;
      }
    }
    
    void checkCondition(StmtCondition C) {
      for (auto elt : C)
        checkConditionElement(elt);
    }
    
    void verifyChecked(IfStmt *S) {
      checkCondition(S->getCond());
      verifyCheckedBase(S);
    }

    void verifyChecked(GuardStmt *S) {
      checkCondition(S->getCond());
      verifyCheckedBase(S);
    }

    void verifyChecked(WhileStmt *S) {
      checkCondition(S->getCond());
      verifyCheckedBase(S);
    }

    Type checkAssignDest(Expr *Dest) {
      if (auto *TE = dyn_cast<TupleExpr>(Dest)) {
        SmallVector<TupleTypeElt, 4> lhsTupleTypes;
        for (unsigned i = 0; i != TE->getNumElements(); ++i) {
          Type SubType = checkAssignDest(TE->getElement(i));
          lhsTupleTypes.push_back(TupleTypeElt(SubType, TE->getElementName(i)));
        }
        return TupleType::get(lhsTupleTypes, Ctx);
      }
      return checkLValue(Dest->getType(), "LHS of assignment");
    }

    void verifyChecked(DeclRefExpr *E) {
      if (E->getType()->is<InOutType>()) {
        PrettyStackTraceExpr debugStack(Ctx, "verifying decl reference", E);
        Out << "reference with inout type "
          << E->getType().getString() << "\n";
        E->dump(Out);
        abort();
      }
      if (E->getType()->is<GenericFunctionType>()) {
        PrettyStackTraceExpr debugStack(Ctx, "verifying decl reference", E);
        Out << "unspecialized reference with polymorphic type "
          << E->getType().getString() << "\n";
        E->dump(Out);
        abort();
      }
      verifyCheckedBase(E);
    }

    void verifyChecked(AssignExpr *S) {
      Type lhsTy = checkAssignDest(S->getDest());
      checkSameType(lhsTy, S->getSrc()->getType(), "assignment operands");
      verifyCheckedBase(S);
    }
    
    void verifyChecked(EnumIsCaseExpr *E) {
      auto nom = E->getSubExpr()->getType()->getAnyNominal();
      if (!nom || !isa<EnumDecl>(nom)) {
        Out << "enum_is_decl operand is not an enum: ";
        E->getSubExpr()->getType().print(Out);
        Out << '\n';
        abort();
      }
      
      if (nom != E->getEnumElement()->getParentEnum()) {
        Out << "enum_is_decl case is not member of enum:\n";
        Out << "  case: ";
        E->getEnumElement()->print(Out);
        Out << "\n  type: ";
        E->getSubExpr()->getType().print(Out);
        Out << '\n';
        abort();
      }
    }

    void verifyChecked(TupleExpr *E) {
      const TupleType *exprTy = E->getType()->castTo<TupleType>();
      for_each(exprTy->getElements().begin(), exprTy->getElements().end(),
               E->getElements().begin(),
               [this](const TupleTypeElt &field, const Expr *elt) {
        if (!field.getType()->isEqual(elt->getType())) {
          Out << "tuple_expr element type mismatch:\n";
          Out << "  field: ";
          Out << field.getType() << "\n";
          Out << "  element: ";
          Out << elt->getType() << "\n";
          abort();
        }
      });
      // FIXME: Check all the variadic elements.
      verifyCheckedBase(E);
    }

    void verifyChecked(InOutExpr *E) {
      Type srcObj = checkLValue(E->getSubExpr()->getType(),
                                "result of InOutExpr");
      auto DestTy = E->getType()->castTo<InOutType>()->getObjectType();

      // HACK: Allow differences in optionality of the source and
      // result types. When IUO is gone from the type system we'll no
      // longer need this.
      auto srcOptObjTy = srcObj->getAnyOptionalObjectType();
      auto dstOptObjTy = DestTy->getAnyOptionalObjectType();
      if (srcOptObjTy && dstOptObjTy) {
        checkSameType(srcOptObjTy, dstOptObjTy, "object types for InOutExpr");
      } else {
        checkSameType(DestTy, srcObj, "object types for InOutExpr");
      }
      verifyCheckedBase(E);
    }

    void verifyParsed(AbstractClosureExpr *E) {
      Type Ty = E->getType();
      if (!Ty)
        return;
      if (Ty->hasError())
        return;
      if (!Ty->is<FunctionType>()) {
        PrettyStackTraceExpr debugStack(Ctx, "verifying closure", E);
        Out << "a closure should have a function type";
        E->print(Out);
        Out << "\n";
        abort();
      }
      verifyParsedBase(E);
    }

    void verifyChecked(AbstractClosureExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying closure", E);

      assert(Scopes.back().get<DeclContext*>() == E);
      assert(E->getParent()->isLocalContext() &&
             "closure expression was not in local context!");

      // Check that the discriminator is unique in its context.
      auto &discriminatorSet = getClosureDiscriminators(E);
      unsigned discriminator = E->getDiscriminator();
      if (discriminator >= discriminatorSet.size()) {
        discriminatorSet.resize(discriminator+1);
        discriminatorSet.set(discriminator);
      } else if (discriminatorSet.test(discriminator)) {
        Out << "a closure must have a unique discriminator in its context\n";
        E->print(Out);
        Out << "\n";
        abort();
      } else {
        discriminatorSet.set(discriminator);
      }

      // If the enclosing scope is a DC directly, rather than a local scope,
      // then the closure should be parented by an Initializer.  Otherwise,
      // it should be parented by the innermost function.
      auto enclosingScope = Scopes[Scopes.size() - 2];
      auto enclosingDC = enclosingScope.dyn_cast<DeclContext*>();
      if (enclosingDC && !isa<AbstractClosureExpr>(enclosingDC)
          && !(isa<SourceFile>(enclosingDC)
               && cast<SourceFile>(enclosingDC)->Kind == SourceFileKind::REPL)){
        auto parentDC = E->getParent();
        if (!isa<Initializer>(parentDC)) {
          Out << "a closure in non-local context should be parented "
                 "by an initializer or REPL context";
          E->print(Out);
          Out << "\n";
          abort();
        } else if (parentDC->getParent() != enclosingDC) {
          Out << "closure in non-local context not grandparented by its "
                 "enclosing function";
          E->print(Out);
          Out << "\n";
          abort();
        }
      } else if (Functions.size() >= 2 &&
                 Functions[Functions.size() - 2] != E->getParent()) {
        Out << "closure in local context not parented by its "
               "enclosing function";
        E->print(Out);
        Out << "\n";
        abort();
      }

      if (E->getDiscriminator() == AbstractClosureExpr::InvalidDiscriminator) {
        Out << "a closure expression should have a valid discriminator\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
    }

    void verifyChecked(MetatypeConversionExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying MetatypeConversion", E);

      auto destTy = checkMetatypeType(E->getType(),
                                      "result of MetatypeConversionExpr");
      auto srcTy = checkMetatypeType(E->getSubExpr()->getType(),
                                     "source of MetatypeConversionExpr");

      if (destTy->isEqual(srcTy)) {
        Out << "trivial MetatypeConversionExpr:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }

      checkTrivialSubtype(srcTy, destTy, "MetatypeConversionExpr");
      verifyCheckedBase(E);
    }
    
    void verifyChecked(ClassMetatypeToObjectExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying ClassMetatypeToObject", E);
      
      auto srcTy = checkMetatypeType(E->getSubExpr()->getType(),
                                     "source of ClassMetatypeToObject");
      
      if (!srcTy->mayHaveSuperclass()) {
        Out << "ClassMetatypeToObject with non-class metatype:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
      
      if (!E->getType()->isEqual(Ctx.getAnyObjectType())) {
        Out << "ClassMetatypeToObject does not produce AnyObject:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
    }
    
    void verifyChecked(ExistentialMetatypeToObjectExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx,
                                    "verifying ExistentialMetatypeToObject", E);
      
      auto srcTy = checkMetatypeType(E->getSubExpr()->getType(),
                                     "source of ExistentialMetatypeToObject");
      
      if (!E->getSubExpr()->getType()->is<ExistentialMetatypeType>()) {
        Out << "ExistentialMetatypeToObject with non-existential "
               "metatype:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
      if (!srcTy->isClassExistentialType()) {
        Out << "ExistentialMetatypeToObject with non-class existential "
               "metatype:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
      
      if (!E->getType()->isEqual(Ctx.getAnyObjectType())) {
        Out << "ExistentialMetatypeToObject does not produce AnyObject:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
    }
    
    void verifyChecked(ProtocolMetatypeToObjectExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx,
                                    "verifying ProtocolMetatypeToObject", E);
      
      auto srcTy = checkMetatypeType(E->getSubExpr()->getType(),
                                     "source of ProtocolMetatypeToObject");
      if (E->getSubExpr()->getType()->is<ExistentialMetatypeType>()) {
        Out << "ProtocolMetatypeToObject with existential "
               "metatype:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }

      if (!srcTy->isExistentialType()) {
        Out << "ProtocolMetatypeToObject with non-existential metatype:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }

      auto layout = srcTy->getExistentialLayout();
      if (layout.superclass ||
          !layout.isObjC() ||
          layout.getProtocols().size() != 1) {
        Out << "ProtocolMetatypeToObject with non-ObjC-protocol metatype:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }

      if (!E->getType()->getClassOrBoundGenericClass()) {
        Out << "ProtocolMetatypeToObject does not produce class:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
    }
    
    void verifyChecked(PointerToPointerExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx,
                                      "verifying PointerToPointer", E);

      auto fromElement = E->getSubExpr()->getType()->getAnyPointerElementType();
      auto toElement = E->getType()->getAnyPointerElementType();
      
      if (!fromElement || !toElement) {
        Out << "PointerToPointer does not convert between pointer types:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
    }
    
    void verifyChecked(InOutToPointerExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx,
                                      "verifying InOutToPointer", E);

      if (!ValidInOutToPointerExprs.count(E)) {
        Out << "InOutToPointerExpr in unexpected position!\n";
        E->print(Out);
        Out << "\n";
        abort();
      }

      auto fromElement = E->getSubExpr()->getType()->getInOutObjectType();
      auto toElement = E->getType()->getAnyPointerElementType();
      
      if (!E->getSubExpr()->getType()->is<InOutType>() && !toElement) {
        Out << "InOutToPointer does not convert from inout to pointer:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
      
      // Ensure we don't convert an array to a void pointer this way.
      
      if (fromElement->getNominalOrBoundGenericNominal() == Ctx.getArrayDecl()
          && toElement->isEqual(Ctx.TheEmptyTupleType)) {
        Out << "InOutToPointer is converting an array to a void pointer; "
               "ArrayToPointer should be used instead:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
    }
    
    void verifyChecked(ArrayToPointerExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx,
                                      "verifying ArrayToPointer", E);

      if (!ValidArrayToPointerExprs.count(E)) {
        Out << "ArrayToPointer in invalid position?!\n";
        E->print(Out);
        Out << "\n";
        abort();
      }

      // The source may be optionally inout.
      auto fromArray = E->getSubExpr()->getType()->getInOutObjectType();
      
      if (fromArray->getNominalOrBoundGenericNominal() != Ctx.getArrayDecl()) {
        Out << "ArrayToPointer does not convert from array:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
      
      auto toElement = E->getType()->getAnyPointerElementType();

      if (!toElement) {
        Out << "ArrayToPointer does not convert to pointer:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
    }
    
    void verifyChecked(StringToPointerExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx,
                                      "verifying StringToPointer", E);
      
      if (E->getSubExpr()->getType()->getNominalOrBoundGenericNominal()
            != Ctx.getStringDecl()) {
        Out << "StringToPointer does not convert from string:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
      
      PointerTypeKind PTK;
      auto toElement = E->getType()->getAnyPointerElementType(PTK);
      if (!toElement) {
        Out << "StringToPointer does not convert to pointer:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
      if (PTK != PTK_UnsafePointer && PTK != PTK_UnsafeRawPointer) {
        Out << "StringToPointer converts to non-const pointer:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }
    }
    
    void verifyChecked(CollectionUpcastConversionExpr *E) {
      verifyChecked(E->getSubExpr());
      verifyCheckedBase(E);
    }
        
    void verifyChecked(DerivedToBaseExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying DerivedToBaseExpr", E);

      auto destTy = E->getType();
      auto srcTy = E->getSubExpr()->getType();
      if (destTy->isEqual(srcTy)) {
        Out << "trivial DerivedToBaseExpr:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }

      if (!destTy->getClassOrBoundGenericClass() ||
          !(srcTy->getClassOrBoundGenericClass() ||
            srcTy->is<DynamicSelfType>())) {
        Out << "DerivedToBaseExpr does not involve class types:\n";
        E->print(Out);
        Out << "\n";
        abort();
      }

      checkTrivialSubtype(srcTy, destTy, "DerivedToBaseExpr");
      verifyCheckedBase(E);
    }

    void verifyChecked(AnyHashableErasureExpr *E) {
      auto anyHashableDecl = Ctx.getAnyHashableDecl();
      if (!anyHashableDecl) {
        Out << "AnyHashable declaration could not be found\n";
        abort();
      }

      auto hashableDecl = Ctx.getProtocol(KnownProtocolKind::Hashable);
      if (!hashableDecl) {
        Out << "Hashable declaration could not be found\n";
        abort();
      }

      checkSameType(E->getType(), anyHashableDecl->getDeclaredType(),
                    "AnyHashableErasureExpr and the standard AnyHashable type");

      if (E->getConformance().getRequirement() != hashableDecl) {
        Out << "conformance on AnyHashableErasureExpr was not for Hashable\n";
        E->getConformance().dump();
        abort();
      }

      verifyConformance(E->getSubExpr()->getType(), E->getConformance());

      verifyCheckedBase(E);
    }

    void verifyChecked(TupleElementExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying TupleElementExpr", E);

      Type resultType = E->getType();
      Type baseType = E->getBase()->getType();
      checkSameLValueness(baseType, resultType,
                          "base and result of TupleElementExpr");

      TupleType *tupleType = baseType->getAs<TupleType>();
      if (!tupleType) {
        Out << "base of TupleElementExpr does not have tuple type: ";
        E->getBase()->getType().print(Out);
        Out << "\n";
        abort();
      }

      if (E->getFieldNumber() >= tupleType->getNumElements()) {
        Out << "field index " << E->getFieldNumber()
            << " for TupleElementExpr is out of range [0,"
            << tupleType->getNumElements() << ")\n";
        abort();
      }

      checkSameType(resultType, tupleType->getElementType(E->getFieldNumber()),
                    "TupleElementExpr and the corresponding tuple element");
      verifyCheckedBase(E);
    }

    void maybeRecordValidPointerConversion(Expr *Base, Expr *Arg) {
      auto handleSubExpr = [&](Expr *origSubExpr) {
        auto subExpr = origSubExpr;
        unsigned optionalDepth = 0;

        auto checkIsBindOptional = [&](Expr *expr) {
          for (unsigned depth = optionalDepth; depth; --depth) {
            if (auto bind = dyn_cast<BindOptionalExpr>(expr)) {
              expr = bind->getSubExpr();
            } else {
              Out << "malformed optional pointer conversion\n";
              origSubExpr->print(Out);
              Out << '\n';
              abort();
            }
          }
        };

        // These outer entities will be interleaved in multi-level optionals.
        while (true) {
          // Look through optional evaluations.
          if (auto *optionalEval = dyn_cast<OptionalEvaluationExpr>(subExpr)) {
            subExpr = optionalEval->getSubExpr();
            optionalDepth++;
            continue;
          }

          // Look through injections into Optional<Pointer>.
          if (auto *injectIntoOpt = dyn_cast<InjectIntoOptionalExpr>(subExpr)) {
            subExpr = injectIntoOpt->getSubExpr();
            continue;
          }

          break;
        }

        // Record inout-to-pointer conversions.
        if (auto *inOutToPtr = dyn_cast<InOutToPointerExpr>(subExpr)) {
          ValidInOutToPointerExprs.insert(inOutToPtr);
          checkIsBindOptional(inOutToPtr->getSubExpr());
          return;
        }

        // Record array-to-pointer conversions.
        if (auto *arrayToPtr = dyn_cast<ArrayToPointerExpr>(subExpr)) {
          ValidArrayToPointerExprs.insert(arrayToPtr);
          checkIsBindOptional(arrayToPtr->getSubExpr());
          return;
        }
      };

      // If we have a tuple_shuffle, strip it off. We want to visit the
      // underlying paren or tuple expr.
      if (auto *TupleShuffle = dyn_cast<TupleShuffleExpr>(Arg)) {
        Arg = TupleShuffle->getSubExpr();
      }

      if (auto *ParentExprArg = dyn_cast<ParenExpr>(Arg)) {
        return handleSubExpr(ParentExprArg->getSubExpr());
      }

      if (auto *TupleArg = dyn_cast<TupleExpr>(Arg)) {
        for (auto *SubExpr : TupleArg->getElements()) {
          handleSubExpr(SubExpr);
        }
        return;
      }

      // Otherwise, just run it through handle sub expr. This case can happen if
      // we have an autoclosure.
      if (isa<AutoClosureExpr>(Base)) {
        handleSubExpr(Arg);
        return;
      }
    }

    void verifyChecked(ApplyExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying ApplyExpr", E);

      FunctionType *FT = E->getFn()->getType()->getAs<FunctionType>();
      if (!FT) {
        Out << "callee of apply expression does not have function type:";
        E->getFn()->getType().print(Out);
        Out << "\n";
        abort();
      }
      Type InputExprTy = E->getArg()->getType();
      Type ResultExprTy = E->getType();
      if (!ResultExprTy->isEqual(FT->getResult())) {
        Out << "result of ApplyExpr does not match result type of callee:";
        E->getType().print(Out);
        Out << " vs. ";
        FT->getResult()->print(Out);
        Out << "\n";
        abort();
      }
      if (!InputExprTy->isEqual(FT->getInput())) {
        TupleType *TT = FT->getInput()->getAs<TupleType>();
        if (isa<SelfApplyExpr>(E)) {
          Type InputExprObjectTy;
          if (InputExprTy->hasReferenceSemantics() ||
              InputExprTy->is<AnyMetatypeType>())
            InputExprObjectTy = InputExprTy;
          else
            InputExprObjectTy = checkLValue(InputExprTy, "object argument");
          Type FunctionInputObjectTy = checkLValue(FT->getInput(),
                                                   "'self' parameter");
          
          checkSameOrSubType(InputExprObjectTy, FunctionInputObjectTy,
                             "object argument and 'self' parameter");
        } else if (!TT || TT->getNumElements() != 1 ||
                   !TT->getElement(0).getType()->isEqual(InputExprTy)) {
          Out << "Argument type does not match parameter type in ApplyExpr:"
                 "\nArgument type: ";
          E->getArg()->getType().print(Out);
          Out << "\nParameter type: ";
          FT->getInput()->print(Out);
          Out << "\n";
          E->dump(Out);
          abort();
        }
      }

      if (!E->isThrowsSet()) {
        Out << "apply expression is not marked as throwing or non-throwing\n";
        E->dump(Out);
        abort();
      } else if (E->throws() && !FT->throws()) {
        Out << "apply expression is marked as throwing, but function operand"
               "does not have a throwing function type\n";
        E->dump(Out);
        abort();
      }

      if (E->isSuper() != E->getArg()->isSuperExpr()) {
        Out << "Function application's isSuper() bit mismatch.\n";
        E->dump(Out);
        abort();
      }
      verifyCheckedBase(E);
    }

    void verifyChecked(MemberRefExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying MemberRefExpr", E);

      if (!E->getMember()) {
        Out << "Member reference is missing declaration\n";
        E->dump(Out);
        abort();
      }
      
      // The base of a member reference cannot be an existential type.
      if (E->getBase()->getType()->getWithoutSpecifierType()
            ->isAnyExistentialType()) {
        Out << "Member reference into an unopened existential type\n";
        E->dump(Out);
        abort();
      }

      // The only time the base is allowed to be inout is if we are accessing
      // a computed property or if the base is a protocol or existential.
      if (auto *baseIOT = E->getBase()->getType()->getAs<InOutType>()) {
        if (!baseIOT->getObjectType()->is<ArchetypeType>()) {
          auto *VD = dyn_cast<VarDecl>(E->getMember().getDecl());
          if (!VD || !VD->hasAccessorFunctions()) {
            Out << "member_ref_expr on value of inout type\n";
            E->dump(Out);
            abort();
          }
        }
      }
      
      // FIXME: Check container/member types through substitutions.

      verifyCheckedBase(E);
    }

    void verifyChecked(DynamicMemberRefExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying DynamicMemberRefExpr", E);

      // The base of a dynamic member reference cannot be an
      // existential type.
      if (E->getBase()->getType()->getWithoutSpecifierType()
            ->isAnyExistentialType()) {
        Out << "Member reference into an unopened existential type\n";
        E->dump(Out);
        abort();
      }

      verifyCheckedBase(E);
    }

    void verifyChecked(SubscriptExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying SubscriptExpr", E);

      if (!E->hasDecl()) {
        Out << "Subscript expression is missing subscript declaration";
        abort();
      }

      // The base of a subscript cannot be an existential type.
      if (E->getBase()->getType()->getWithoutSpecifierType()
            ->isAnyExistentialType()) {
        Out << "Member reference into an unopened existential type\n";
        E->dump(Out);
        abort();
      }

      // FIXME: Check base/member types through substitutions.

      verifyCheckedBase(E);
    }

    void verifyChecked(DynamicSubscriptExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying DynamicSubscriptExpr", E);

      // The base of a subscript cannot be an existential type.
      if (E->getBase()->getType()->getWithoutSpecifierType()
            ->isAnyExistentialType()) {
        Out << "Member reference into an unopened existential type\n";
        E->dump(Out);
        abort();
      }

      // FIXME: Check base/member types through substitutions.

      verifyCheckedBase(E);
    }
    
    void checkOptionalObjectType(Type optionalType,
                                 Type objectType,
                                 Expr *E) {
      auto optionalRVType = optionalType->getRValueType();
      auto objectRVType = objectType->getRValueType();
      
      checkSameType(objectRVType, optionalRVType->getAnyOptionalObjectType(),
                    "optional object type");
      
      if (objectType->is<LValueType>() != optionalType->is<LValueType>()) {
        Out << "optional operation must preserve lvalue-ness of base\n";
        E->print(Out);
        abort();
      }
    }

    void verifyChecked(OptionalEvaluationExpr *E) {
      if (E->getType()->hasLValueType()) {
        Out << "Optional evaluation should not produce an lvalue";
        E->print(Out);
        abort();
      }
      checkSameType(E->getType(), E->getSubExpr()->getType(),
                    "OptionalEvaluation cannot change type");
    }
    
    void verifyChecked(BindOptionalExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying BindOptionalExpr", E);

      if (E->getDepth() >= OptionalEvaluations.size()) {
        Out << "BindOptional expression is out of its depth\n";
        E->print(Out);
        abort();
      }
      
      checkOptionalObjectType(E->getSubExpr()->getType(),
                              E->getType(), E);
      
      verifyCheckedBase(E);
    }

    void verifyChecked(CheckedCastExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying CheckCastExpr", E);

      if (!E->isResolved()) {
        Out << "CheckedCast kind not resolved\n";
        abort();
      }

      verifyCheckedBase(E);
    }

    void verifyChecked(CoerceExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying CoerceExpr", E);

      checkSameType(E->getType(), E->getSubExpr()->getType(),
                    "coercion type and subexpression type");

      verifyCheckedBase(E);
    }

    void verifyChecked(IdentityExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying IdentityExpr", E);
      if (!E->getType()->isEqual(E->getSubExpr()->getType())) {
        Out << "Unexpected types in IdentityExpr\n";
        abort();
      }
      checkSameLValueAccessKind(E, E->getSubExpr(), "IdentityExpr");

      verifyCheckedBase(E);
    }

    void verifyChecked(AnyTryExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying AnyTryExpr", E);

      if (!isa<OptionalTryExpr>(E)) {
        checkSameType(E->getType(), E->getSubExpr()->getType(),
                      "AnyTryExpr and sub-expression");
      }

      verifyCheckedBase(E);
    }

    void verifyChecked(OptionalTryExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying OptionalTryExpr", E);

      Type unwrappedType = E->getType()->getOptionalObjectType();
      if (!unwrappedType) {
        Out << "OptionalTryExpr result type is not optional\n";
        abort();
      }

      checkSameType(unwrappedType, E->getSubExpr()->getType(),
                    "OptionalTryExpr and sub-expression");

      verifyCheckedBase(E);
    }

    void verifyChecked(TupleShuffleExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying TupleShuffleExpr", E);

      TupleType *TT = E->getType()->getAs<TupleType>();
      TupleType *SubTT = E->getSubExpr()->getType()->getAs<TupleType>();
      auto getSubElementType = [&](unsigned i) {
        if (E->isSourceScalar()) {
          assert(i == 0);
          return E->getSubExpr()->getType();
        } else {
          return SubTT->getElementType(i);
        }
      };

      /// Retrieve the ith element type from the resulting tuple type.
      auto getOuterElementType = [&](unsigned i) -> Type {
        if (E->isResultScalar()) {
          assert(i == 0);
          return E->getType()->getWithoutParens();
        } else {
          return TT->getElementType(i);
        }
      };

      Type varargsType;
      unsigned callerDefaultArgIndex = 0;
      for (unsigned i = 0, e = E->getElementMapping().size(); i != e; ++i) {
        int subElem = E->getElementMapping()[i];
        if (subElem == TupleShuffleExpr::DefaultInitialize)
          continue;
        if (subElem == TupleShuffleExpr::Variadic) {
          varargsType = TT->getElement(i).getVarargBaseTy();
          break;
        }
        if (subElem == TupleShuffleExpr::CallerDefaultInitialize) {
          auto init = E->getCallerDefaultArgs()[callerDefaultArgIndex++];
          if (!getOuterElementType(i)->isEqual(init->getType())) {
            Out << "Type mismatch in TupleShuffleExpr\n";
            abort();
          }
          continue;
        }
        if (!getOuterElementType(i)->isEqual(getSubElementType(subElem))) {
          Out << "Type mismatch in TupleShuffleExpr\n";
          abort();
        }
      }
      if (varargsType) {
        for (auto sourceIdx : E->getVariadicArgs()) {
          if (!getSubElementType(sourceIdx)->isEqual(varargsType)) {
            Out << "Vararg type mismatch in TupleShuffleExpr\n";
            abort();
          }
        }
      }

      verifyCheckedBase(E);
    }
    
    void verifyChecked(DynamicTypeExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying DynamicTypeExpr", E);

      auto metatype = E->getType()->getAs<AnyMetatypeType>();
      if (!metatype) {
        Out << "DynamicTypeExpr must have metatype type\n";
        abort();
      }

      checkSameType(E->getBase()->getType(), metatype->getInstanceType(),
                    "base type of .Type expression");
      verifyCheckedBase(E);
    }

    void verifyChecked(InjectIntoOptionalExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying InjectIntoOptionalExpr",
                                      E);

      auto valueType = E->getType()->getAnyOptionalObjectType();
      if (!valueType) {
        Out << "InjectIntoOptionalExpr is not of Optional type";
        abort();
      }

      if (!E->getSubExpr()->getType()->isEqual(valueType)) {
        Out << "InjectIntoOptionalExpr operand is not of the value type";
        abort();
      }
      verifyCheckedBase(E);
    }

    void verifyChecked(IfExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying IfExpr", E);

      auto condTy
        = E->getCondExpr()->getType()->getAs<BuiltinIntegerType>();
      if (!condTy || !condTy->isFixedWidth() || condTy->getFixedWidth() != 1) {
        Out << "IfExpr condition is not an i1\n";
        abort();
      }

      checkSameType(E->getThenExpr()->getType(),
                    E->getElseExpr()->getType(),
                    "then and else branches of an if-expr");
      verifyCheckedBase(E);
    }
    
    void verifyChecked(SuperRefExpr *expr) {
      verifyCheckedBase(expr);
    }

    void verifyChecked(TypeExpr *expr) {
      if (!expr->getType()->is<AnyMetatypeType>()) {
        Out << "TypeExpr must have metatype type\n";
        abort();
      }

      verifyCheckedBase(expr);
    }

    void verifyChecked(ForceValueExpr *E) {
      checkOptionalObjectType(E->getSubExpr()->getType(),
                              E->getType(), E);
      
      verifyCheckedBase(E);
    }

    void verifyChecked(OpaqueValueExpr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying OpaqueValueExpr", E);

      if (!OpaqueValues.count(E)) {
        Out << "OpaqueValueExpr not introduced at this point in AST\n";
        abort();
      }

      ++OpaqueValues[E];

      // Make sure opaque values are uniquely-referenced.
      if (OpaqueValues[E] > 1) {
        Out << "Multiple references to unique OpaqueValueExpr\n";
        abort();
      }
      verifyCheckedBase(E);
    }
    
    void verifyChecked(MakeTemporarilyEscapableExpr *E) {
      PrettyStackTraceExpr debugStack(
        Ctx, "verifying MakeTemporarilyEscapableExpr", E);
      
      // Expression type should match subexpression.
      if (!E->getType()->isEqual(E->getSubExpr()->getType())) {
        Out << "MakeTemporarilyEscapableExpr type does not match subexpression";
        abort();
      }

      auto call = dyn_cast<CallExpr>(E->getSubExpr());
      if (!call) {
        Out << "MakeTemporarilyEscapableExpr subexpression is not a call\n";
        abort();
      }

      auto callFnTy = call->getFn()->getType()->getAs<FunctionType>();
      if (!callFnTy) {
        Out << "MakeTemporarilyEscapableExpr call does not call function\n";
        abort();
      }
      if (!callFnTy->getExtInfo().isNoEscape()) {
        Out << "MakeTemporarilyEscapableExpr called function is not noescape\n";
        abort();
      }

      auto callArgTy = call->getArg()->getType()->getAs<FunctionType>();
      if (!callArgTy) {
        Out << "MakeTemporarilyEscapableExpr call argument is not a function\n";
        abort();
      }

      // Closure and opaque value should both be functions, with the closure
      // noescape and the opaque value escapable but otherwise matching.
      auto closureFnTy =
          E->getNonescapingClosureValue()->getType()->getAs<FunctionType>();
      if (!closureFnTy) {
        Out << "MakeTemporarilyEscapableExpr closure type is not a closure\n";
        abort();
      }
      auto opaqueValueFnTy =
          E->getOpaqueValue()->getType()->getAs<FunctionType>();
      if (!opaqueValueFnTy) {
        Out << "MakeTemporarilyEscapableExpr opaque value type is not a "
               "closure\n";
        abort();
      }
      auto closureFnNoEscape =
          closureFnTy->withExtInfo(closureFnTy->getExtInfo().withNoEscape());
      auto opaqueValueNoEscape = opaqueValueFnTy->withExtInfo(
          opaqueValueFnTy->getExtInfo().withNoEscape());
      if (!closureFnNoEscape->isEqual(opaqueValueNoEscape)) {
        Out << "MakeTemporarilyEscapableExpr closure and opaque value type "
               "don't match\n";
        abort();
      }
    }
  
    void verifyChecked(KeyPathApplicationExpr *E) {
      PrettyStackTraceExpr debugStack(
        Ctx, "verifying KeyPathApplicationExpr", E);
      
      auto baseTy = E->getBase()->getType();
      auto keyPathTy = E->getKeyPath()->getType();
      auto resultTy = E->getType();
      
      if (auto nom = keyPathTy->getAs<NominalType>()) {
        if (nom->getDecl() == Ctx.getAnyKeyPathDecl()) {
          // AnyKeyPath application is <T> rvalue T -> rvalue Any?
          if (baseTy->is<LValueType>()) {
            Out << "AnyKeyPath application base is not an rvalue\n";
            abort();
          }
          auto resultObjTy = resultTy->getOptionalObjectType();
          if (!resultObjTy || !resultObjTy->isAny()) {
            Out << "AnyKeyPath application result must be Any?\n";
            abort();
          }
          return;
        }
      } else if (auto bgt = keyPathTy->getAs<BoundGenericType>()) {
        if (bgt->getDecl() == Ctx.getPartialKeyPathDecl()) {
          // PartialKeyPath<T> application is rvalue T -> rvalue Any
          if (!baseTy->isEqual(bgt->getGenericArgs()[0])) {
            Out << "PartialKeyPath application base doesn't match type\n";
            abort();
          }
          if (!resultTy->isAny()) {
            Out << "PartialKeyPath application result must be Any?\n";
            abort();
          }
          return;
        } else if (bgt->getDecl() == Ctx.getKeyPathDecl()) {
          // KeyPath<T, U> application is rvalue T -> rvalue U
          if (!baseTy->isEqual(bgt->getGenericArgs()[0])) {
            Out << "KeyPath application base doesn't match type\n";
            abort();
          }
          if (!resultTy->isEqual(bgt->getGenericArgs()[1])) {
            Out << "KeyPath application result doesn't match type\n";
            abort();
          }
          return;
        } else if (bgt->getDecl() == Ctx.getWritableKeyPathDecl()) {
          // WritableKeyPath<T, U> application is
          //    lvalue T -> lvalue U
          // or rvalue T -> rvalue U
          if (baseTy->is<LValueType>()) {
            if (!resultTy->is<LValueType>()) {
              Out << "WritableKeyPath base and result don't match lvalue-ness\n";
              abort();
            }
            baseTy = baseTy->getRValueType();
            resultTy = resultTy->getRValueType();
          }
          
          if (!baseTy->isEqual(bgt->getGenericArgs()[0])) {
            Out << "WritableKeyPath application base doesn't match type\n";
            abort();
          }
          if (!resultTy->isEqual(bgt->getGenericArgs()[1])) {
            Out << "WritableKeyPath application result doesn't match type\n";
            abort();
          }
          return;
        } else if (bgt->getDecl() == Ctx.getReferenceWritableKeyPathDecl()) {
          // ReferenceWritableKeyPath<T, U> application is
          //    rvalue T -> lvalue U
          // or lvalue T -> lvalue U
          // or rvalue T -> rvalue U
          if (baseTy->is<LValueType>()) {
            if (!resultTy->is<LValueType>()) {
              Out << "ReferenceWritableKeyPath base and result don't "
                     "match lvalue-ness\n";
              abort();
            }
            baseTy = baseTy->getRValueType();
            resultTy = resultTy->getRValueType();
          } else {
            resultTy = resultTy->getRValueType();
          }

          if (!baseTy->isEqual(bgt->getGenericArgs()[0])) {
            Out << "ReferenceWritableKeyPath application base doesn't "
                   "match type\n";
            abort();
          }
          if (!resultTy->isEqual(bgt->getGenericArgs()[1])) {
            Out << "ReferenceWritableKeyPath application result doesn't "
                   "match type\n";
            abort();
          }
          return;
        }
      }
      
      Out << "invalid key path type\n";
      abort();
    }

    static bool hasEnclosingFunctionContext(DeclContext *dc) {
      switch (dc->getContextKind()) {
      case DeclContextKind::AbstractClosureExpr:
      case DeclContextKind::AbstractFunctionDecl:
      case DeclContextKind::SerializedLocal:
        return true;

      case DeclContextKind::TopLevelCodeDecl:
      case DeclContextKind::Module:
      case DeclContextKind::FileUnit:
        return false;

      case DeclContextKind::Initializer:
      case DeclContextKind::GenericTypeDecl:
      case DeclContextKind::ExtensionDecl:
      case DeclContextKind::SubscriptDecl:
        return hasEnclosingFunctionContext(dc->getParent());
      }

      llvm_unreachable("Unhandled DeclContextKind in switch.");
    }

    void verifyChecked(ValueDecl *VD) {
      if (!VD->hasAccess() && !VD->getDeclContext()->isLocalContext() &&
          !isa<GenericTypeParamDecl>(VD) && !isa<ParamDecl>(VD)) {
        dumpRef(VD);
        Out << " does not have access";
        abort();
      }

      // Make sure that there are no archetypes in the interface type.
      if (VD->getDeclContext()->isTypeContext() &&
          !hasEnclosingFunctionContext(VD->getDeclContext()) &&
          VD->getInterfaceType().findIf([](Type type) {
            return type->is<ArchetypeType>();
          })) {
        Out << "Interface type contains archetypes\n";
        VD->dump(Out);
        abort();
      }

      verifyCheckedBase(VD);
    }

    void verifyChecked(PatternBindingDecl *binding) {
      // Look at all of the VarDecls being bound.
      for (auto entry : binding->getPatternList())
        if (auto *P = entry.getPattern())
          P->forEachVariable([&](VarDecl *VD) {
            // ParamDecls never get PBD's.
            assert(!isa<ParamDecl>(VD) && "ParamDecl has a PatternBindingDecl?");
          });
    }

    void verifyChecked(AbstractStorageDecl *ASD) {
      if (ASD->hasAccess() && ASD->isSettable(nullptr)) {
        auto setterAccess = ASD->getSetterFormalAccess();
        if (ASD->getSetter() &&
            ASD->getSetter()->getFormalAccess() != setterAccess) {
          Out << "AbstractStorageDecl's setter access is out of sync"
                 " with the access actually on the setter";
          abort();
        }
      }

      if (auto getter = ASD->getGetter()) {
        if (getter->isMutating() != ASD->isGetterMutating()) {
          Out << "AbstractStorageDecl::isGetterMutating is out of sync"
                 " with whether the getter is actually mutating";
          abort();
        }
      }
      if (auto setter = ASD->getSetter()) {
        if (setter->isMutating() != ASD->isSetterMutating()) {
          Out << "AbstractStorageDecl::isSetterMutating is out of sync"
                 " with whether the setter is actually mutating";
          abort();
        }
      }
      if (auto materializeForSet = ASD->getMaterializeForSetFunc()) {
        if (materializeForSet->isMutating() != ASD->isSetterMutating()) {
          Out << "AbstractStorageDecl::isSetterMutating is out of sync"
                 " with whether materializeForSet is mutating";
          abort();
        }
      }
      if (ASD->hasAddressors()) {
        if (auto addressor = ASD->getAddressor()) {
          if (addressor->isMutating() != ASD->isGetterMutating()) {
            Out << "AbstractStorageDecl::isGetterMutating is out of sync"
                   " with whether immutable addressor is mutating";
            abort();
          }
        }
        if (auto addressor = ASD->getMutableAddressor()) {
          if (addressor->isMutating() != ASD->isSetterMutating()) {
            Out << "AbstractStorageDecl::isSetterMutating is out of sync"
                   " with whether mutable addressor is mutating";
            abort();
          }
        }
      }

      // Make sure we consistently set accessor overrides.
      if (auto *baseASD = ASD->getOverriddenDecl()) {
        if (ASD->getGetter() && baseASD->getGetter())
          assert(ASD->getGetter()->getOverriddenDecl() ==
                 baseASD->getGetter() &&
                 "Storage overrides but getter does not");
        if (ASD->getSetter() && baseASD->getSetter() &&
            baseASD->isSetterAccessibleFrom(ASD->getDeclContext()))
          assert(ASD->getSetter()->getOverriddenDecl() ==
                 baseASD->getSetter() &&
                 "Storage overrides but setter does not");
        if (ASD->getMaterializeForSetFunc() &&
            baseASD->getMaterializeForSetFunc() &&
            baseASD->isSetterAccessibleFrom(ASD->getDeclContext())) {
          if (baseASD->getMaterializeForSetFunc()->hasForcedStaticDispatch()) {
            assert(ASD->getMaterializeForSetFunc()->getOverriddenDecl() == nullptr
                   && "Forced static dispatch materializeForSet should not be "
                   "overridden");
          } else {
            assert(ASD->getMaterializeForSetFunc()->getOverriddenDecl() ==
                   baseASD->getMaterializeForSetFunc() &&
                   "Storage override but materializeForSet does not");
          }
        }
      } else {
        if (ASD->getGetter())
          assert(!ASD->getGetter()->getOverriddenDecl() &&
                 "Storage does not override but getter does");
        if (ASD->getSetter())
          assert(!ASD->getSetter()->getOverriddenDecl() &&
                 "Storage does not override but setter does");
        if (ASD->getMaterializeForSetFunc())
          assert(!ASD->getMaterializeForSetFunc()->getOverriddenDecl() &&
                 "Storage does not override but materializeForSet does");
      }

      verifyCheckedBase(ASD);
    }

    void verifyChecked(VarDecl *var) {
      PrettyStackTraceDecl debugStack("verifying VarDecl", var);

      // Variables must have materializable type, unless they are parameters,
      // in which case they must either have l-value type or be anonymous.
      if (!var->getInterfaceType()->isMaterializable() || var->isInOut()) {
        if (!isa<ParamDecl>(var)) {
          Out << "Non-parameter VarDecl has non-materializable type: ";
          var->getType().print(Out);
          Out << "\n";
          abort();
        }

        if (!var->getInterfaceType()->is<InOutType>() && var->hasName()) {
          Out << "ParamDecl may only have non-materializable tuple type "
                 "when it is anonymous: ";
          var->getType().print(Out);
          Out << "\n";
          abort();
        }
      }

      // The fact that this is *directly* be a reference storage type
      // cuts the code down quite a bit in getTypeOfReference.
      if (var->getAttrs().hasAttribute<OwnershipAttr>() !=
          isa<ReferenceStorageType>(var->getInterfaceType().getPointer())) {
        if (var->getAttrs().hasAttribute<OwnershipAttr>()) {
          Out << "VarDecl has an ownership attribute, but its type"
                 " is not a ReferenceStorageType: ";
        } else {
          Out << "VarDecl has no ownership attribute, but its type"
                 " is a ReferenceStorageType: ";
        }
        var->getInterfaceType().print(Out);
        abort();
      }

      Type typeForAccessors =
          var->getInterfaceType()->getReferenceStorageReferent();
      if (!var->getDeclContext()->contextHasLazyGenericEnvironment()) {
        typeForAccessors =
            var->getDeclContext()->mapTypeIntoContext(typeForAccessors);
        if (const FuncDecl *getter = var->getGetter()) {
          if (getter->getParameterLists().back()->size() != 0) {
            Out << "property getter has parameters\n";
            abort();
          }
          Type getterResultType = getter->getResultInterfaceType();
          getterResultType =
              var->getDeclContext()->mapTypeIntoContext(getterResultType);
          if (!getterResultType->isEqual(typeForAccessors)) {
            Out << "property and getter have mismatched types: '";
            typeForAccessors.print(Out);
            Out << "' vs. '";
            getterResultType.print(Out);
            Out << "'\n";
            abort();
          }
        }
      }

      if (const FuncDecl *setter = var->getSetter()) {
        if (!setter->getResultInterfaceType()->isVoid()) {
          Out << "property setter has non-Void result type\n";
          abort();
        }
        if (setter->getParameterLists().back()->size() == 0) {
          Out << "property setter has no parameters\n";
          abort();
        }
        if (setter->getParameterLists().back()->size() != 1) {
          Out << "property setter has 2+ parameters\n";
          abort();
        }
        const ParamDecl *param = setter->getParameterLists().back()->get(0);
        Type paramType = param->getInterfaceType();
        if (!var->getDeclContext()->contextHasLazyGenericEnvironment()) {
          paramType = var->getDeclContext()->mapTypeIntoContext(paramType);
          if (!paramType->isEqual(typeForAccessors)) {
            Out << "property and setter param have mismatched types:\n";
            typeForAccessors.dump(Out, 2);
            Out << "vs.\n";
            paramType.dump(Out, 2);
            abort();
          }
        }
      }

      if (var->getAttrs().hasAttribute<ImplicitlyUnwrappedOptionalAttr>()) {
        auto varTy = var->getInterfaceType()->getReferenceStorageReferent();

        // FIXME: Update to look for plain Optional once
        // ImplicitlyUnwrappedOptional is removed
        if (!varTy->getAnyOptionalObjectType()) {
          Out << "implicitly unwrapped optional attribute should only be set on VarDecl "
                 "with optional type\n";
          abort();
        }
      }

      verifyCheckedBase(var);
    }

    // Dump a reference to the given declaration.
    void dumpRef(Decl *decl) {
      if (auto value = dyn_cast<ValueDecl>(decl))
        value->dumpRef(Out);
      else if (auto ext = dyn_cast<ExtensionDecl>(decl)) {
        Out << "extension of ";
        if (ext->getExtendedType())
          ext->getExtendedType().print(Out);
      }
    }

    /// Check the given list of protocols.
    void verifyProtocolList(Decl *decl, ArrayRef<ProtocolDecl *> protocols) {
      PrettyStackTraceDecl debugStack("verifying ProtocolList", decl);

      // Make sure that the protocol list is fully expanded.
      SmallVector<ProtocolDecl *, 4> nominalProtocols(protocols.begin(),
                                                      protocols.end());
      ProtocolType::canonicalizeProtocols(nominalProtocols);

      SmallVector<Type, 4> protocolTypes;
      for (auto proto : protocols)
        protocolTypes.push_back(proto->getDeclaredType());
      auto type = ProtocolCompositionType::get(Ctx, protocolTypes,
                                               /*HasExplicitAnyObject=*/false);
      auto layout = type->getExistentialLayout();
      SmallVector<ProtocolDecl *, 4> canonicalProtocols;
      for (auto *protoTy : layout.getProtocols())
        canonicalProtocols.push_back(protoTy->getDecl());
      if (nominalProtocols != canonicalProtocols) {
        dumpRef(decl);
        Out << " doesn't have a complete set of protocols\n";
        abort();
      }      
    }

    /// Verify that the given conformance makes sense for the given
    /// type.
    void verifyConformance(Type type, ProtocolConformanceRef conformance) {
      if (conformance.isAbstract()) {
        if (!type->is<ArchetypeType>() && !type->isAnyExistentialType()) {
          Out << "type " << type
              << " should not have an abstract conformance to "
              << conformance.getRequirement()->getName();
          abort();
        }

        return;
      }

      if (!type->isEqual(conformance.getConcrete()->getType())) {
        Out << "conforming type does not match conformance\n";
        Out << "conforming type:\n";
        type.dump(Out, 2);
        Out << "\nconformance:\n";
        conformance.getConcrete()->dump(Out, 2);
        Out << "\n";
        abort();
      }
    }

    /// Check the given explicit protocol conformance.
    void verifyConformance(Decl *decl, ProtocolConformance *conformance) {
      PrettyStackTraceDecl debugStack("verifying protocol conformance", decl);

      if (!conformance) {
        // FIXME: Eventually, this should itself be a verification
        // failure.
        return;
      }

      switch (conformance->getState()) {
      case ProtocolConformanceState::Complete:
        // More checking below.
        break;
        
      case ProtocolConformanceState::Incomplete:
        // Ignore incomplete conformances; we didn't need them.
        return;

      case ProtocolConformanceState::CheckingTypeWitnesses:
      case ProtocolConformanceState::Checking:
        dumpRef(decl);
        Out << " has a protocol conformance that is still being checked "
            << conformance->getProtocol()->getName().str() << "\n";
        abort();
      }

      auto normal = dyn_cast<NormalProtocolConformance>(conformance);
      if (!normal)
        return;

      // If the conformance is lazily resolved, don't check it; that can cause
      // massive deserialization at a point where the compiler cannot handle it.
      if (normal->isLazilyLoaded()) return;

      // Translate the owning declaration into a DeclContext.
      auto *nominal = dyn_cast<NominalTypeDecl>(decl);
      DeclContext *conformingDC;
      if (nominal) {
        conformingDC = nominal;
      } else {
        auto ext = cast<ExtensionDecl>(decl);
        conformingDC = ext;
        nominal = ext->getExtendedType()->getAnyNominal();
      }

      auto proto = conformance->getProtocol();
      if (normal->getDeclContext() != conformingDC) {
        Out << "AST verification error: conformance of "
            << nominal->getName().str() << " to protocol "
            << proto->getName().str() << " is in the wrong context.\n"
            << "Owning context:\n";
        conformingDC->printContext(Out);
        Out << "Conformance context:\n";
        normal->getDeclContext()->printContext(Out);
        abort();
      }

      // Check that a normal protocol conformance is complete.
      for (auto member : proto->getMembers()) {
        if (auto assocType = dyn_cast<AssociatedTypeDecl>(member)) {
          if (!normal->hasTypeWitness(assocType)) {
            dumpRef(decl);
            Out << " is missing type witness for "
                << conformance->getProtocol()->getName().str() 
                << "." << assocType->getName().str()
                << "\n";
            abort();
          }

          // Make sure that the replacement type only uses archetypes allowed
          // in the context where the normal conformance exists.
          auto replacementType = normal->getTypeWitness(assocType, nullptr);
          Verifier(M, normal->getDeclContext())
            .verifyChecked(replacementType);
          continue;
        }
          
        // No witness necessary for type aliases
        if (isa<TypeAliasDecl>(member))
          continue;
        
        // If this is an accessor for something, ignore it.
        if (auto *FD = dyn_cast<FuncDecl>(member))
          if (FD->isAccessor())
            continue;


        if (auto req = dyn_cast<ValueDecl>(member)) {
          if (!normal->hasWitness(req)) {
            if ((req->getAttrs().isUnavailable(Ctx) ||
                 req->getAttrs().hasAttribute<OptionalAttr>()) &&
                proto->isObjC()) {
              continue;
            }

            dumpRef(decl);
            Out << " is missing witness for "
                << conformance->getProtocol()->getName().str() 
                << "." << req->getBaseName()
                << "\n";
            abort();
          }

          // Check the witness substitutions.
          const auto &witness = normal->getWitness(req, nullptr);

          if (witness.requiresSubstitution()) {
            GenericEnv.push_back({witness.getSyntheticEnvironment()});
            for (const auto &sub : witness.getSubstitutions()) {
              verifyChecked(sub.getReplacement());
            }
            assert(GenericEnv.back().storage.dyn_cast<GenericEnvironment *>()
                     == witness.getSyntheticEnvironment());
            GenericEnv.pop_back();
          }

          continue;
        }
      }

      // Make sure we have the right signature conformances.
      if (!normal->isInvalid()){
        auto conformances = normal->getSignatureConformances();
        unsigned idx = 0;
        for (const auto &req : proto->getRequirementSignature()) {
          if (req.getKind() != RequirementKind::Conformance)
            continue;

          if (idx >= conformances.size()) {
            Out << "error: not enough conformances for requirement signature\n";
            normal->dump(Out);
            abort();
          }

          auto reqProto =
            req.getSecondType()->castTo<ProtocolType>()->getDecl();
          if (reqProto != conformances[idx].getRequirement()) {
            Out << "error: wrong protocol in signature conformances: have "
              << conformances[idx].getRequirement()->getName().str()
              << ", expected " << reqProto->getName().str()<< "\n";
            normal->dump(Out);
            abort();
          }

          ++idx;
        }

        if (idx != conformances.size()) {
          Out << "error: too many conformances for requirement signature\n";
          normal->dump(Out);
          abort();
        }
      }
    }

    void verifyGenericEnvironment(Decl *D,
                                  GenericSignature *sig,
                                  GenericEnvironment *env) {
      if (!sig && !env)
        return;

      if (sig && env) {
        for (auto *paramTy : sig->getGenericParams()) {
          (void)env->mapTypeIntoContext(paramTy);
        }

        return;
      }

      Out << "Decl must have both signature and environment, or neither\n";
      D->dump(Out);
      abort();
    }

    void verifyChecked(GenericTypeDecl *generic) {
      if (!generic->hasLazyGenericEnvironment()) {
        verifyGenericEnvironment(generic,
                                 generic->getGenericSignature(),
                                 generic->getGenericEnvironment());
      }

      verifyCheckedBase(generic);
    }

    void verifyChecked(NominalTypeDecl *nominal) {
      // Make sure that the protocol list is fully expanded.
      verifyProtocolList(nominal, nominal->getLocalProtocols());

      // Make sure that the protocol conformances are complete.
      for (auto conformance : nominal->getLocalConformances()) {
        verifyConformance(nominal, conformance);
      }

      verifyCheckedBase(nominal);
    }

    void verifyChecked(ExtensionDecl *ext) {
      // Make sure that the protocol list is fully expanded.
      verifyProtocolList(ext, ext->getLocalProtocols());

      // Make sure that the protocol conformances are complete.
      for (auto conformance : ext->getLocalConformances()) {
        verifyConformance(ext, conformance);
      }

      verifyCheckedBase(ext);
    }

    void verifyParsed(EnumElementDecl *UED) {
      PrettyStackTraceDecl debugStack("verifying EnumElementDecl", UED);

      if (!isa<EnumDecl>(UED->getDeclContext())) {
        Out << "EnumElementDecl has wrong DeclContext";
        abort();
      }

      verifyParsedBase(UED);
    }

    void verifyParsed(AbstractFunctionDecl *AFD) {
      PrettyStackTraceDecl debugStack("verifying AbstractFunctionDecl", AFD);

      // All of the parameter names should match.
      if (!isa<DestructorDecl>(AFD)) { // Destructor has no non-self params.
        auto paramNames = AFD->getFullName().getArgumentNames();
        bool checkParamNames = (bool)AFD->getFullName();
        bool hasSelf =
          isa<ConstructorDecl>(AFD) || AFD->getDeclContext()->isTypeContext();
        auto *firstParams = AFD->getParameterList(hasSelf ? 1 : 0);

        if (checkParamNames &&
            paramNames.size() != firstParams->size()) {
          Out << "Function name does not match its argument pattern ("
              << paramNames.size() << " elements instead of "
              << firstParams->size() << ")\n";
          AFD->dump(Out);
          abort();
        }

        // This doesn't use for_each because paramNames shouldn't be checked
        // when the function is anonymous.
        for (size_t i = 0, e = firstParams->size(); i < e; ++i) {
          auto &param = firstParams->get(i);

          if (checkParamNames &&
              param->getArgumentName() != paramNames[i]) {
            Out << "Function full name doesn't match parameter's arg name\n";
            AFD->dump(Out);
            abort();
          }
        }
      }

      verifyParsedBase(AFD);
    }

    void verifyParsed(ConstructorDecl *CD) {
      PrettyStackTraceDecl debugStack("verifying ConstructorDecl", CD);

      auto *DC = CD->getDeclContext();
      if (!isa<NominalTypeDecl>(DC) && !isa<ExtensionDecl>(DC) &&
          !CD->isInvalid()) {
        Out << "ConstructorDecls outside nominal types and extensions "
               "should be marked invalid";
        abort();
      }

      verifyParsedBase(CD);
    }

    void verifyChecked(ProtocolDecl *PD) {
      PrettyStackTraceDecl debugStack("verifying ProtocolDecl", PD);

      if (PD->isObjC() && !PD->requiresClass()) {
        Out << "@objc protocols should be class protocols as well";
        abort();
      }
      verifyCheckedBase(PD);
    }

    void verifyChecked(ConstructorDecl *CD) {
      PrettyStackTraceDecl debugStack("verifying ConstructorDecl", CD);

      auto *ND = CD->getDeclContext()->getAsNominalTypeOrNominalTypeExtensionContext();
      if (!isa<ClassDecl>(ND) && !isa<StructDecl>(ND) && !isa<EnumDecl>(ND) &&
          !isa<ProtocolDecl>(ND) && !CD->isInvalid()) {
        Out << "ConstructorDecls outside structs, classes or enums "
               "should be marked invalid";
        abort();
      }

      // Verify that the optionality of the result type of the
      // initializer matches the failability of the initializer.
      if (!CD->isInvalid() && 
          CD->getDeclContext()->getDeclaredInterfaceType()->getAnyNominal() 
            != Ctx.getOptionalDecl() &&
          CD->getDeclContext()->getDeclaredInterfaceType()->getAnyNominal() 
            != Ctx.getImplicitlyUnwrappedOptionalDecl()) {
        OptionalTypeKind resultOptionality = OTK_None;
        CD->getResultInterfaceType()->getAnyOptionalObjectType(resultOptionality);
        if (resultOptionality != CD->getFailability()) {
          Out << "Initializer has result optionality/failability mismatch\n";
          CD->dump(llvm::errs());
          abort();
        }

        // Also check the interface type.
        if (auto genericFn 
              = CD->getInterfaceType()->getAs<GenericFunctionType>()) {
          resultOptionality = OTK_None;
          genericFn->getResult()->castTo<AnyFunctionType>()->getResult()
            ->getAnyOptionalObjectType(resultOptionality);
          if (resultOptionality != CD->getFailability()) {
            Out << "Initializer has result optionality/failability mismatch\n";
            CD->dump(llvm::errs());
            abort();
          }
        }
      }

      verifyCheckedBase(CD);
    }

    void verifyParsed(DestructorDecl *DD) {
      PrettyStackTraceDecl debugStack("verifying DestructorDecl", DD);

      if (DD->isGeneric()) {
        Out << "DestructorDecl cannot be generic";
        abort();
      }

      auto *DC = DD->getDeclContext();
      if (!isa<NominalTypeDecl>(DC) && !isa<ExtensionDecl>(DC) &&
          !DD->isInvalid()) {
        Out << "DestructorDecls outside nominal types and extensions "
               "should be marked invalid";
        abort();
      }

      verifyParsedBase(DD);
    }

    void verifyChecked(AbstractFunctionDecl *AFD) {
      PrettyStackTraceDecl debugStack("verifying AbstractFunctionDecl", AFD);

      // If this function is generic or is within a generic context, it should
      // have an interface type.
      if (AFD->isGenericContext() !=
          AFD->getInterfaceType()->is<GenericFunctionType>()) {
        Out << "Functions in generic context must have an interface type\n";
        AFD->dump(Out);
        abort();
      }

      // If the function has a generic interface type, it should also have a
      // generic signature.
      if (AFD->isGenericContext() !=
          (AFD->getGenericSignature() != nullptr)) {
        Out << "Functions in generic context must have a generic signature\n";
        AFD->dump(Out);
        abort();
      }

      if (!AFD->hasLazyGenericEnvironment()) {
        verifyGenericEnvironment(AFD,
                                 AFD->getGenericSignature(),
                                 AFD->getGenericEnvironment());
      }

      // If there is an interface type, it shouldn't have any unresolved
      // dependent member types.
      // FIXME: This is a general property of the type system.
      auto interfaceTy = AFD->getInterfaceType();
      if (auto unresolvedDependentTy
            = interfaceTy->findUnresolvedDependentMemberType()) {
        Out << "Unresolved dependent member type ";
        unresolvedDependentTy->print(Out);
        abort();
      }

      // Throwing @objc methods must have a foreign error convention.
      if (AFD->isObjC() &&
          static_cast<bool>(AFD->getForeignErrorConvention())
            != AFD->hasThrows()) {
        if (AFD->hasThrows())
          Out << "@objc method throws but does not have a foreign error "
              << "convention";
        else
          Out << "@objc method has a foreign error convention but does not "
              << "throw";
        abort();
      }

      // If a decl has the Throws bit set, the ThrowsLoc should be valid,
      // and vice versa, unless the decl was imported, de-serialized, or
      // implicit.
      if (!AFD->isImplicit() &&
          isa<SourceFile>(AFD->getModuleScopeContext()) &&
          (AFD->getThrowsLoc().isValid() != AFD->hasThrows())) {
        Out << "function 'throws' location does not match 'throws' flag\n";
        AFD->dump(Out);
        abort();
      }

      // If a decl has the Throws bit set, the function type should throw,
      // and vice versa.
      auto fnTy = AFD->getInterfaceType()->castTo<AnyFunctionType>();
      for (unsigned i = 1, e = AFD->getNumParameterLists(); i != e; ++i)
        fnTy = fnTy->getResult()->castTo<AnyFunctionType>();

      if (AFD->hasThrows() != fnTy->getExtInfo().throws()) {
        Out << "function 'throws' flag does not match function type\n";
        AFD->dump(Out);
        abort();
      }

      if (AFD->getForeignErrorConvention()
          && !AFD->isObjC() && !AFD->getAttrs().hasAttribute<CDeclAttr>()) {
        Out << "foreign error convention on non-@objc, non-@_cdecl function\n";
        AFD->dump(Out);
        abort();
      }

      verifyCheckedBase(AFD);
    }

    void verifyChecked(DestructorDecl *DD) {
      PrettyStackTraceDecl debugStack("verifying DestructorDecl", DD);

      auto *ND = DD->getDeclContext()->getAsNominalTypeOrNominalTypeExtensionContext();
      if (!isa<ClassDecl>(ND) && !DD->isInvalid()) {
        Out << "DestructorDecls outside classes should be marked invalid";
        abort();
      }
      verifyCheckedBase(DD);
    }

    void verifyChecked(FuncDecl *FD) {
      PrettyStackTraceDecl debugStack("verifying FuncDecl", FD);

      if (FD->isAccessor()) {
        auto *storageDecl = FD->getAccessorStorageDecl();
        if (!storageDecl) {
          Out << "Missing storage decl\n";
          abort();
        }

        if (FD->isGetterOrSetter()) {
          if (FD->isFinal() != storageDecl->isFinal()) {
            Out << "Property and accessor do not match for 'final'\n";
            abort();
          }
          if (FD->isDynamic() != storageDecl->isDynamic()) {
            Out << "Property and accessor do not match for 'dynamic'\n";
            abort();
          }
        }

        auto storedAccessor =
          storageDecl->getAccessorFunction(FD->getAccessorKind());
        if (storedAccessor != FD) {
          Out << "storage declaration has different accessor for this kind\n";
          abort();
        }

        switch (FD->getAccessorKind()) {
        case AccessorKind::NotAccessor: llvm_unreachable("bad kind");
        case AccessorKind::IsGetter:
        case AccessorKind::IsSetter:
        case AccessorKind::IsWillSet:
        case AccessorKind::IsDidSet:
        case AccessorKind::IsMaterializeForSet:
          if (FD->getAddressorKind() != AddressorKind::NotAddressor) {
            Out << "non-addressor accessor has an addressor kind\n";
            abort();
          }
          break;

        case AccessorKind::IsAddressor:
        case AccessorKind::IsMutableAddressor:
          if (FD->getAddressorKind() == AddressorKind::NotAddressor) {
            Out << "addressor does not have an addressor kind\n";
            abort();
          }
          break;
        }
      }

      if (FD->isMutating()) {
        if (!FD->isInstanceMember()) {
          Out << "mutating function is not an instance member\n";
          abort();
        }
        if (FD->getDeclContext()->getAsClassOrClassExtensionContext()) {
          Out << "mutating function in a class\n";
          abort();
        }
        const ParamDecl *selfParam = FD->getImplicitSelfDecl();
        if (!selfParam->getInterfaceType()->is<InOutType>()) {
          Out << "mutating function does not have inout 'self'\n";
          abort();
        }
      } else {
        const ParamDecl *selfParam = FD->getImplicitSelfDecl();
        if (selfParam && selfParam->getInterfaceType()->is<InOutType>()) {
          Out << "non-mutating function has inout 'self'\n";
          abort();
        }
      }

      if (FD->getAttrs().hasAttribute<ImplicitlyUnwrappedOptionalAttr>()) {
        if (!FD->getInterfaceType() ||
            !FD->getInterfaceType()->is<AnyFunctionType>()) {
          Out << "Expected FuncDecl to have a function type!\n";
          abort();
        }

        auto resultTy = FD->getResultInterfaceType();

        // FIXME: Update to look for plain Optional once
        // ImplicitlyUnwrappedOptional is removed
        if (!resultTy->getAnyOptionalObjectType()) {
          Out << "implicitly unwrapped optional attribute should only be set "
                 "on functions with optional return types\n";
          abort();
        }
      }

      verifyCheckedBase(FD);
    }

    void verifyParsed(FuncDecl *FD) {
      PrettyStackTraceDecl debugStack("verifying FuncDecl", FD);

      unsigned MinParamPatterns = FD->getImplicitSelfDecl() ? 2 : 1;
      if (FD->getParameterLists().size() < MinParamPatterns) {
        Out << "should have at least " << MinParamPatterns
            << " parameter patterns\n";
        abort();
      }

      if (FD->isAccessor()) {
        unsigned NumExpectedParamPatterns = 1;
        if (FD->getImplicitSelfDecl())
          NumExpectedParamPatterns++;
        if (FD->getParameterLists().size() != NumExpectedParamPatterns) {
          Out << "accessors should not be curried\n";
          abort();
        }
      }

      if (auto *VD = FD->getAccessorStorageDecl()) {
        if (isa<VarDecl>(VD)
            && cast<VarDecl>(VD)->isStatic() != FD->isStatic()) {
          Out << "getter or setter static-ness must match static-ness of var\n";
          abort();
        }
      }

      verifyParsedBase(FD);
    }

    void verifyChecked(ClassDecl *CD) {
      PrettyStackTraceDecl debugStack("verifying ClassDecl", CD);
      
      if (!CD->hasLazyMembers()) {
        unsigned NumDestructors = 0;
        for (auto Member : CD->getMembers()) {
          if (isa<DestructorDecl>(Member)) {
            NumDestructors++;
          }
        }
        if (NumDestructors != 1) {
          Out << "every class should have exactly one destructor, "
                 "explicitly provided or created by the type checker\n";
          abort();
        }
      }
      
      if (!CD->hasDestructor()) {
        Out << "every class's 'has destructor' bit must be set\n";
        abort();
      }

      verifyCheckedBase(CD);
    }

    void verifyParsed(AssociatedTypeDecl *ATD) {
      PrettyStackTraceDecl debugStack("verifying AssociatedTypeDecl", ATD);

      auto *DC = ATD->getDeclContext();
      if (!isa<NominalTypeDecl>(DC) ||
          !isa<ProtocolDecl>(cast<NominalTypeDecl>(DC))) {
        Out << "AssociatedTypeDecl should only occur inside a protocol\n";
        abort();
      }
      verifyParsedBase(ATD);
    }

    void verifyParsed(TuplePattern *TP) {
      PrettyStackTracePattern debugStack(Ctx, "verifying TuplePattern", TP);
      verifyParsedBase(TP);
    }

    void verifyChecked(TuplePattern *TP) {
      PrettyStackTracePattern debugStack(Ctx, "verifying TuplePattern", TP);
      verifyCheckedBase(TP);
    }

    /// Look through a possible l-value type, returning true if it was
    /// an l-value.
    bool lookThroughLValue(Type &type, bool &isInOut) {
      if (LValueType *lv = type->getAs<LValueType>()) {
        Type objectType = lv->getObjectType();
        if (objectType->is<LValueType>()) {
          Out << "type is an lvalue of lvalue type: ";
          type.print(Out);
          Out << "\n";
        }
        isInOut = false;
        type = objectType;
        return true;
      }
      if (InOutType *io = type->getAs<InOutType>()) {
        Type objectType = io->getObjectType();
        if (objectType->is<InOutType>()) {
          Out << "type is an inout of inout type: ";
          type.print(Out);
          Out << "\n";
        }
        isInOut = true;
        type = objectType;
        return true;
      }
      return false;
    }

    /// The two types are required to either both be l-values or
    /// both not be l-values.  They are adjusted to not be l-values.
    /// Returns true if they are both l-values.
    bool checkSameLValueness(Type &T0, Type &T1,
                             const char *what) {
      bool Q0, Q1;
      bool isLValue0 = lookThroughLValue(T0, Q0);
      bool isLValue1 = lookThroughLValue(T1, Q1);
      
      if (isLValue0 != isLValue1) {
        Out << "lvalue-ness of " << what << " do not match: "
            << isLValue0 << ", " << isLValue1 << "\n";
        abort();
      }

      if (isLValue0 && Q0 != Q1) {
        Out << "qualification of " << what << " do not match\n";
        abort();
      }

      return isLValue0;
    }

    Type checkLValue(Type T, const char *what) {
      LValueType *LV = T->getAs<LValueType>();
      if (LV)
        return LV->getObjectType();

      Out << "type is not an l-value in " << what << ": ";
      T.print(Out);
      Out << "\n";
      abort();
    }

    void checkSameLValueAccessKind(Expr *LHS, Expr *RHS, const char *what) {
      if (LHS->hasLValueAccessKind() != RHS->hasLValueAccessKind() ||
          (LHS->hasLValueAccessKind() &&
           LHS->getLValueAccessKind() != RHS->getLValueAccessKind())) {
        Out << what << " has a mismatched l-value access kind\n";
        abort();
      }
    }

    // Verification utilities.
    Type checkMetatypeType(Type type, const char *what) {
      auto metatype = type->getAs<AnyMetatypeType>();
      if (metatype) return metatype->getInstanceType();

      Out << what << " is not a metatype: ";
      type.print(Out);
      Out << "\n";
      abort();
    }

    void checkSameType(Type T0, Type T1, const char *what) {
      if (T0->isEqual(T1))
        return;

      Out << "different types for " << what << ": ";
      T0.print(Out);
      Out << " vs. ";
      T1.print(Out);
      Out << "\n";
      abort();
    }

    void checkTrivialSubtype(Type srcTy, Type destTy, const char *what) {
      if (srcTy->isEqual(destTy)) return;

      if (auto srcMetatype = srcTy->getAs<AnyMetatypeType>()) {
        if (auto destMetatype = destTy->getAs<AnyMetatypeType>()) {
          return checkTrivialSubtype(srcMetatype->getInstanceType(),
                                     destMetatype->getInstanceType(),
                                     what);
        }
        goto fail;
      }

      // If the destination is a class, walk the supertypes of the source.
      if (destTy->getClassOrBoundGenericClass()) {
        if (!destTy->isBindableToSuperclassOf(srcTy)) {
          srcTy.print(Out);
          Out << " is not a superclass of ";
          destTy.print(Out);
          Out << " for " << what << "\n";
          abort();
        }

        return;
      }

      // FIXME: Tighten up checking for conversions to protocol types.
      if (destTy->isExistentialType())
        return;

    fail:
      Out << "subtype conversion in " << what << " is invalid: ";
      srcTy.print(Out);
      Out << " to ";
      destTy.print(Out);
      Out << "\n";
      abort();
    }

    void checkSameOrSubType(Type T0, Type T1, const char *what) {
      if (T0->isEqual(T1))
        return;

      // Protocol subtyping.
      if (auto Proto0 = T0->getAs<ProtocolType>())
        if (auto Proto1 = T1->getAs<ProtocolType>())
          if (Proto0->getDecl()->inheritsFrom(Proto1->getDecl()))
            return;
      
      // FIXME: Actually check this?
      if (T0->isExistentialType() || T1->isExistentialType())
        return;
      
      Out << "incompatible types for " << what << ": ";
      T0.print(Out);
      Out << " vs. ";
      T1.print(Out);
      Out << "\n";
      abort();
    }

    Type checkExceptionTypeExists(const char *where) {
      auto exn = Ctx.getErrorDecl();
      if (exn) return exn->getDeclaredType();

      Out << "exception type does not exist in " << where << "\n";
      abort();
    }

    bool isGoodSourceRange(SourceRange SR) {
      if (SR.isInvalid())
        return false;
      (void) Ctx.SourceMgr.findBufferContainingLoc(SR.Start);
      (void) Ctx.SourceMgr.findBufferContainingLoc(SR.End);
      return true;
    }
    
    template<typename T>
    void checkSourceRangesBase(T ASTNode) {
      checkSourceRanges(cast<typename ASTNodeBase<T>::BaseTy>(ASTNode));
    }
    
    void checkSourceRanges(Expr *E) {
      PrettyStackTraceExpr debugStack(Ctx, "verifying ranges", E);

      if (!E->getSourceRange().isValid()) {
        // We don't care about source ranges on implicitly-generated
        // expressions.
        if (E->isImplicit())
          return;

        Out << "invalid source range for expression: ";
        E->print(Out);
        Out << "\n";
        abort();
      }
      if (!isGoodSourceRange(E->getSourceRange())) {
        Out << "bad source range for expression: ";
        E->print(Out);
        Out << "\n";
        abort();
      }
      // FIXME: Re-visit this to always do the check.
      if (!E->isImplicit())
        checkSourceRanges(E->getSourceRange(), Parent,
                          [&]{ E->print(Out); } );
    }

    void checkSourceRanges(Stmt *S) {
      PrettyStackTraceStmt debugStack(Ctx, "verifying ranges", S);

      if (!S->getSourceRange().isValid()) {
        // We don't care about source ranges on implicitly-generated
        // statements.
        if (S->isImplicit())
          return;

        Out << "invalid source range for statement: ";
        S->print(Out);
        Out << "\n";
        abort();
      }
      if (!isGoodSourceRange(S->getSourceRange())) {
        Out << "bad source range for statement: ";
        S->print(Out);
        Out << "\n";
        abort();
      }
      checkSourceRanges(S->getSourceRange(), Parent,
                        [&]{ S->print(Out); });
    }

    void checkSourceRanges(IfConfigDecl *ICD) {
      checkSourceRangesBase(ICD);

      SourceLoc Location = ICD->getStartLoc();
      for (auto &Clause : ICD->getClauses()) {
        // Clause start, note that the first clause start location is the
        // same as that of the whole statement
        if (Location == ICD->getStartLoc()) {
          if (Location != Clause.Loc) {
            Out << "bad start location of IfConfigDecl first clause\n";
            ICD->print(Out);
            abort();
          }
        } else {
          if (!Ctx.SourceMgr.isBeforeInBuffer(Location, Clause.Loc)) {
            Out << "bad start location of IfConfigDecl clause\n";
            ICD->print(Out);
            abort();
          }
        }
        Location = Clause.Loc;

        // Condition if present
        Expr *Cond = Clause.Cond;
        if (Cond) {
          if (!Ctx.SourceMgr.isBeforeInBuffer(Location, Cond->getStartLoc())) {
            Out << "invalid IfConfigDecl clause condition start location\n";
            ICD->print(Out);
            abort();
          }
          Location = Cond->getEndLoc();
        }
        
        // Body elements
        auto StoredLoc = Location;
        for (auto &Element : Clause.Elements) {
          auto StartLocation = Element.getStartLoc();
          if (StartLocation.isInvalid()) {
            continue;
          }
          
          if (!Ctx.SourceMgr.isBeforeInBuffer(StoredLoc, StartLocation)) {
            Out << "invalid IfConfigDecl clause element start location\n";
            ICD->print(Out);
            abort();
          }
          
          auto EndLocation = Element.getEndLoc();
          if (EndLocation.isValid() &&
              Ctx.SourceMgr.isBeforeInBuffer(Location, EndLocation)) {
            Location = EndLocation;
          }
        }
      }

      if (Ctx.SourceMgr.isBeforeInBuffer(ICD->getEndLoc(), Location)) {
        Out << "invalid IfConfigDecl end location\n";
        ICD->print(Out);
        abort();
      }
    }
    
    void checkSourceRanges(Pattern *P) {
      PrettyStackTracePattern debugStack(Ctx, "verifying ranges", P);

      // We don't care about source ranges on implicitly-generated
      // patterns.
      if (P->isImplicit())
        return;

      if (!P->getSourceRange().isValid()) {
        Out << "invalid source range for pattern: ";
        P->print(Out);
        Out << "\n";
        abort();
      }
      if (!isGoodSourceRange(P->getSourceRange())) {
        Out << "bad source range for pattern: ";
        P->print(Out);
        Out << "\n";
        abort();
      }
      checkSourceRanges(P->getSourceRange(), Parent,
                        [&]{ P->print(Out); });
    }

    void assertValidRegion(Decl *D) {
      auto R = D->getSourceRange();
      if (R.isValid() && Ctx.SourceMgr.isBeforeInBuffer(R.End, R.Start)) {
        Out << "invalid type source range for decl: ";
        D->print(Out);
        Out << "\n";
        abort();
      }
    }

    void checkSourceRanges(ParamDecl *PD) {
      assertValidRegion(PD);
    }

    void checkSourceRanges(Decl *D) {
      PrettyStackTraceDecl debugStack("verifying ranges", D);

      if (!D->getSourceRange().isValid()) {
        // We don't care about source ranges on implicitly-generated
        // decls.
        if (D->isImplicit())
          return;
        
        Out << "invalid source range for decl: ";
        D->print(Out);
        Out << "\n";
        abort();
      }
      checkSourceRanges(D->getSourceRange(), Parent,
                        [&]{ D->print(Out); });
    }

    /// \brief Verify that the given source ranges is contained within the
    /// parent's source range.
    void checkSourceRanges(SourceRange Current,
                           ASTWalker::ParentTy Parent,
                           std::function<void()> printEntity) {
      SourceRange Enclosing;
      if (Parent.isNull())
          return;

      if (Parent.getAsModule()) {
        return;
      } else if (Decl *D = Parent.getAsDecl()) {
        Enclosing = D->getSourceRange();
        if (D->isImplicit())
          return;
        // FIXME: This is not working well for decl parents.
        return;
      } else if (Stmt *S = Parent.getAsStmt()) {
        Enclosing = S->getSourceRange();
        if (S->isImplicit())
          return;
      } else if (Pattern *P = Parent.getAsPattern()) {
        Enclosing = P->getSourceRange();
        if (P->isImplicit())
          return;
      } else if (Expr *E = Parent.getAsExpr()) {
        // FIXME: This hack is required because the inclusion check below
        // doesn't compares the *start* of the ranges, not the end of the
        // ranges.  In the case of an interpolated string literal expr, the
        // subexpressions are contained within the string token.  This means
        // that comparing the start of the string token to the end of an
        // embedded expression will fail.
        if (isa<InterpolatedStringLiteralExpr>(E))
          return;

        if (E->isImplicit())
          return;
        
        Enclosing = E->getSourceRange();
      } else if (TypeRepr *TyR = Parent.getAsTypeRepr()) {
        Enclosing = TyR->getSourceRange();
      } else {
        llvm_unreachable("impossible parent node");
      }
      
      if (!Ctx.SourceMgr.rangeContains(Enclosing, Current)) {
        Out << "child source range not contained within its parent: ";
        printEntity();
        Out << "\n  parent range: ";
        Enclosing.print(Out, Ctx.SourceMgr);
        Out << "\n  child range: ";
        Current.print(Out, Ctx.SourceMgr);
        Out << "\n";
        abort();
      }
    }

    void checkErrors(Expr *E) {}
    void checkErrors(Stmt *S) {}
    void checkErrors(Pattern *P) {}
    void checkErrors(Decl *D) {}
    void checkErrors(ValueDecl *D) {
      PrettyStackTraceDecl debugStack("verifying errors", D);

      if (!D->hasInterfaceType())
        return;
      if (D->getInterfaceType()->hasError() && !D->isInvalid()) {
        Out << "Valid decl has error type!\n";
        D->dump(Out);
        abort();
      }
    }
  };
} // end anonymous namespace

void swift::verify(SourceFile &SF) {
#if !(defined(NDEBUG) || defined(SWIFT_DISABLE_AST_VERIFIER))
  Verifier verifier(SF, &SF);
  SF.walk(verifier);
#endif
}

bool swift::shouldVerify(const Decl *D, const ASTContext &Context) {
#if !(defined(NDEBUG) || defined(SWIFT_DISABLE_AST_VERIFIER))
  if (const auto *ED = dyn_cast<ExtensionDecl>(D)) {
    return shouldVerify(ED->getExtendedType()->getAnyNominal(), Context);
  }

  const auto *VD = dyn_cast<ValueDecl>(D);
  if (!VD) {
    // Verify declarations without names everywhere.
    return true;
  }

  return true;
#else
  return false;
#endif
}

void swift::verify(Decl *D) {
#if !(defined(NDEBUG) || defined(SWIFT_DISABLE_AST_VERIFIER))
  Verifier V = Verifier::forDecl(D);
  D->walk(V);
#endif
}

