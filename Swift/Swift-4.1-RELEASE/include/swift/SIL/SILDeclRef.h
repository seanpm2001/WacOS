//===--- SILDeclRef.h - Defines the SILDeclRef struct -----------*- C++ -*-===//
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
// This file defines the SILDeclRef struct, which is used to identify a SIL
// global identifier that can be used as the operand of a FunctionRefInst
// instruction or that can have a SIL Function associated with it.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_SILDeclRef_H
#define SWIFT_SIL_SILDeclRef_H

#include "swift/AST/ClangNode.h"
#include "swift/AST/ResilienceExpansion.h"
#include "swift/AST/TypeAlignments.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/PrettyStackTrace.h"

namespace llvm {
  class raw_ostream;
}

namespace swift {
  enum class EffectsKind : uint8_t;
  class AbstractFunctionDecl;
  class AbstractClosureExpr;
  class ValueDecl;
  class FuncDecl;
  class ClosureExpr;
  class AutoClosureExpr;
  class ASTContext;
  class ClassDecl;
  class SILFunctionType;
  enum class SILLinkage : unsigned char;
  enum IsSerialized_t : unsigned char;
  enum class SubclassScope : unsigned char;
  class SILModule;
  class SILLocation;
  class AnyFunctionRef;

/// How a method is dispatched.
enum class MethodDispatch {
  // The method implementation can be referenced statically.
  Static,
  // The method implementation uses class_method dispatch.
  Class,
};

/// Get the method dispatch mechanism for a method.
MethodDispatch getMethodDispatch(AbstractFunctionDecl *method);

/// True if calling the given method or property should use ObjC dispatch.
bool requiresForeignEntryPoint(ValueDecl *vd);

/// True if the entry point is natively foreign.
bool requiresForeignToNativeThunk(ValueDecl *vd);

enum ForDefinition_t : bool {
  NotForDefinition = false,
  ForDefinition = true
};

/// \brief A key for referencing a Swift declaration in SIL.
///
/// This can currently be either a reference to a ValueDecl for functions,
/// methods, constructors, and other named entities, or a reference to a
/// AbstractClosureExpr for an anonymous function.  In addition to the AST
/// reference, there are discriminators for referencing different
/// implementation-level entities associated with a single language-level
/// declaration, such as uncurry levels of a function, the allocating and
/// initializing entry points of a constructor, etc.
struct SILDeclRef {
  typedef llvm::PointerUnion<ValueDecl *, AbstractClosureExpr *> Loc;
  
  /// Represents the "kind" of the SILDeclRef. For some Swift decls there
  /// are multiple SIL entry points, and the kind is used to distinguish them.
  enum class Kind : unsigned {
    /// \brief This constant references the FuncDecl or AbstractClosureExpr
    /// in loc.
    Func,

    /// Allocator - this constant references the allocating constructor
    /// entry point of a class ConstructorDecl or the constructor of a value
    /// ConstructorDecl.
    Allocator,
    /// Initializer - this constant references the initializing constructor
    /// entry point of the class ConstructorDecl in loc.
    Initializer,
    
    /// EnumElement - this constant references the injection function for
    /// an EnumElementDecl.
    EnumElement,
    
    /// Destroyer - this constant references the destroying destructor for the
    /// DestructorDecl in loc.
    Destroyer,

    /// Deallocator - this constant references the deallocating
    /// destructor for the DestructorDecl in loc.
    Deallocator,
    
    /// GlobalAccessor - this constant references the lazy-initializing
    /// accessor for the global VarDecl in loc.
    GlobalAccessor,

    /// GlobalGetter - this constant references the lazy-initializing
    /// getter for the global VarDecl.
    GlobalGetter,

    /// References the generator for a default argument of a function.
    DefaultArgGenerator,

    /// References the initializer expression for a stored property
    /// of a nominal type.
    StoredPropertyInitializer,

    /// References the ivar initializer for the ClassDecl in loc.
    ///
    /// Only classes that are allocated using Objective-C's allocation
    /// routines have an ivar initializer, which is emitted as
    /// .cxx_construct.
    IVarInitializer,

    /// References the ivar destroyer for the ClassDecl in loc.
    ///
    /// Only classes that are allocated using Objective-C's allocation
    /// routines have an ivar destroyer, which is emitted as
    /// .cxx_destruct.
    IVarDestroyer,
  };
  
  /// The ValueDecl or AbstractClosureExpr represented by this SILDeclRef.
  Loc loc;
  /// The Kind of this SILDeclRef.
  Kind kind : 4;
  /// The required resilience expansion of the declaration.
  unsigned Expansion : 1;
  /// True if the SILDeclRef is a curry thunk.
  unsigned isCurried : 1;
  /// True if this references a foreign entry point for the referenced decl.
  unsigned isForeign : 1;
  /// True if this is a direct reference to a class's method implementation
  /// that isn't dynamically dispatched.
  unsigned isDirectReference : 1;
  /// The default argument index for a default argument getter.
  unsigned defaultArgIndex : 10;
  
  /// Produces a null SILDeclRef.
  SILDeclRef() : loc(), kind(Kind::Func), Expansion(0),
                 isCurried(0), isForeign(0), isDirectReference(0),
                 defaultArgIndex(0) {}
  
  /// Produces a SILDeclRef of the given kind for the given decl.
  explicit SILDeclRef(ValueDecl *decl, Kind kind,
                      ResilienceExpansion expansion
                        = ResilienceExpansion::Minimal,
                      bool isCurried = false,
                      bool isForeign = false);
  
  /// Produces a SILDeclRef for the given ValueDecl or
  /// AbstractClosureExpr:
  /// - If 'loc' is a func or closure, this returns a Func SILDeclRef.
  /// - If 'loc' is a ConstructorDecl, this returns the Allocator SILDeclRef
  ///   for the constructor.
  /// - If 'loc' is an EnumElementDecl, this returns the EnumElement
  ///   SILDeclRef for the enum element.
  /// - If 'loc' is a DestructorDecl, this returns the Destructor SILDeclRef
  ///   for the containing ClassDecl.
  /// - If 'loc' is a global VarDecl, this returns its GlobalAccessor
  ///   SILDeclRef.
  ///
  /// If 'isCurried' is true, the loc must be a method or enum element;
  /// the SILDeclRef will then refer to a curry thunk with type
  /// (Self) -> (Args...) -> Result, rather than a direct reference to
  /// the actual method whose lowered type is (Args..., Self) -> Result.
  explicit SILDeclRef(Loc loc,
                      ResilienceExpansion expansion
                        = ResilienceExpansion::Minimal,
                      bool isCurried = false,
                      bool isForeign = false);

  /// Produce a SIL constant for a default argument generator.
  static SILDeclRef getDefaultArgGenerator(Loc loc, unsigned defaultArgIndex);

  bool isNull() const { return loc.isNull(); }
  explicit operator bool() const { return !isNull(); }
  
  bool hasDecl() const { return loc.is<ValueDecl *>(); }
  bool hasClosureExpr() const;
  bool hasAutoClosureExpr() const;
  bool hasFuncDecl() const;

  ValueDecl *getDecl() const { return loc.get<ValueDecl *>(); }
  AbstractClosureExpr *getAbstractClosureExpr() const {
    return loc.dyn_cast<AbstractClosureExpr *>();
  }
  ClosureExpr *getClosureExpr() const;
  AutoClosureExpr *getAutoClosureExpr() const;
  FuncDecl *getFuncDecl() const;
  AbstractFunctionDecl *getAbstractFunctionDecl() const;
  
  llvm::Optional<AnyFunctionRef> getAnyFunctionRef() const;
  
  SILLocation getAsRegularLocation() const;

  enum class ManglingKind {
    Default,
    DynamicThunk,
    SwiftDispatchThunk,
  };

  /// Produce a mangled form of this constant.
  std::string mangle(ManglingKind MKind = ManglingKind::Default) const;

  /// True if the SILDeclRef references a function.
  bool isFunc() const {
    return kind == Kind::Func;
  }

  /// True if the SILDeclRef references a setter function.
  bool isSetter() const;

  /// True if the SILDeclRef references a constructor entry point.
  bool isConstructor() const {
    return kind == Kind::Allocator || kind == Kind::Initializer;
  }
  /// True if the SILDeclRef references a destructor entry point.
  bool isDestructor() const {
    return kind == Kind::Destroyer || kind == Kind::Deallocator;
  }
  /// True if the SILDeclRef references an enum entry point.
  bool isEnumElement() const {
    return kind == Kind::EnumElement;
  }
  /// True if the SILDeclRef references a global variable accessor.
  bool isGlobal() const {
    return kind == Kind::GlobalAccessor || kind == Kind::GlobalGetter;
  }
  /// True if the SILDeclRef references the generator for a default argument of
  /// a function.
  bool isDefaultArgGenerator() const {
    return kind == Kind::DefaultArgGenerator;
  }
  /// True if the SILDeclRef references the initializer for a stored property
  /// of a nominal type.
  bool isStoredPropertyInitializer() const {
    return kind == Kind::StoredPropertyInitializer;
  }

  /// True if the SILDeclRef references the ivar initializer or deinitializer of
  /// a class.
  bool isIVarInitializerOrDestroyer() const {
    return kind == Kind::IVarInitializer || kind == Kind::IVarDestroyer;
  }

  /// \brief True if the function should be treated as transparent.
  bool isTransparent() const;
  /// \brief True if the function should have its body serialized.
  IsSerialized_t isSerialized() const;
  /// \brief True if the function has noinline attribute.
  bool isNoinline() const;
  /// \brief True if the function has __always inline attribute.
  bool isAlwaysInline() const;
  
  /// \return True if the function has an effects attribute.
  bool hasEffectsAttribute() const;

  /// \return the effects kind of the function.
  EffectsKind getEffectsAttribute() const;

  /// \brief Return the expected linkage of this declaration.
  SILLinkage getLinkage(ForDefinition_t forDefinition) const;

  /// \brief Return the hash code for the SIL declaration.
  llvm::hash_code getHashCode() const {
    return llvm::hash_combine(loc.getOpaqueValue(),
                              static_cast<int>(kind),
                              Expansion, isCurried,
                              isForeign, isDirectReference,
                              defaultArgIndex);
  }

  bool operator==(SILDeclRef rhs) const {
    return loc.getOpaqueValue() == rhs.loc.getOpaqueValue()
      && kind == rhs.kind
      && Expansion == rhs.Expansion
      && isCurried == rhs.isCurried
      && isForeign == rhs.isForeign
      && isDirectReference == rhs.isDirectReference
      && defaultArgIndex == rhs.defaultArgIndex;
  }
  bool operator!=(SILDeclRef rhs) const {
    return !(*this == rhs);
  }
  
  void print(llvm::raw_ostream &os) const;
  void dump() const;

  unsigned getParameterListCount() const;

  ResilienceExpansion getResilienceExpansion() const {
    return ResilienceExpansion(Expansion);
  }
  
  // Returns the SILDeclRef for an entity at a shallower uncurry level.
  SILDeclRef asCurried(bool curried = true) const {
    assert(!isCurried && "can't safely go to deeper uncurry level");
    // Curry thunks are never foreign.
    bool willBeForeign = isForeign && !curried;
    bool willBeDirect = isDirectReference;
    return SILDeclRef(loc.getOpaqueValue(), kind, Expansion,
                      curried, willBeDirect, willBeForeign,
                      defaultArgIndex);
  }
  
  /// Returns the foreign (or native) entry point corresponding to the same
  /// decl.
  SILDeclRef asForeign(bool foreign = true) const {
    assert(!isCurried);
    return SILDeclRef(loc.getOpaqueValue(), kind, Expansion,
                      isCurried, isDirectReference, foreign, defaultArgIndex);
  }
  
  SILDeclRef asDirectReference(bool direct = true) const {
    SILDeclRef r = *this;
    // The 'direct' distinction only makes sense for curry thunks.
    if (r.isCurried)
      r.isDirectReference = direct;
    return r;
  }

  /// True if the decl ref references a thunk from a natively foreign
  /// declaration to Swift calling convention.
  bool isForeignToNativeThunk() const;
  
  /// True if the decl ref references a thunk from a natively Swift declaration
  /// to foreign C or ObjC calling convention.
  bool isNativeToForeignThunk() const;

  /// True if the decl ref references a method which introduces a new vtable
  /// entry.
  bool requiresNewVTableEntry() const;

  /// Return a SILDeclRef to the declaration overridden by this one, or
  /// a null SILDeclRef if there is no override.
  SILDeclRef getOverridden() const;
  
  /// Return a SILDeclRef to the declaration whose vtable entry this declaration
  /// overrides. This may be different from "getOverridden" because some
  /// declarations do not always have vtable entries.
  SILDeclRef getNextOverriddenVTableEntry() const;

  /// True if the referenced entity is some kind of thunk.
  bool isThunk() const;

  /// True if the referenced entity is emitted by Swift on behalf of the Clang
  /// importer.
  bool isClangImported() const;

  /// True if the referenced entity is emitted by Clang on behalf of the Clang
  /// importer.
  bool isClangGenerated() const;
  static bool isClangGenerated(ClangNode node);

  bool isImplicit() const;

  /// Return the scope in which the parent class of a method (i.e. class
  /// containing this declaration) can be subclassed, returning NotApplicable if
  /// this is not a method, there is no such class, or the class cannot be
  /// subclassed.
  SubclassScope getSubclassScope() const;

private:
  friend struct llvm::DenseMapInfo<swift::SILDeclRef>;
  /// Produces a SILDeclRef from an opaque value.
  explicit SILDeclRef(void *opaqueLoc,
                      Kind kind,
                      unsigned rawExpansion,
                      bool isCurried,
                      bool isDirectReference,
                      bool isForeign,
                      unsigned defaultArgIndex)
    : loc(Loc::getFromOpaqueValue(opaqueLoc)),
      kind(kind),
      Expansion(rawExpansion),
      isCurried(isCurried),
      isForeign(isForeign), isDirectReference(isDirectReference),
      defaultArgIndex(defaultArgIndex)
  {}

};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, SILDeclRef C) {
  C.print(OS);
  return OS;
}

} // end swift namespace

namespace llvm {

// DenseMap key support for SILDeclRef.
template<> struct DenseMapInfo<swift::SILDeclRef> {
  using SILDeclRef = swift::SILDeclRef;
  using Kind = SILDeclRef::Kind;
  using Loc = SILDeclRef::Loc;
  using ResilienceExpansion = swift::ResilienceExpansion;
  using PointerInfo = DenseMapInfo<void*>;
  using UnsignedInfo = DenseMapInfo<unsigned>;

  static SILDeclRef getEmptyKey() {
    return SILDeclRef(PointerInfo::getEmptyKey(), Kind::Func,
                      0, false, false, false, 0);
  }
  static SILDeclRef getTombstoneKey() {
    return SILDeclRef(PointerInfo::getTombstoneKey(), Kind::Func,
                      0, false, false, false, 0);
  }
  static unsigned getHashValue(swift::SILDeclRef Val) {
    unsigned h1 = PointerInfo::getHashValue(Val.loc.getOpaqueValue());
    unsigned h2 = UnsignedInfo::getHashValue(unsigned(Val.kind));
    unsigned h3 = (Val.kind == Kind::DefaultArgGenerator)
                    ? UnsignedInfo::getHashValue(Val.defaultArgIndex)
                    : UnsignedInfo::getHashValue(Val.isCurried);
    unsigned h4 = UnsignedInfo::getHashValue(Val.isForeign);
    unsigned h5 = UnsignedInfo::getHashValue(Val.isDirectReference);
    return h1 ^ (h2 << 4) ^ (h3 << 9) ^ (h4 << 7) ^ (h5 << 11);
  }
  static bool isEqual(swift::SILDeclRef const &LHS,
                      swift::SILDeclRef const &RHS) {
    return LHS == RHS;
  }
};

} // end llvm namespace

#endif
