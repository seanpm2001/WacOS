//===--- NameLookupRequests.cpp - Name Lookup Requests --------------------===//
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

#include "swift/AST/NameLookup.h"
#include "swift/AST/NameLookupRequests.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/GenericParamList.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/Evaluator.h"
#include "swift/AST/Module.h"
#include "swift/AST/SourceFile.h"
#include "swift/ClangImporter/ClangImporterRequests.h"
#include "swift/Subsystems.h"

using namespace swift;

namespace swift {
// Implement the name lookup type zone.
#define SWIFT_TYPEID_ZONE NameLookup
#define SWIFT_TYPEID_HEADER "swift/AST/NameLookupTypeIDZone.def"
#include "swift/Basic/ImplementTypeIDZone.h"
#undef SWIFT_TYPEID_ZONE
#undef SWIFT_TYPEID_HEADER
}

//----------------------------------------------------------------------------//
// Referenced inherited decls computation.
//----------------------------------------------------------------------------//

SourceLoc InheritedDeclsReferencedRequest::getNearestLoc() const {
  const auto &storage = getStorage();
  auto &typeLoc = getInheritedTypeLocAtIndex(std::get<0>(storage),
                                             std::get<1>(storage));
  return typeLoc.getLoc();
}

//----------------------------------------------------------------------------//
// Superclass declaration computation.
//----------------------------------------------------------------------------//
void SuperclassDeclRequest::diagnoseCycle(DiagnosticEngine &diags) const {
  // FIXME: Improve this diagnostic.
  auto nominalDecl = std::get<0>(getStorage());
  diags.diagnose(nominalDecl, diag::circular_class_inheritance,
                 nominalDecl->getName());
}

void SuperclassDeclRequest::noteCycleStep(DiagnosticEngine &diags) const {
  auto decl = std::get<0>(getStorage());
  diags.diagnose(decl, diag::kind_declname_declared_here,
                 decl->getDescriptiveKind(), decl->getName());
}

Optional<ClassDecl *> SuperclassDeclRequest::getCachedResult() const {
  auto nominalDecl = std::get<0>(getStorage());

  if (auto *classDecl = dyn_cast<ClassDecl>(nominalDecl))
    if (classDecl->LazySemanticInfo.SuperclassDecl.getInt())
      return classDecl->LazySemanticInfo.SuperclassDecl.getPointer();

  if (auto *protocolDecl = dyn_cast<ProtocolDecl>(nominalDecl))
    if (protocolDecl->LazySemanticInfo.SuperclassDecl.getInt())
      return protocolDecl->LazySemanticInfo.SuperclassDecl.getPointer();

  return None;
}

void SuperclassDeclRequest::cacheResult(ClassDecl *value) const {
  auto nominalDecl = std::get<0>(getStorage());

  if (auto *classDecl = dyn_cast<ClassDecl>(nominalDecl))
    classDecl->LazySemanticInfo.SuperclassDecl.setPointerAndInt(value, true);

  if (auto *protocolDecl = dyn_cast<ProtocolDecl>(nominalDecl))
    protocolDecl->LazySemanticInfo.SuperclassDecl.setPointerAndInt(value, true);
}

//----------------------------------------------------------------------------//
// InheritedProtocolsRequest computation.
//----------------------------------------------------------------------------//

Optional<ArrayRef<ProtocolDecl *>>
InheritedProtocolsRequest::getCachedResult() const {
  auto proto = std::get<0>(getStorage());
  if (!proto->areInheritedProtocolsValid())
    return None;

  return proto->InheritedProtocols;
}

void InheritedProtocolsRequest::cacheResult(ArrayRef<ProtocolDecl *> PDs) const {
  auto proto = std::get<0>(getStorage());
  proto->InheritedProtocols = PDs;
  proto->setInheritedProtocolsValid();
}

void InheritedProtocolsRequest::writeDependencySink(
    evaluator::DependencyCollector &tracker,
    ArrayRef<ProtocolDecl *> PDs) const {
  for (auto *parentProto : PDs) {
    tracker.addPotentialMember(parentProto);
  }
}

//----------------------------------------------------------------------------//
// Missing designated initializers computation
//----------------------------------------------------------------------------//

Optional<bool> HasMissingDesignatedInitializersRequest::getCachedResult() const {
  auto classDecl = std::get<0>(getStorage());
  return classDecl->getCachedHasMissingDesignatedInitializers();
}

void HasMissingDesignatedInitializersRequest::cacheResult(bool result) const {
  auto classDecl = std::get<0>(getStorage());
  classDecl->setHasMissingDesignatedInitializers(result);
}

bool
HasMissingDesignatedInitializersRequest::evaluate(Evaluator &evaluator,
                                           ClassDecl *subject) const {
  // Short-circuit and check for the attribute here.
  if (subject->getAttrs().hasAttribute<HasMissingDesignatedInitializersAttr>())
    return true;

  AccessScope scope =
    subject->getFormalAccessScope(/*useDC*/nullptr,
                                  /*treatUsableFromInlineAsPublic*/true);
  // This flag only makes sense for public types that will be written in the
  // module.
  if (!scope.isPublic())
    return false;

  auto constructors = subject->lookupDirect(DeclBaseName::createConstructor());
  return llvm::any_of(constructors, [&](ValueDecl *decl) {
    auto init = cast<ConstructorDecl>(decl);
    if (!init->isDesignatedInit())
      return false;
    AccessScope scope =
        init->getFormalAccessScope(/*useDC*/nullptr,
                                   /*treatUsableFromInlineAsPublic*/true);
    return !scope.isPublic();
  });
}

//----------------------------------------------------------------------------//
// Extended nominal computation.
//----------------------------------------------------------------------------//

Optional<NominalTypeDecl *> ExtendedNominalRequest::getCachedResult() const {
  // Note: if we fail to compute any nominal declaration, it's considered
  // a cache miss. This allows us to recompute the extended nominal types
  // during extension binding.
  // This recomputation is also what allows you to extend types defined inside
  // other extensions, regardless of source file order. See \c bindExtensions(),
  // which uses a worklist algorithm that attempts to bind everything until
  // fixed point.
  auto ext = std::get<0>(getStorage());
  if (!ext->hasBeenBound() || !ext->getExtendedNominal())
    return None;
  return ext->getExtendedNominal();
}

void ExtendedNominalRequest::cacheResult(NominalTypeDecl *value) const {
  auto ext = std::get<0>(getStorage());
  ext->setExtendedNominal(value);
}

void ExtendedNominalRequest::writeDependencySink(
    evaluator::DependencyCollector &tracker,
    NominalTypeDecl *value) const {
  if (!value)
    return;

  // Ensure this extension comes from a source file.
  auto *SF = std::get<0>(getStorage())->getParentSourceFile();
  if (!SF)
    return;

  tracker.addPotentialMember(value);
}

//----------------------------------------------------------------------------//
// Destructor computation.
//----------------------------------------------------------------------------//

Optional<DestructorDecl *> GetDestructorRequest::getCachedResult() const {
  auto *classDecl = std::get<0>(getStorage());
  auto results = classDecl->lookupDirect(DeclBaseName::createDestructor());
  if (results.empty())
    return None;

  return cast<DestructorDecl>(results.front());
}

void GetDestructorRequest::cacheResult(DestructorDecl *value) const {
  auto *classDecl = std::get<0>(getStorage());
  classDecl->addMember(value);
}

//----------------------------------------------------------------------------//
// GenericParamListRequest computation.
//----------------------------------------------------------------------------//

Optional<GenericParamList *> GenericParamListRequest::getCachedResult() const {
  auto *decl = std::get<0>(getStorage());
  if (auto *params = decl->GenericParamsAndBit.getPointer())
    return params;

  if (decl->GenericParamsAndBit.getInt())
    return nullptr;

  return None;
}

void GenericParamListRequest::cacheResult(GenericParamList *params) const {
  auto *context = std::get<0>(getStorage());
  if (params)
    params->setDeclContext(context);

  context->GenericParamsAndBit.setPointerAndInt(params, true);
}

//----------------------------------------------------------------------------//
// UnqualifiedLookupRequest computation.
//----------------------------------------------------------------------------//

void swift::simple_display(llvm::raw_ostream &out,
                           const UnqualifiedLookupDescriptor &desc) {
  out << "looking up ";
  simple_display(out, desc.Name);
  out << " from ";
  simple_display(out, desc.DC);
  out << " with options ";
  simple_display(out, desc.Options);
}

SourceLoc
swift::extractNearestSourceLoc(const UnqualifiedLookupDescriptor &desc) {
  return extractNearestSourceLoc(desc.DC);
}

//----------------------------------------------------------------------------//
// DirectLookupRequest computation.
//----------------------------------------------------------------------------//

void swift::simple_display(llvm::raw_ostream &out,
                           const DirectLookupDescriptor &desc) {
  out << "directly looking up ";
  simple_display(out, desc.Name);
  out << " on ";
  simple_display(out, desc.DC);
  out << " with options ";
  simple_display(out, desc.Options);
}

SourceLoc swift::extractNearestSourceLoc(const DirectLookupDescriptor &desc) {
  return extractNearestSourceLoc(desc.DC);
}

//----------------------------------------------------------------------------//
// LookupOperatorRequest computation.
//----------------------------------------------------------------------------//

OperatorLookupDescriptor OperatorLookupDescriptor::forDC(const DeclContext *DC,
                                                         Identifier name) {
  auto *moduleDC = DC->getModuleScopeContext();
  if (auto *file = dyn_cast<FileUnit>(moduleDC)) {
    return OperatorLookupDescriptor::forFile(file, name);
  } else {
    auto *mod = cast<ModuleDecl>(moduleDC->getAsDecl());
    return OperatorLookupDescriptor::forModule(mod, name);
  }
}

ArrayRef<FileUnit *> OperatorLookupDescriptor::getFiles() const {
  if (auto *module = getModule())
    return module->getFiles();

  // Return an ArrayRef pointing to the FileUnit in the union.
  return llvm::makeArrayRef(*fileOrModule.getAddrOfPtr1());
}

void swift::simple_display(llvm::raw_ostream &out,
                           const OperatorLookupDescriptor &desc) {
  out << "looking up operator ";
  simple_display(out, desc.name);
  out << " in ";
  simple_display(out, desc.fileOrModule);
}

SourceLoc swift::extractNearestSourceLoc(const OperatorLookupDescriptor &desc) {
  return extractNearestSourceLoc(desc.fileOrModule);
}

void DirectLookupRequest::writeDependencySink(
    evaluator::DependencyCollector &tracker,
    const TinyPtrVector<ValueDecl *> &result) const {
  auto &desc = std::get<0>(getStorage());
  // Add used members from the perspective of
  // 1) The decl context they are found in
  // 2) The decl context of the request
  // This gets us a dependency not just on `Foo.bar` but on `extension Foo.bar`.
  for (const auto *member : result) {
    tracker.addUsedMember(member->getDeclContext(), desc.Name.getBaseName());
  }
  tracker.addUsedMember(desc.DC, desc.Name.getBaseName());
}

//----------------------------------------------------------------------------//
// LookupInModuleRequest computation.
//----------------------------------------------------------------------------//

void LookupInModuleRequest::writeDependencySink(
    evaluator::DependencyCollector &reqTracker,
    const QualifiedLookupResult &l) const {
  auto *DC = std::get<0>(getStorage());
  auto member = std::get<1>(getStorage());

  // Decline to record lookups if the module in question has no incremental
  // dependency information available.
  auto *module = DC->getParentModule();
  if (module->isMainModule() || module->hasIncrementalInfo()) {
    reqTracker.addTopLevelName(member.getBaseName());
  }
}

//----------------------------------------------------------------------------//
// LookupConformanceInModuleRequest computation.
//----------------------------------------------------------------------------//

void swift::simple_display(llvm::raw_ostream &out,
                           const LookupConformanceDescriptor &desc) {
  out << "looking up conformance to ";
  simple_display(out, desc.PD);
  out << " for ";
  out << desc.Ty.getString();
  out << " in ";
  simple_display(out, desc.Mod);
}

void AnyObjectLookupRequest::writeDependencySink(
    evaluator::DependencyCollector &reqTracker,
    const QualifiedLookupResult &l) const {
  auto member = std::get<1>(getStorage());
  reqTracker.addDynamicLookupName(member.getBaseName());
}

SourceLoc
swift::extractNearestSourceLoc(const LookupConformanceDescriptor &desc) {
  return SourceLoc();
}

//----------------------------------------------------------------------------//
// ModuleQualifiedLookupRequest computation.
//----------------------------------------------------------------------------//

void ModuleQualifiedLookupRequest::writeDependencySink(
    evaluator::DependencyCollector &reqTracker,
    const QualifiedLookupResult &l) const {
  auto *module = std::get<1>(getStorage());
  auto member = std::get<2>(getStorage());

  // Decline to record lookups if the module in question has no incremental
  // dependency information available.
  if (module->isMainModule() || module->hasIncrementalInfo()) {
    reqTracker.addTopLevelName(member.getBaseName());
  }
}

//----------------------------------------------------------------------------//
// LookupConformanceInModuleRequest computation.
//----------------------------------------------------------------------------//

void LookupConformanceInModuleRequest::writeDependencySink(
    evaluator::DependencyCollector &reqTracker,
    ProtocolConformanceRef lookupResult) const {
  if (lookupResult.isInvalid() || !lookupResult.isConcrete())
    return;

  auto &desc = std::get<0>(getStorage());
  auto *Adoptee = desc.Ty->getAnyNominal();
  if (!Adoptee)
    return;

  // Decline to record lookups if the module in question has no incremental
  // dependency information available.
  auto *conformance = lookupResult.getConcrete();
  auto *module = conformance->getDeclContext()->getParentModule();
  if (module->isMainModule() || module->hasIncrementalInfo()) {
    reqTracker.addPotentialMember(Adoptee);
  }
}

//----------------------------------------------------------------------------//
// UnqualifiedLookupRequest computation.
//----------------------------------------------------------------------------//

void UnqualifiedLookupRequest::writeDependencySink(
    evaluator::DependencyCollector &track,
    const LookupResult &res) const {
  auto &desc = std::get<0>(getStorage());
  track.addTopLevelName(desc.Name.getBaseName());
}

// The following clang importer requests have some definitions here to prevent
// linker errors when building lib syntax parser (which doesn't link with the
// clang importer).

//----------------------------------------------------------------------------//
// ClangDirectLookupRequest computation.
//----------------------------------------------------------------------------//

void swift::simple_display(llvm::raw_ostream &out,
                           const ClangDirectLookupDescriptor &desc) {
  out << "Looking up ";
  simple_display(out, desc.name);
  out << " in ";
  simple_display(out, desc.decl);
}

SourceLoc
swift::extractNearestSourceLoc(const ClangDirectLookupDescriptor &desc) {
  return extractNearestSourceLoc(desc.decl);
}

//----------------------------------------------------------------------------//
// CXXNamespaceMemberLookup computation.
//----------------------------------------------------------------------------//

void swift::simple_display(llvm::raw_ostream &out,
                           const CXXNamespaceMemberLookupDescriptor &desc) {
  out << "Looking up ";
  simple_display(out, desc.name);
  out << " in ";
  simple_display(out, desc.namespaceDecl);
}

SourceLoc
swift::extractNearestSourceLoc(const CXXNamespaceMemberLookupDescriptor &desc) {
  return extractNearestSourceLoc(desc.namespaceDecl);
}

//----------------------------------------------------------------------------//
// ClangRecordMemberLookup computation.
//----------------------------------------------------------------------------//

void swift::simple_display(llvm::raw_ostream &out,
                           const ClangRecordMemberLookupDescriptor &desc) {
  out << "Looking up ";
  simple_display(out, desc.name);
  out << " in ";
  simple_display(out, desc.recordDecl);
}

SourceLoc
swift::extractNearestSourceLoc(const ClangRecordMemberLookupDescriptor &desc) {
  return extractNearestSourceLoc(desc.recordDecl);
}

// Implement the clang importer type zone.
#define SWIFT_TYPEID_ZONE ClangImporter
#define SWIFT_TYPEID_HEADER "swift/ClangImporter/ClangImporterTypeIDZone.def"
#include "swift/Basic/ImplementTypeIDZone.h"
#undef SWIFT_TYPEID_ZONE
#undef SWIFT_TYPEID_HEADER

// Define request evaluation functions for each of the name lookup requests.
static AbstractRequestFunction *nameLookupRequestFunctions[] = {
#define SWIFT_REQUEST(Zone, Name, Sig, Caching, LocOptions)                    \
  reinterpret_cast<AbstractRequestFunction *>(&Name::evaluateRequest),
#include "swift/AST/NameLookupTypeIDZone.def"
#undef SWIFT_REQUEST
};

void swift::registerNameLookupRequestFunctions(Evaluator &evaluator) {
  evaluator.registerRequestFunctions(Zone::NameLookup,
                                     nameLookupRequestFunctions);
}
