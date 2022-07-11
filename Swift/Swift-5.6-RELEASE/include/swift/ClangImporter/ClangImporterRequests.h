//===--- ClangImporterRequests.h - Clang Importer Requests ------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file defines clang-importer requests.
//
//===----------------------------------------------------------------------===//
#ifndef SWIFT_CLANG_IMPORTER_REQUESTS_H
#define SWIFT_CLANG_IMPORTER_REQUESTS_H

#include "swift/AST/SimpleRequest.h"
#include "swift/AST/ASTTypeIDs.h"
#include "swift/AST/EvaluatorDependencies.h"
#include "swift/AST/FileUnit.h"
#include "swift/AST/Identifier.h"
#include "swift/AST/NameLookup.h"
#include "swift/Basic/Statistic.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/TinyPtrVector.h"

namespace swift {
class Decl;
class DeclName;
class EnumDecl;

/// The input type for a clang direct lookup request.
struct ClangDirectLookupDescriptor final {
  Decl *decl;
  const clang::Decl *clangDecl;
  DeclName name;

  ClangDirectLookupDescriptor(Decl *decl, const clang::Decl *clangDecl,
                              DeclName name)
      : decl(decl), clangDecl(clangDecl), name(name) {}

  friend llvm::hash_code hash_value(const ClangDirectLookupDescriptor &desc) {
    return llvm::hash_combine(desc.name, desc.decl, desc.clangDecl);
  }

  friend bool operator==(const ClangDirectLookupDescriptor &lhs,
                         const ClangDirectLookupDescriptor &rhs) {
    return lhs.name == rhs.name && lhs.decl == rhs.decl &&
           lhs.clangDecl == rhs.clangDecl;
  }

  friend bool operator!=(const ClangDirectLookupDescriptor &lhs,
                         const ClangDirectLookupDescriptor &rhs) {
    return !(lhs == rhs);
  }
};

void simple_display(llvm::raw_ostream &out,
                    const ClangDirectLookupDescriptor &desc);
SourceLoc extractNearestSourceLoc(const ClangDirectLookupDescriptor &desc);

/// This matches SwiftLookupTable::SingleEntry;
using SingleEntry = llvm::PointerUnion<clang::NamedDecl *, clang::MacroInfo *,
                                       clang::ModuleMacro *>;
/// Uses the appropriate SwiftLookupTable to find a set of clang decls given
/// their name.
class ClangDirectLookupRequest
    : public SimpleRequest<ClangDirectLookupRequest,
                           SmallVector<SingleEntry, 4>(
                               ClangDirectLookupDescriptor),
                           RequestFlags::Uncached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  SmallVector<SingleEntry, 4> evaluate(Evaluator &evaluator,
                                       ClangDirectLookupDescriptor desc) const;
};

/// The input type for a namespace member lookup request.
struct CXXNamespaceMemberLookupDescriptor final {
  EnumDecl *namespaceDecl;
  DeclName name;

  CXXNamespaceMemberLookupDescriptor(EnumDecl *namespaceDecl, DeclName name)
      : namespaceDecl(namespaceDecl), name(name) {
    assert(isa<clang::NamespaceDecl>(namespaceDecl->getClangDecl()));
  }

  friend llvm::hash_code
  hash_value(const CXXNamespaceMemberLookupDescriptor &desc) {
    return llvm::hash_combine(desc.name, desc.namespaceDecl);
  }

  friend bool operator==(const CXXNamespaceMemberLookupDescriptor &lhs,
                         const CXXNamespaceMemberLookupDescriptor &rhs) {
    return lhs.name == rhs.name && lhs.namespaceDecl == rhs.namespaceDecl;
  }

  friend bool operator!=(const CXXNamespaceMemberLookupDescriptor &lhs,
                         const CXXNamespaceMemberLookupDescriptor &rhs) {
    return !(lhs == rhs);
  }
};

void simple_display(llvm::raw_ostream &out,
                    const CXXNamespaceMemberLookupDescriptor &desc);
SourceLoc
extractNearestSourceLoc(const CXXNamespaceMemberLookupDescriptor &desc);

/// Uses ClangDirectLookup to find a named member inside of the given namespace.
class CXXNamespaceMemberLookup
    : public SimpleRequest<CXXNamespaceMemberLookup,
                           TinyPtrVector<ValueDecl *>(
                               CXXNamespaceMemberLookupDescriptor),
                           RequestFlags::Uncached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  TinyPtrVector<ValueDecl *>
  evaluate(Evaluator &evaluator, CXXNamespaceMemberLookupDescriptor desc) const;
};

/// The input type for a record member lookup request.
struct ClangRecordMemberLookupDescriptor final {
  StructDecl *recordDecl;
  DeclName name;

  ClangRecordMemberLookupDescriptor(StructDecl *recordDecl, DeclName name)
      : recordDecl(recordDecl), name(name) {
    assert(isa<clang::RecordDecl>(recordDecl->getClangDecl()));
  }

  friend llvm::hash_code
  hash_value(const ClangRecordMemberLookupDescriptor &desc) {
    return llvm::hash_combine(desc.name, desc.recordDecl);
  }

  friend bool operator==(const ClangRecordMemberLookupDescriptor &lhs,
                         const ClangRecordMemberLookupDescriptor &rhs) {
    return lhs.name == rhs.name && lhs.recordDecl == rhs.recordDecl;
  }

  friend bool operator!=(const ClangRecordMemberLookupDescriptor &lhs,
                         const ClangRecordMemberLookupDescriptor &rhs) {
    return !(lhs == rhs);
  }
};

void simple_display(llvm::raw_ostream &out,
                    const ClangRecordMemberLookupDescriptor &desc);
SourceLoc
extractNearestSourceLoc(const ClangRecordMemberLookupDescriptor &desc);

/// Uses ClangDirectLookup to find a named member inside of the given record.
class ClangRecordMemberLookup
    : public SimpleRequest<ClangRecordMemberLookup,
                           TinyPtrVector<ValueDecl *>(
                               ClangRecordMemberLookupDescriptor),
                           RequestFlags::Uncached> {
public:
  using SimpleRequest::SimpleRequest;

private:
  friend SimpleRequest;

  // Evaluation.
  TinyPtrVector<ValueDecl *>
  evaluate(Evaluator &evaluator, ClangRecordMemberLookupDescriptor desc) const;
};

#define SWIFT_TYPEID_ZONE ClangImporter
#define SWIFT_TYPEID_HEADER "swift/ClangImporter/ClangImporterTypeIDZone.def"
#include "swift/Basic/DefineTypeIDZone.h"
#undef SWIFT_TYPEID_ZONE
#undef SWIFT_TYPEID_HEADER

// Set up reporting of evaluated requests.
template<typename Request>
void reportEvaluatedRequest(UnifiedStatsReporter &stats,
                            const Request &request);

#define SWIFT_REQUEST(Zone, RequestType, Sig, Caching, LocOptions)             \
  template <>                                                                  \
  inline void reportEvaluatedRequest(UnifiedStatsReporter &stats,              \
                                     const RequestType &request) {             \
    ++stats.getFrontendCounters().RequestType;                                 \
  }
#include "swift/ClangImporter/ClangImporterTypeIDZone.def"
#undef SWIFT_REQUEST

} // end namespace swift

#endif // SWIFT_CLANG_IMPORTER_REQUESTS_H

