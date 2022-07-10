//===--- ConformanceLookupTable - Conformance Lookup Table ------*- C++ -*-===//
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
//  This file defines the ConformanceLookupTable class, which manages protocol
//  conformances for a given nominal type. Most clients should not access this
//  table directly; rather, they should go through the NominalTypeDecl or
//  DeclContext entry points.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_CONFORMANCE_LOOKUP_TABLE_H
#define SWIFT_AST_CONFORMANCE_LOOKUP_TABLE_H

#include "swift/AST/DeclContext.h"
#include "swift/AST/TypeLoc.h"
#include "swift/Basic/LLVM.h"
#include "swift/Basic/SourceLoc.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SetVector.h"
#include <unordered_map>

namespace swift {

class ExtensionDecl;
class ModuleDecl;

/// Keeps track of the protocols to which a particular nominal type conforms.
///
/// This table is a lower-level detail that clients should generally not
/// access directly. Rather, one should use the protocol- and
/// conformance-centric entry points in \c NominalTypeDecl and \c DeclContext.
class ConformanceLookupTable {
  /// Describes the stage at which a particular nominal type or
  /// extension's conformances has been processed.
  enum class ConformanceStage : unsigned char {
    /// The explicit conformances have been recorded in the lookup table.
    RecordedExplicit,

    /// Conformances from the superclass have been inherited.
    Inherited,

    /// The explicit conformances have been expanded out to include
    /// the conformances they imply.
    ExpandedImplied,

    /// The complete set of conformances have been fully resolved to
    /// assign conformances, diagnose conflicts, etc.
    Resolved,
  };

  /// The number of conformance stages.
  static const unsigned NumConformanceStages = 4;

  /// An entry in the last-processed list, which contains a pointer to
  /// the last extension that was processed at a particular stage (or
  /// nullptr if no extensions have been processed) and indicates
  /// whether the nominal type declaration itself has been processed
  /// at that stage.
  typedef llvm::PointerIntPair<ExtensionDecl *, 1, bool> LastProcessedEntry;

  /// Array indicating how far we have gotten in processing each
  /// nominal type and list of extensions for each stage of
  /// conformance checking.
  ///
  /// Uses std::unordered_map instead of DenseMap so that stable interior
  /// references can be taken.
  std::unordered_map<NominalTypeDecl *,
                     std::array<LastProcessedEntry, NumConformanceStages>>
  LastProcessed;
  
  /// The list of parsed extension declarations that have been delayed because
  /// no resolver was available at the time.
  ///
  /// FIXME: This is insane. The resolver should be there or we shouldn't
  /// have parsed extensions.
  llvm::SetVector<ExtensionDecl *> DelayedExtensionDecls[NumConformanceStages];

  struct ConformanceEntry;

  /// Describes the "source" of a conformance, indicating where the
  /// conformance came from.
  class ConformanceSource {
    llvm::PointerIntPair<void *, 2, ConformanceEntryKind> Storage;

    ConformanceSource(void *ptr, ConformanceEntryKind kind) 
      : Storage(ptr, kind) { }

  public:
    /// Create an inherited conformance.
    ///
    /// The given class will have an inherited conformance for the
    /// requested protocol.
    static ConformanceSource forInherited(ClassDecl *classDecl) {
      return ConformanceSource(classDecl, ConformanceEntryKind::Inherited);
    }

    /// Create an explicit conformance.
    ///
    /// The given declaration context (nominal type declaration or
    /// extension thereof) explicitly specifies conformance to the
    /// protocol.
    static ConformanceSource forExplicit(DeclContext *dc) {
      return ConformanceSource(dc, ConformanceEntryKind::Explicit);
    }

    /// Create an implied conformance.
    ///
    /// Conformance to the protocol is implied by the given
    /// conformance entry. The chain of conformance entries will
    /// eventually terminate in a non-implied conformance.
    static ConformanceSource forImplied(ConformanceEntry *entry) {
      return ConformanceSource(entry, ConformanceEntryKind::Implied);
    }

    /// Create a synthesized conformance.
    ///
    /// The given nominal type declaration will get a synthesized
    /// conformance to the requested protocol.
    static ConformanceSource forSynthesized(NominalTypeDecl *typeDecl) {
      return ConformanceSource(typeDecl, ConformanceEntryKind::Synthesized);
    }

    /// Retrieve the kind of conformance formed from this source.
    ConformanceEntryKind getKind() const { return Storage.getInt(); }

    /// Retrieve kind of the conformance for ranking purposes.
    ///
    /// The only difference between the ranking kind and the kind is
    /// that implied conformances originating from a synthesized
    /// conformance are considered to be synthesized (which has a
    /// lower ranking).
    ConformanceEntryKind getRankingKind() const {
      switch (auto kind = getKind()) {
      case ConformanceEntryKind::Explicit:
      case ConformanceEntryKind::Inherited:
      case ConformanceEntryKind::Synthesized:
        return kind;

      case ConformanceEntryKind::Implied:
        return (getImpliedSource()->getDeclaredConformance()->getKind()
                  == ConformanceEntryKind::Synthesized)
                 ? ConformanceEntryKind::Synthesized
                 : ConformanceEntryKind::Implied;
      }
    }

    /// For an inherited conformance, retrieve the class declaration
    /// for the inheriting class.
    ClassDecl *getInheritingClass() const {
      assert(getKind() == ConformanceEntryKind::Inherited);
      return static_cast<ClassDecl *>(Storage.getPointer());      
    }

    /// For an explicit conformance, retrieve the declaration context
    /// that specifies the conformance.
    DeclContext *getExplicitDeclContext() const {
      assert(getKind() == ConformanceEntryKind::Explicit);
      return static_cast<DeclContext *>(Storage.getPointer());      
    }

    /// For a synthesized conformance, retrieve the nominal type decl
    /// that will receive the conformance.
    ConformanceEntry *getImpliedSource() const {
      assert(getKind() == ConformanceEntryKind::Implied);
      return static_cast<ConformanceEntry *>(Storage.getPointer());
    }

    /// For a synthesized conformance, retrieve the nominal type decl
    /// that will receive the conformance.
    NominalTypeDecl *getSynthesizedDecl() const {
      assert(getKind() == ConformanceEntryKind::Synthesized);
      return static_cast<NominalTypeDecl *>(Storage.getPointer());
    }

    /// Get the declaration context that this conformance will be
    /// associated with.
    DeclContext *getDeclContext() const;
  };

  /// An entry in the conformance table.
  struct ConformanceEntry {
    /// The source location within the current context where the
    /// protocol conformance was specified.
    SourceLoc Loc;

    /// If this conformance entry has been superseded, the conformance
    /// that superseded it.
    ConformanceEntry *SupersededBy = nullptr;

    /// The source of this conformance entry , which is either a
    /// DeclContext (for an explicitly-specified conformance) or a
    /// link to the conformance that implied this conformance.
    ConformanceSource Source;

    /// Either the protocol to be resolved or the resolved protocol conformance.
    llvm::PointerUnion<ProtocolDecl *, ProtocolConformance *> Conformance;

    ConformanceEntry(SourceLoc loc, ProtocolDecl *protocol,
                     ConformanceSource source)
      : Loc(loc), Source(source), Conformance(protocol) { }

    /// Retrieve the location at which this conformance was declared
    /// or synthesized.
    SourceLoc getLoc() const { return Loc; }

    /// Whether this conformance is already "fixed" and cannot be superseded.
    bool isFixed() const {
      // If a conformance has been assigned, it cannot be superseded.
      if (getConformance())
        return true;

      // Otherwise, only inherited conformances are fixed.
      switch (getKind()) {
      case ConformanceEntryKind::Explicit:
      case ConformanceEntryKind::Implied:
      case ConformanceEntryKind::Synthesized:
        return false;

      case ConformanceEntryKind::Inherited:
        return true;
      }
    }

    /// Whether this protocol conformance was superseded by another
    /// conformance.
    bool isSuperseded() const { return SupersededBy != nullptr; }

    /// Retrieve the conformance entry that superseded this one.
    ConformanceEntry *getSupersededBy() const { return SupersededBy; }

    /// Note that this conformance entry was superseded by the given
    /// entry.
    void markSupersededBy(ConformanceLookupTable &table,
                          ConformanceEntry *entry,
                          bool diagnose);

    /// Determine the kind of conformance.
    ConformanceEntryKind getKind() const {
      return Source.getKind();
    }

    /// Determine the kind of conformance for ranking purposes.
    ConformanceEntryKind getRankingKind() const {
      return Source.getRankingKind();
    }

    /// Retrieve the declaration context associated with this conformance.
    DeclContext *getDeclContext() const {
      return Source.getDeclContext();
    }

    /// Retrieve the protocol to which this conformance entry refers.
    ProtocolDecl *getProtocol() const;

    /// Retrieve the conformance for this entry, if it has one.
    ProtocolConformance *getConformance() const {
      return Conformance.dyn_cast<ProtocolConformance *>();
    }

    /// Retrieve the conformance entry where the conformance was
    /// declared.
    const ConformanceEntry *getDeclaredConformance() const {
      if (Source.getKind() == ConformanceEntryKind::Implied)
        return Source.getImpliedSource()->getDeclaredConformance();

      return this;
    }

    /// Retrieve the source location of the place where the
    /// conformance was introduced, e.g., an explicit conformance or
    /// the point at which a subclass inherits a conformance from its
    /// superclass.
    SourceLoc getDeclaredLoc() const {
      if (Source.getKind() == ConformanceEntryKind::Implied)
        return Source.getImpliedSource()->getDeclaredLoc();

      return Loc;
    }

    // Only allow allocation of conformance entries using the
    // allocator in ASTContext.
    void *operator new(size_t Bytes, ASTContext &C,
                       unsigned Alignment = alignof(ConformanceEntry));

    LLVM_ATTRIBUTE_DEPRECATED(
      void dump() const LLVM_ATTRIBUTE_USED,
        "only for use within the debugger");
    void dump(raw_ostream &os, unsigned indent = 0) const;
  };

  /// The set of conformance entries for a given protocol.
  typedef llvm::TinyPtrVector<ConformanceEntry *> ConformanceEntries;

  /// The type of the internal conformance table.
  typedef llvm::MapVector<ProtocolDecl *, ConformanceEntries> ConformanceTable;

  /// The conformance table.
  ConformanceTable Conformances;

  typedef llvm::SmallVector<ProtocolDecl *, 2> ProtocolList;

  /// List of all of the protocols to which a given context declares
  /// conformance, both explicitly and implicitly.
  llvm::MapVector<DeclContext *, SmallVector<ConformanceEntry *, 4>>
    AllConformances;

  /// The complete set of diagnostics about erroneously superseded
  /// protocol conformances.
  llvm::SmallDenseMap<DeclContext *, std::vector<ConformanceEntry *> >
    AllSupersededDiagnostics;

  /// Associates a conforming decl to its protocol conformance decls.
  llvm::DenseMap<const ValueDecl *, llvm::TinyPtrVector<ValueDecl *>>
    ConformingDeclMap;

  /// Indicates whether we are visiting the superclass.
  bool VisitingSuperclass = false;

  /// Add a protocol.
  bool addProtocol(NominalTypeDecl *nominal,
                   ProtocolDecl *protocol, SourceLoc loc,
                   ConformanceSource source);

  /// Add the protocols from the given list.
  void addProtocols(NominalTypeDecl *nominal, ArrayRef<TypeLoc> inherited,
                    ConformanceSource source, LazyResolver *resolver);

  /// Expand the implied conformances for the given DeclContext.
  void expandImpliedConformances(NominalTypeDecl *nominal, DeclContext *dc,
                                 LazyResolver *resolver);

  /// A three-way ordering
  enum class Ordering {
    Before,
    Equivalent,
    After,
  };

  /// Determine whether the first conformance entry supersedes the
  /// second when determining where to place the conformance.
  ///
  /// \param diagnoseSuperseded When one entry is better than another,
  /// whether to diagnose the problem as an error.
  Ordering compareConformances(ConformanceEntry *lhs, ConformanceEntry *rhs,
                               bool &diagnoseSuperseded);

  /// Resolve the set of conformances that will be generated for the
  /// given protocol.
  ///
  /// \returns true if any conformance entries were superseded by this
  /// operation.
  bool resolveConformances(NominalTypeDecl *nominal,
                           ProtocolDecl *protocol,
                           LazyResolver *resolver);

  /// Retrieve the declaration context that provides the
  /// (non-inherited) conformance described by the given conformance
  /// entry.
  DeclContext *getConformingContext(NominalTypeDecl *nominal,
                                    LazyResolver *resolver,
                                    ConformanceEntry *entry);

  /// Resolve the given conformance entry to an actual protocol conformance.
  ProtocolConformance *getConformance(NominalTypeDecl *nominal,
                                      LazyResolver *resolver,
                                      ConformanceEntry *entry);

  /// Enumerate each of the unhandled contexts (nominal type
  /// declaration or extension) within the given stage.
  ///
  /// \param stage The stage to process. Note that it is up to the
  /// caller to ensure that prior stages have already been handled.
  ///
  /// \param nominalFunc Function object to be invoked when the
  /// nominal type declaration itself needs to be processed. It takes
  /// the nominal type declaration and its result is ignored.
  ///
  /// \param extensionFunc Function object to be invoked with a given
  /// extension needs to be processed. It takes the extension as an
  /// argument and its result is ignored.
  template<typename NominalFunc, typename ExtensionFunc>
  void forEachInStage(ConformanceStage stage,
                      NominalTypeDecl *nominal,
                      LazyResolver *resolver,
                      NominalFunc nominalFunc,
                      ExtensionFunc extensionFunc);

  /// Inherit the conformances from the given superclass into the
  /// given nominal type.
  ///
  /// \param classDecl The class into which the conformances will be
  /// inherited.
  ///
  /// \param superclassDecl The superclass from which the conformances
  /// will be inherited.
  ///
  /// \param superclassExt If non-null, the superclass extension from
  /// which conformances will be inherited. If null, the conformances
  /// on the superclass declaration itself will be inherited.
  void inheritConformances(ClassDecl *classDecl, 
                           ClassDecl *superclassDecl,
                           ExtensionDecl *superclassExt,
                           LazyResolver *resolver);

  /// Update a lookup table with conformances from newly-added extensions.
  void updateLookupTable(NominalTypeDecl *nominal, ConformanceStage stage,
                         LazyResolver *resolver);

  /// Load all of the protocol conformances for the given (serialized)
  /// declaration context.
  void loadAllConformances(NominalTypeDecl *nominal,
                           DeclContext *dc,
                           ArrayRef<ProtocolConformance *> conformances);

public:
  /// Create a new conformance lookup table.
  ConformanceLookupTable(ASTContext &ctx, NominalTypeDecl *nominal,
                         LazyResolver *resolver);

  /// Destroy the conformance table.
  void destroy();

  /// Add a synthesized conformance to the lookup table.
  void addSynthesizedConformance(NominalTypeDecl *nominal,
                                 ProtocolDecl *protocol);

  /// Register an externally-supplied protocol conformance.
  void registerProtocolConformance(ProtocolConformance *conformance);

  /// Look for conformances to the given protocol.
  ///
  /// \param conformances Will be populated with the set of protocol
  /// conformances found for this protocol and nominal type.
  ///
  /// \returns true if any conformances were found. 
  bool lookupConformance(ModuleDecl *module,
                         NominalTypeDecl *nominal,
                         ProtocolDecl *protocol, 
                         LazyResolver *resolver,
                         SmallVectorImpl<ProtocolConformance *> &conformances);

  /// Look for all of the conformances within the given declaration context.
  void lookupConformances(NominalTypeDecl *nominal,
                          DeclContext *dc,
                          LazyResolver *resolver,
                          ConformanceLookupKind lookupKind,
                          SmallVectorImpl<ProtocolDecl *> *protocols,
                          SmallVectorImpl<ProtocolConformance *> *conformances,
                          SmallVectorImpl<ConformanceDiagnostic> *diagnostics);

  /// Retrieve the complete set of protocols to which this nominal
  /// type conforms.
  void getAllProtocols(NominalTypeDecl *nominal,
                       LazyResolver *resolver,
                       SmallVectorImpl<ProtocolDecl *> &scratch);

  /// Retrieve the complete set of protocol conformances for this
  /// nominal type.
  void getAllConformances(NominalTypeDecl *nominal,
                          LazyResolver *resolver,
                          bool sorted,
                          SmallVectorImpl<ProtocolConformance *> &scratch);

  /// Retrieve the protocols that would be implicitly synthesized.
  /// FIXME: This is a hack, because it's the wrong question to ask. It
  /// skips over the possibility that there is an explicit conformance
  /// somewhere.
  void getImplicitProtocols(NominalTypeDecl *nominal,
                            SmallVectorImpl<ProtocolDecl *> &protocols);

  /// Returns the protocol requirements that \c Member conforms to.
  ArrayRef<ValueDecl *>
  getSatisfiedProtocolRequirementsForMember(const ValueDecl *member,
                                            NominalTypeDecl *nominal,
                                            LazyResolver *resolver,
                                            bool sorted);

  // Only allow allocation of conformance lookup tables using the
  // allocator in ASTContext or by doing a placement new.
  void *operator new(size_t Bytes, ASTContext &C,
                     unsigned Alignment = alignof(ConformanceLookupTable));

  void *operator new(size_t Bytes, void *Mem) {
    assert(Mem);
    return Mem;
  }

  LLVM_ATTRIBUTE_DEPRECATED(
      void dump() const LLVM_ATTRIBUTE_USED,
      "only for use within the debugger");
  void dump(raw_ostream &os) const;

  /// Compare two protocol conformances to place them in some canonical order.
  static int compareProtocolConformances(ProtocolConformance * const *lhsPtr,
                                         ProtocolConformance * const *rhsPtr);
};

}

#endif /* SWIFT_AST_CONFORMANCE_LOOKUP_TABLE_H */
