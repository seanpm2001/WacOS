//===--- Decl.cpp - Swift Language Decl ASTs ------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Decl class and subclasses.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/Decl.h"
#include "swift/AST/AccessRequests.h"
#include "swift/AST/AccessScope.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/ASTWalker.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/CaptureInfo.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsSema.h"
#include "swift/AST/ExistentialLayout.h"
#include "swift/AST/Expr.h"
#include "swift/AST/ForeignAsyncConvention.h"
#include "swift/AST/ForeignErrorConvention.h"
#include "swift/AST/GenericEnvironment.h"
#include "swift/AST/GenericSignature.h"
#include "swift/AST/Initializer.h"
#include "swift/AST/LazyResolver.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/Module.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/NameLookupRequests.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/ParseRequests.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/PropertyWrappers.h"
#include "swift/AST/ProtocolConformance.h"
#include "swift/AST/ResilienceExpansion.h"
#include "swift/AST/SourceFile.h"
#include "swift/AST/Stmt.h"
#include "swift/AST/TypeCheckRequests.h"
#include "swift/AST/TypeLoc.h"
#include "swift/AST/SwiftNameTranslation.h"
#include "swift/Basic/Defer.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/Parse/Lexer.h" // FIXME: Bad dependency
#include "clang/Lex/MacroInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "swift/Basic/Range.h"
#include "swift/Basic/StringExtras.h"
#include "swift/Basic/Statistic.h"
#include "swift/Basic/TypeID.h"
#include "swift/Demangling/ManglingMacros.h"

#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Module.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"

#include "InlinableText.h"
#include <algorithm>

using namespace swift;

#define DEBUG_TYPE "Serialization"

STATISTIC(NumLazyRequirementSignatures,
          "# of lazily-deserialized requirement signatures known");

#undef DEBUG_TYPE

#define DECL(Id, _) \
  static_assert((DeclKind::Id == DeclKind::Module) ^ \
                IsTriviallyDestructible<Id##Decl>::value, \
                "Decls are BumpPtrAllocated; the destructor is never called");
#include "swift/AST/DeclNodes.def"
static_assert(IsTriviallyDestructible<ParameterList>::value,
              "ParameterLists are BumpPtrAllocated; the d'tor is never called");
static_assert(IsTriviallyDestructible<GenericParamList>::value,
              "GenericParamLists are BumpPtrAllocated; the d'tor isn't called");

const clang::MacroInfo *ClangNode::getAsMacro() const {
  if (auto MM = getAsModuleMacro())
    return MM->getMacroInfo();
  return getAsMacroInfo();
}

clang::SourceLocation ClangNode::getLocation() const {
  if (auto D = getAsDecl())
    return D->getLocation();
  if (auto M = getAsMacro())
    return M->getDefinitionLoc();

  return clang::SourceLocation();
}

clang::SourceRange ClangNode::getSourceRange() const {
  if (auto D = getAsDecl())
    return D->getSourceRange();
  if (auto M = getAsMacro())
    return clang::SourceRange(M->getDefinitionLoc(), M->getDefinitionEndLoc());

  return clang::SourceLocation();
}

const clang::Module *ClangNode::getClangModule() const {
  if (auto *M = getAsModule())
    return M;
  if (auto *ID = dyn_cast_or_null<clang::ImportDecl>(getAsDecl()))
    return ID->getImportedModule();
  return nullptr;
}

void ClangNode::dump() const {
#if SWIFT_BUILD_ONLY_SYNTAXPARSERLIB
  return; // not needed for the parser library.
#endif

  if (auto D = getAsDecl())
    D->dump();
  else if (auto M = getAsMacro())
    M->dump();
  else if (auto M = getAsModule())
    M->dump();
  else
    llvm::errs() << "ClangNode contains nullptr\n";
}

StringRef Decl::getKindName(DeclKind K) {
  switch (K) {
#define DECL(Id, Parent) case DeclKind::Id: return #Id;
#include "swift/AST/DeclNodes.def"
  }
  llvm_unreachable("bad DeclKind");
}

DescriptiveDeclKind Decl::getDescriptiveKind() const {
#define TRIVIAL_KIND(Kind)                      \
  case DeclKind::Kind:                          \
    return DescriptiveDeclKind::Kind

  switch (getKind()) {
  TRIVIAL_KIND(Import);
  TRIVIAL_KIND(Extension);
  TRIVIAL_KIND(EnumCase);
  TRIVIAL_KIND(TopLevelCode);
  TRIVIAL_KIND(IfConfig);
  TRIVIAL_KIND(PoundDiagnostic);
  TRIVIAL_KIND(PatternBinding);
  TRIVIAL_KIND(PrecedenceGroup);
  TRIVIAL_KIND(InfixOperator);
  TRIVIAL_KIND(PrefixOperator);
  TRIVIAL_KIND(PostfixOperator);
  TRIVIAL_KIND(TypeAlias);
  TRIVIAL_KIND(GenericTypeParam);
  TRIVIAL_KIND(AssociatedType);
  TRIVIAL_KIND(Protocol);
  TRIVIAL_KIND(Constructor);
  TRIVIAL_KIND(Destructor);
  TRIVIAL_KIND(EnumElement);
  TRIVIAL_KIND(Param);
  TRIVIAL_KIND(Module);
  TRIVIAL_KIND(MissingMember);

   case DeclKind::Enum:
     return cast<EnumDecl>(this)->getGenericParams()
              ? DescriptiveDeclKind::GenericEnum
              : DescriptiveDeclKind::Enum;

   case DeclKind::Struct:
     return cast<StructDecl>(this)->getGenericParams()
              ? DescriptiveDeclKind::GenericStruct
              : DescriptiveDeclKind::Struct;

   case DeclKind::Class:
     return cast<ClassDecl>(this)->getGenericParams()
              ? DescriptiveDeclKind::GenericClass
              : DescriptiveDeclKind::Class;

   case DeclKind::Var: {
     auto var = cast<VarDecl>(this);
     switch (var->getCorrectStaticSpelling()) {
     case StaticSpellingKind::None:
       if (var->getDeclContext()->isTypeContext())
         return DescriptiveDeclKind::Property;
       return var->isLet() ? DescriptiveDeclKind::Let
                           : DescriptiveDeclKind::Var;
     case StaticSpellingKind::KeywordStatic:
       return DescriptiveDeclKind::StaticProperty;
     case StaticSpellingKind::KeywordClass:
       return DescriptiveDeclKind::ClassProperty;
     }
   }

   case DeclKind::Subscript: {
     auto subscript = cast<SubscriptDecl>(this);
     switch (subscript->getCorrectStaticSpelling()) {
     case StaticSpellingKind::None:
       return DescriptiveDeclKind::Subscript;
     case StaticSpellingKind::KeywordStatic:
       return DescriptiveDeclKind::StaticSubscript;
     case StaticSpellingKind::KeywordClass:
       return DescriptiveDeclKind::ClassSubscript;
     }
   }

   case DeclKind::Accessor: {
     auto accessor = cast<AccessorDecl>(this);

     switch (accessor->getAccessorKind()) {
     case AccessorKind::Get:
       return DescriptiveDeclKind::Getter;

     case AccessorKind::Set:
       return DescriptiveDeclKind::Setter;

     case AccessorKind::WillSet:
       return DescriptiveDeclKind::WillSet;

     case AccessorKind::DidSet:
       return DescriptiveDeclKind::DidSet;

     case AccessorKind::Address:
       return DescriptiveDeclKind::Addressor;

     case AccessorKind::MutableAddress:
       return DescriptiveDeclKind::MutableAddressor;

     case AccessorKind::Read:
       return DescriptiveDeclKind::ReadAccessor;

     case AccessorKind::Modify:
       return DescriptiveDeclKind::ModifyAccessor;
     }
     llvm_unreachable("bad accessor kind");
   }

   case DeclKind::Func: {
     auto func = cast<FuncDecl>(this);

     if (func->isOperator())
       return DescriptiveDeclKind::OperatorFunction;

     if (func->getDeclContext()->isLocalContext())
       return DescriptiveDeclKind::LocalFunction;

     if (func->getDeclContext()->isModuleScopeContext())
       return DescriptiveDeclKind::GlobalFunction;

     // We have a method.
     switch (func->getCorrectStaticSpelling()) {
     case StaticSpellingKind::None:
       if (func->isDistributed()) {
         return DescriptiveDeclKind::DistributedMethod;
       } else {
         return DescriptiveDeclKind::Method;
       }
     case StaticSpellingKind::KeywordStatic:
       return DescriptiveDeclKind::StaticMethod;
     case StaticSpellingKind::KeywordClass:
       return DescriptiveDeclKind::ClassMethod;
     }
   }

   case DeclKind::OpaqueType: {
     auto *opaqueTypeDecl = cast<OpaqueTypeDecl>(this);
     if (dyn_cast_or_null<VarDecl>(opaqueTypeDecl->getNamingDecl()))
       return DescriptiveDeclKind::OpaqueVarType;
     return DescriptiveDeclKind::OpaqueResultType;
   }
  }
#undef TRIVIAL_KIND
  llvm_unreachable("bad DescriptiveDeclKind");
}

StringRef Decl::getDescriptiveKindName(DescriptiveDeclKind K) {
#define ENTRY(Kind, String) case DescriptiveDeclKind::Kind: return String
  switch (K) {
  ENTRY(Import, "import");
  ENTRY(Extension, "extension");
  ENTRY(EnumCase, "case");
  ENTRY(TopLevelCode, "top-level code");
  ENTRY(IfConfig, "conditional block");
  ENTRY(PoundDiagnostic, "diagnostic");
  ENTRY(PatternBinding, "pattern binding");
  ENTRY(Var, "var");
  ENTRY(Param, "parameter");
  ENTRY(Let, "let");
  ENTRY(Property, "property");
  ENTRY(StaticProperty, "static property");
  ENTRY(ClassProperty, "class property");
  ENTRY(PrecedenceGroup, "precedence group");
  ENTRY(InfixOperator, "infix operator");
  ENTRY(PrefixOperator, "prefix operator");
  ENTRY(PostfixOperator, "postfix operator");
  ENTRY(TypeAlias, "type alias");
  ENTRY(GenericTypeParam, "generic parameter");
  ENTRY(AssociatedType, "associated type");
  ENTRY(Type, "type");
  ENTRY(Enum, "enum");
  ENTRY(Struct, "struct");
  ENTRY(Class, "class");
  ENTRY(Protocol, "protocol");
  ENTRY(GenericEnum, "generic enum");
  ENTRY(GenericStruct, "generic struct");
  ENTRY(GenericClass, "generic class");
  ENTRY(GenericType, "generic type");
  ENTRY(Subscript, "subscript");
  ENTRY(StaticSubscript, "static subscript");
  ENTRY(ClassSubscript, "class subscript");
  ENTRY(Constructor, "initializer");
  ENTRY(Destructor, "deinitializer");
  ENTRY(LocalFunction, "local function");
  ENTRY(GlobalFunction, "global function");
  ENTRY(OperatorFunction, "operator function");
  ENTRY(Method, "instance method");
  ENTRY(StaticMethod, "static method");
  ENTRY(ClassMethod, "class method");
  ENTRY(DistributedMethod, "distributed instance method");
  ENTRY(Getter, "getter");
  ENTRY(Setter, "setter");
  ENTRY(WillSet, "willSet observer");
  ENTRY(DidSet, "didSet observer");
  ENTRY(Addressor, "address accessor");
  ENTRY(MutableAddressor, "mutableAddress accessor");
  ENTRY(ReadAccessor, "_read accessor");
  ENTRY(ModifyAccessor, "_modify accessor");
  ENTRY(EnumElement, "enum case");
  ENTRY(Module, "module");
  ENTRY(MissingMember, "missing member placeholder");
  ENTRY(Requirement, "requirement");
  ENTRY(OpaqueResultType, "result");
  ENTRY(OpaqueVarType, "type");
  }
#undef ENTRY
  llvm_unreachable("bad DescriptiveDeclKind");
}

const Decl *Decl::getInnermostDeclWithAvailability() const {
  const Decl *enclosingDecl = this;
  // Find the innermost enclosing declaration with an @available annotation.
  while (enclosingDecl != nullptr) {
    if (enclosingDecl->getAttrs().hasAttribute<AvailableAttr>())
      return enclosingDecl;

    enclosingDecl = enclosingDecl->getDeclContext()->getAsDecl();
  }

  return nullptr;
}

Optional<llvm::VersionTuple>
Decl::getIntroducedOSVersion(PlatformKind Kind) const {
  for (auto *attr: getAttrs()) {
    if (auto *ava = dyn_cast<AvailableAttr>(attr)) {
      if (ava->Platform == Kind && ava->Introduced) {
        return ava->Introduced;
      }
    }
  }
  return None;
}

llvm::raw_ostream &swift::operator<<(llvm::raw_ostream &OS,
                                     StaticSpellingKind SSK) {
  switch (SSK) {
  case StaticSpellingKind::None:
    return OS << "<none>";
  case StaticSpellingKind::KeywordStatic:
    return OS << "'static'";
  case StaticSpellingKind::KeywordClass:
    return OS << "'class'";
  }
  llvm_unreachable("bad StaticSpellingKind");
}

llvm::raw_ostream &swift::operator<<(llvm::raw_ostream &OS,
                                     ReferenceOwnership RO) {
  if (RO == ReferenceOwnership::Strong)
    return OS << "'strong'";
  return OS << "'" << keywordOf(RO) << "'";
}

llvm::raw_ostream &swift::operator<<(llvm::raw_ostream &OS,
                                     SelfAccessKind SAK) {
  switch (SAK) {
  case SelfAccessKind::NonMutating: return OS << "'nonmutating'";
  case SelfAccessKind::Mutating: return OS << "'mutating'";
  case SelfAccessKind::Consuming: return OS << "'__consuming'";
  }
  llvm_unreachable("Unknown SelfAccessKind");
}

DeclContext *Decl::getInnermostDeclContext() const {
  if (auto func = dyn_cast<AbstractFunctionDecl>(this))
    return const_cast<AbstractFunctionDecl*>(func);
  if (auto subscript = dyn_cast<SubscriptDecl>(this))
    return const_cast<SubscriptDecl*>(subscript);
  if (auto type = dyn_cast<GenericTypeDecl>(this))
    return const_cast<GenericTypeDecl*>(type);
  if (auto ext = dyn_cast<ExtensionDecl>(this))
    return const_cast<ExtensionDecl*>(ext);
  if (auto topLevel = dyn_cast<TopLevelCodeDecl>(this))
    return const_cast<TopLevelCodeDecl*>(topLevel);

  return getDeclContext();
}

bool Decl::isInvalid() const {
  switch (getKind()) {
#define VALUE_DECL(ID, PARENT)
#define DECL(ID, PARENT) \
  case DeclKind::ID:
#include "swift/AST/DeclNodes.def"
    return Bits.Decl.Invalid;
  case DeclKind::Param: {
    // Parameters are special because closure parameters may not have type
    // annotations. In which case, the interface type request returns
    // ErrorType. Therefore, consider parameters with implicit types to always
    // be valid.
    auto *PD = cast<ParamDecl>(this);
    if (!PD->getTypeRepr() && !PD->hasInterfaceType())
      return false;
  }
    LLVM_FALLTHROUGH;
  case DeclKind::Enum:
  case DeclKind::Struct:
  case DeclKind::Class:
  case DeclKind::Protocol:
  case DeclKind::OpaqueType:
  case DeclKind::TypeAlias:
  case DeclKind::GenericTypeParam:
  case DeclKind::AssociatedType:
  case DeclKind::Module:
  case DeclKind::Var:
  case DeclKind::Subscript:
  case DeclKind::Constructor:
  case DeclKind::Destructor:
  case DeclKind::Func:
  case DeclKind::EnumElement:
    return cast<ValueDecl>(this)->getInterfaceType()->hasError();

  case DeclKind::Accessor: {
    auto *AD = cast<AccessorDecl>(this);
    if (AD->hasInterfaceType() && AD->getInterfaceType()->hasError())
      return true;
    return AD->getStorage()->isInvalid();
  }
  }

  llvm_unreachable("Unknown decl kind");
}

void Decl::setInvalidBit() { Bits.Decl.Invalid = true; }

void Decl::setInvalid() {
  switch (getKind()) {
#define VALUE_DECL(ID, PARENT)
#define DECL(ID, PARENT) \
  case DeclKind::ID:
#include "swift/AST/DeclNodes.def"
    Bits.Decl.Invalid = true;
    return;
  case DeclKind::Enum:
  case DeclKind::Struct:
  case DeclKind::Class:
  case DeclKind::Protocol:
  case DeclKind::OpaqueType:
  case DeclKind::TypeAlias:
  case DeclKind::GenericTypeParam:
  case DeclKind::AssociatedType:
  case DeclKind::Module:
  case DeclKind::Var:
  case DeclKind::Param:
  case DeclKind::Subscript:
  case DeclKind::Constructor:
  case DeclKind::Destructor:
  case DeclKind::Func:
  case DeclKind::Accessor:
  case DeclKind::EnumElement:
    cast<ValueDecl>(this)->setInterfaceType(ErrorType::get(getASTContext()));
    return;
  }

  llvm_unreachable("Unknown decl kind");
}

void Decl::setDeclContext(DeclContext *DC) { 
  Context = DC;
}

bool Decl::isUserAccessible() const {
  if (auto VD = dyn_cast<ValueDecl>(this)) {
    return VD->isUserAccessible();
  }
  return true;
}

bool Decl::canHaveComment() const {
  return !this->hasClangNode() &&
         (isa<ValueDecl>(this) || isa<ExtensionDecl>(this)) &&
         !isa<ParamDecl>(this) &&
         (!isa<AbstractTypeParamDecl>(this) || isa<AssociatedTypeDecl>(this));
}

ModuleDecl *Decl::getModuleContext() const {
  return getDeclContext()->getParentModule();
}

/// Retrieve the diagnostic engine for diagnostics emission.
DiagnosticEngine &Decl::getDiags() const {
  return getASTContext().Diags;
}

// Helper functions to verify statically whether source-location
// functions have been overridden.
typedef const char (&TwoChars)[2];
template<typename Class> 
inline char checkSourceLocType(SourceLoc (Class::*)() const);
inline TwoChars checkSourceLocType(SourceLoc (Decl::*)() const);

template<typename Class>
inline char checkSourceLocType(SourceLoc (Class::*)(bool) const);
inline TwoChars checkSourceLocType(SourceLoc (Decl::*)(bool) const);

template<typename Class> 
inline char checkSourceRangeType(SourceRange (Class::*)() const);
inline TwoChars checkSourceRangeType(SourceRange (Decl::*)() const);

SourceRange Decl::getSourceRange() const {
  switch (getKind()) {
#define DECL(ID, PARENT) \
static_assert(sizeof(checkSourceRangeType(&ID##Decl::getSourceRange)) == 1, \
              #ID "Decl is missing getSourceRange()"); \
case DeclKind::ID: return cast<ID##Decl>(this)->getSourceRange();
#include "swift/AST/DeclNodes.def"
  }

  llvm_unreachable("Unknown decl kind");
}

SourceRange Decl::getSourceRangeIncludingAttrs() const {
  auto Range = getSourceRange();

  // Attributes on AccessorDecl may syntactically belong to PatternBindingDecl.
  // e.g. 'override'.
  if (auto *AD = dyn_cast<AccessorDecl>(this)) {
    // If this is implicit getter, accessor range should not include attributes.
    if (AD->isImplicitGetter())
      return Range;

    // Otherwise, include attributes directly attached to the accessor.
    SourceLoc VarLoc = AD->getStorage()->getStartLoc();
    for (auto Attr : getAttrs()) {
      if (!Attr->getRange().isValid())
        continue;

      SourceLoc AttrStartLoc = Attr->getRangeWithAt().Start;
      if (getASTContext().SourceMgr.isBeforeInBuffer(VarLoc, AttrStartLoc))
        Range.widen(AttrStartLoc);
    }
    return Range;
  }

  // Attributes on VarDecl syntactically belong to PatternBindingDecl.
  if (isa<VarDecl>(this) && !isa<ParamDecl>(this))
    return Range;

  // Attributes on PatternBindingDecls are attached to VarDecls in AST.
  if (auto *PBD = dyn_cast<PatternBindingDecl>(this)) {
    for (auto i : range(PBD->getNumPatternEntries()))
      PBD->getPattern(i)->forEachVariable([&](VarDecl *VD) {
        for (auto Attr : VD->getAttrs())
          if (Attr->getRange().isValid())
            Range.widen(Attr->getRangeWithAt());
      });
  }

  for (auto Attr : getAttrs()) {
    if (Attr->getRange().isValid())
      Range.widen(Attr->getRangeWithAt());
  }
  return Range;
}

SourceLoc Decl::getLocFromSource() const {
  switch (getKind()) {
#define DECL(ID, X) \
static_assert(sizeof(checkSourceLocType(&ID##Decl::getLocFromSource)) == 1, \
              #ID "Decl is missing getLocFromSource()"); \
case DeclKind::ID: return cast<ID##Decl>(this)->getLocFromSource();
#include "swift/AST/DeclNodes.def"
  }

  llvm_unreachable("Unknown decl kind");
}

const ExternalSourceLocs *Decl::getSerializedLocs() const {
  auto &Context = getASTContext();
  if (auto EL = Context.getExternalSourceLocs(this).getValueOr(nullptr))
    return EL;

  static ExternalSourceLocs NullLocs{};

  auto *File = cast<FileUnit>(getDeclContext()->getModuleScopeContext());
  if (File->getKind() != FileUnitKind::SerializedAST)
    return &NullLocs;

  auto RawLocs = File->getExternalRawLocsForDecl(this);
  if (!RawLocs.hasValue()) {
    // Don't read .swiftsourceinfo again on failure
    Context.setExternalSourceLocs(this, &NullLocs);
    return &NullLocs;
  }

  auto &SM = getASTContext().SourceMgr;
  unsigned BufferID = SM.getExternalSourceBufferID(RawLocs->SourceFilePath);
  if (!BufferID) {
    // Don't try open the file again on failure
    Context.setExternalSourceLocs(this, &NullLocs);
    return &NullLocs;
  }

  CharSourceRange BufferRange = SM.getRangeForBuffer(BufferID);
  auto ResolveLoc = [&](const ExternalSourceLocs::RawLoc &Raw) -> SourceLoc {
    // If the underlying source has been updated and the swiftsourceinfo hasn't,
    // make sure we don't produce invalid source locations. Ideally would check
    // the file hasn't been modified.
    if (Raw.Offset > BufferRange.getByteLength())
      return SourceLoc();

    // If the decl had a presumed loc, create its virtual file so that
    // getPresumedLineAndColForLoc works from serialized locations as well. No
    // need to check the buffer range, the directive must be before the location
    // itself.
    if (Raw.Directive.isValid()) {
      auto &LD = Raw.Directive;
      SourceLoc Loc = SM.getLocForOffset(BufferID, LD.Offset);
      SM.createVirtualFile(Loc, LD.Name, LD.LineOffset, LD.Length);
    }
    return SM.getLocForOffset(BufferID, Raw.Offset);
  };

  auto *Result = getASTContext().Allocate<ExternalSourceLocs>();
  Result->BufferID = BufferID;
  Result->Loc = ResolveLoc(RawLocs->Loc);

  auto DocRanges = getASTContext().AllocateUninitialized<CharSourceRange>(RawLocs->DocRanges.size());
  for (auto I : indices(RawLocs->DocRanges)) {
    auto &Range = RawLocs->DocRanges[I];
    DocRanges[I] = CharSourceRange(ResolveLoc(Range.first), Range.second);
  }
  Result->DocRanges = DocRanges;

  Context.setExternalSourceLocs(this, Result);
  return Result;
}

StringRef Decl::getAlternateModuleName() const {
  for (auto *Att: Attrs) {
    if (auto *OD = dyn_cast<OriginallyDefinedInAttr>(Att)) {
      if (OD->isActivePlatform(getASTContext())) {
        return OD->OriginalModuleName;
      }
    }
  }
  
  for (auto *DC = getDeclContext(); DC; DC = DC->getParent()) {
    if (auto decl = DC->getAsDecl()) {
      if (decl == this)
        continue;
      auto AM = decl->getAlternateModuleName();
      if (!AM.empty())
        return AM;
    }
  }
  return StringRef();
}

SourceLoc Decl::getLoc(bool SerializedOK) const {
#define DECL(ID, X) \
static_assert(sizeof(checkSourceLocType(&ID##Decl::getLoc)) == 2, \
              #ID "Decl is re-defining getLoc()");
#include "swift/AST/DeclNodes.def"
  if (isa<ModuleDecl>(this))
    return SourceLoc();
  // When the decl is context-free, we should get loc from source buffer.
  if (!getDeclContext())
    return getLocFromSource();
  FileUnit *File = dyn_cast<FileUnit>(getDeclContext()->getModuleScopeContext());
  if (!File)
    return getLocFromSource();
  switch(File->getKind()) {
  case FileUnitKind::Source:
    return getLocFromSource();
  case FileUnitKind::SerializedAST: {
    if (!SerializedOK)
      return SourceLoc();
    return getSerializedLocs()->Loc;
  }
  case FileUnitKind::Builtin:
  case FileUnitKind::Synthesized:
  case FileUnitKind::ClangModule:
  case FileUnitKind::DWARFModule:
    return SourceLoc();
  }
  llvm_unreachable("invalid file kind");
}

Optional<CustomAttrNominalPair> Decl::getGlobalActorAttr() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<Decl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           GlobalActorAttributeRequest{mutableThis},
                           None);
}

bool Decl::preconcurrency() const {
  if (getAttrs().hasAttribute<PreconcurrencyAttr>())
    return true;

  // Imported C declarations always predate concurrency.
  if (isa<ClangModuleUnit>(getDeclContext()->getModuleScopeContext()))
    return true;

  return false;
}


Expr *AbstractFunctionDecl::getSingleExpressionBody() const {
  assert(hasSingleExpressionBody() && "Not a single-expression body");
  auto braceStmt = getBody();
  assert(braceStmt != nullptr && "No body currently available.");
  auto body = getBody()->getLastElement();
  if (auto *stmt = body.dyn_cast<Stmt *>()) {
    if (auto *returnStmt = dyn_cast<ReturnStmt>(stmt)) {
      return returnStmt->getResult();
    } else if (isa<FailStmt>(stmt)) {
      // We can only get to this point if we're a type-checked ConstructorDecl
      // which was originally spelled init?(...) { nil }.  
      //
      // There no longer is a single-expression to return, so ignore null.
      return nullptr;
    }
  }
  return body.get<Expr *>();
}

void AbstractFunctionDecl::setSingleExpressionBody(Expr *NewBody) {
  assert(hasSingleExpressionBody() && "Not a single-expression body");
  auto body = getBody()->getLastElement();
  if (auto *stmt = body.dyn_cast<Stmt *>()) {
    if (auto *returnStmt = dyn_cast<ReturnStmt>(stmt)) {
      returnStmt->setResult(NewBody);
      return;
    } else if (isa<FailStmt>(stmt)) {
      // We can only get to this point if we're a type-checked ConstructorDecl
      // which was originally spelled init?(...) { nil }.  
      //
      // We can no longer write the single-expression which is being set on us 
      // into anything because a FailStmt does not have such a child.  As a
      // result we need to demand that the NewBody is null.
      assert(NewBody == nullptr);
      return;
    }
  }
  getBody()->setLastElement(NewBody);
}

bool AbstractStorageDecl::isCompileTimeConst() const {
  return getAttrs().hasAttribute<CompileTimeConstAttr>();
}

bool AbstractStorageDecl::isTransparent() const {
  return getAttrs().hasAttribute<TransparentAttr>();
}

bool AbstractFunctionDecl::isTransparent() const {
  // Check if the declaration had the attribute.
  if (getAttrs().hasAttribute<TransparentAttr>())
    return true;

  // If this is an accessor, the computation is a bit more involved, so we
  // kick off a request.
  if (const auto *AD = dyn_cast<AccessorDecl>(this)) {
    ASTContext &ctx = getASTContext();
    return evaluateOrDefault(ctx.evaluator,
      IsAccessorTransparentRequest{const_cast<AccessorDecl *>(AD)},
      false);
  }

  return false;
}

bool ParameterList::hasInternalParameter(StringRef Prefix) const {
  for (auto param : *this) {
    if (param->hasName() && param->getNameStr().startswith(Prefix))
      return true;
    auto argName = param->getArgumentName();
    if (!argName.empty() && argName.str().startswith(Prefix))
      return true;
  }
  return false;
}

bool Decl::hasUnderscoredNaming() const {
  const Decl *D = this;

  // If it's a function or subscript with a parameter with leading
  // underscore, it's a private function or subscript.
  if (isa<AbstractFunctionDecl>(D) || isa<SubscriptDecl>(D)) {
    const auto VD = cast<ValueDecl>(D);
    if (getParameterList(const_cast<ValueDecl *>(VD))
            ->hasInternalParameter("_")) {
      return true;
    }
  }

  if (const auto PD = dyn_cast<ProtocolDecl>(D)) {
    StringRef NameStr = PD->getNameStr();
    if (NameStr.startswith("_Builtin")) {
      return true;
    }
    if (NameStr.startswith("_ExpressibleBy")) {
      return true;
    }
  }

  if (const auto ImportD = dyn_cast<ImportDecl>(D)) {
    if (const auto *Mod = ImportD->getModule()) {
      if (Mod->isSwiftShimsModule()) {
        return true;
      }
    }
  }

  const auto VD = dyn_cast<ValueDecl>(D);
  if (!VD || !VD->hasName()) {
    return false;
  }

  if (!VD->getBaseName().isSpecial() &&
      VD->getBaseIdentifier().str().startswith("_")) {
    return true;
  }

  return false;
}

bool Decl::isPrivateStdlibDecl(bool treatNonBuiltinProtocolsAsPublic) const {
  const Decl *D = this;
  if (auto ExtD = dyn_cast<ExtensionDecl>(D)) {
    Type extTy = ExtD->getExtendedType();
    return extTy.isPrivateStdlibType(treatNonBuiltinProtocolsAsPublic);
  }

  DeclContext *DC = D->getDeclContext()->getModuleScopeContext();
  if (DC->getParentModule()->isBuiltinModule() ||
      DC->getParentModule()->isSwiftShimsModule())
    return true;
  if (!DC->getParentModule()->isSystemModule())
    return false;
  auto FU = dyn_cast<FileUnit>(DC);
  if (!FU)
    return false;
  // Check for Swift module and overlays.
  if (!DC->getParentModule()->isStdlibModule() &&
      FU->getKind() != FileUnitKind::SerializedAST)
    return false;

  if (isa<ProtocolDecl>(D)) {
    if (treatNonBuiltinProtocolsAsPublic)
      return false;
  }

  if (D->getAttrs().hasAttribute<ShowInInterfaceAttr>()) {
    return false;
  }

  return hasUnderscoredNaming();
}

bool Decl::isStdlibDecl() const {
  DeclContext *DC = getDeclContext();
  return DC->isModuleScopeContext() &&
         DC->getParentModule()->isStdlibModule();
}

AvailabilityContext Decl::getAvailabilityForLinkage() const {
  auto containingContext =
      AvailabilityInference::annotatedAvailableRange(this, getASTContext());
  if (containingContext.hasValue()) {
    // If this entity comes from the concurrency module, adjust it's
    // availability for linkage purposes up to Swift 5.5, so that we use
    // weak references any time we reference those symbols when back-deploying
    // concurrency.
    ASTContext &ctx = getASTContext();
    if (getModuleContext()->getName() == ctx.Id_Concurrency) {
      containingContext->intersectWith(ctx.getConcurrencyAvailability());
    }

    return *containingContext;
  }

  if (auto *accessor = dyn_cast<AccessorDecl>(this))
    return accessor->getStorage()->getAvailabilityForLinkage();

  if (auto *opaqueTypeDecl = dyn_cast<OpaqueTypeDecl>(this))
    return opaqueTypeDecl->getNamingDecl()->getAvailabilityForLinkage();

  if (auto *ext = dyn_cast<ExtensionDecl>(this))
    if (auto *nominal = ext->getExtendedNominal())
      return nominal->getAvailabilityForLinkage();

  auto *dc = getDeclContext();
  if (auto *ext = dyn_cast<ExtensionDecl>(dc))
    return ext->getAvailabilityForLinkage();
  else if (auto *nominal = dyn_cast<NominalTypeDecl>(dc))
    return nominal->getAvailabilityForLinkage();

  return AvailabilityContext::alwaysAvailable();
}

bool Decl::isAlwaysWeakImported() const {
  // For a Clang declaration, trust Clang.
  if (auto clangDecl = getClangDecl()) {
    return clangDecl->isWeakImported();
  }

  if (getAttrs().hasAttribute<WeakLinkedAttr>())
    return true;

  if (auto *accessor = dyn_cast<AccessorDecl>(this))
    return accessor->getStorage()->isAlwaysWeakImported();

  if (auto *opaqueTypeDecl = dyn_cast<OpaqueTypeDecl>(this))
    return opaqueTypeDecl->getNamingDecl()->isAlwaysWeakImported();

  if (auto *ext = dyn_cast<ExtensionDecl>(this))
    if (auto *nominal = ext->getExtendedNominal())
      return nominal->isAlwaysWeakImported();

  auto *dc = getDeclContext();
  if (auto *ext = dyn_cast<ExtensionDecl>(dc))
    return ext->isAlwaysWeakImported();
  if (auto *nominal = dyn_cast<NominalTypeDecl>(dc))
    return nominal->isAlwaysWeakImported();

  return false;
}

bool Decl::isWeakImported(ModuleDecl *fromModule) const {
  if (fromModule == nullptr) {
    return (isAlwaysWeakImported() ||
            !getAvailabilityForLinkage().isAlwaysAvailable());
  }

  if (getModuleContext() == fromModule)
    return false;

  if (isAlwaysWeakImported())
    return true;

  auto containingContext = getAvailabilityForLinkage();
  if (containingContext.isAlwaysAvailable())
    return false;

  auto fromContext = AvailabilityContext::forDeploymentTarget(
      fromModule->getASTContext());
  return !fromContext.isContainedIn(containingContext);
}

GenericContext::GenericContext(DeclContextKind Kind, DeclContext *Parent,
                               GenericParamList *Params)
    : _GenericContext(), DeclContext(Kind, Parent) {
  if (Params) {
    Params->setDeclContext(this);
    GenericParamsAndBit.setPointerAndInt(Params, false);
  }
}

TypeArrayView<GenericTypeParamType>
GenericContext::getInnermostGenericParamTypes() const {
  return getGenericSignature().getInnermostGenericParams();
}

/// Retrieve the generic requirements.
ArrayRef<Requirement> GenericContext::getGenericRequirements() const {
  return getGenericSignature().getRequirements();
}

GenericParamList *GenericContext::getGenericParams() const {
  return evaluateOrDefault(getASTContext().evaluator,
                           GenericParamListRequest{
                               const_cast<GenericContext *>(this)}, nullptr);
}

GenericParamList *GenericContext::getParsedGenericParams() const {
  if (GenericParamsAndBit.getInt())
    return nullptr;
  return GenericParamsAndBit.getPointer();
}

bool GenericContext::hasComputedGenericSignature() const {
  return GenericSigAndBit.getInt();
}
      
bool GenericContext::isComputingGenericSignature() const {
  return getASTContext().evaluator.hasActiveRequest(
                 GenericSignatureRequest{const_cast<GenericContext*>(this)});
}

/// If we hit a cycle while building the generic signature, we can't return
/// nullptr, since this breaks invariants elsewhere. Instead, build a dummy
/// signature with no requirements.
static GenericSignature getPlaceholderGenericSignature(
    const DeclContext *DC) {
  SmallVector<GenericParamList *, 2> gpLists;
  DC->forEachGenericContext([&](GenericParamList *genericParams) {
    gpLists.push_back(genericParams);
  });

  if (gpLists.empty())
    return nullptr;

  std::reverse(gpLists.begin(), gpLists.end());
  for (unsigned i : indices(gpLists))
    gpLists[i]->setDepth(i);

  SmallVector<GenericTypeParamType *, 2> result;
  for (auto *genericParams : gpLists) {
    for (auto *genericParam : *genericParams) {
      result.push_back(genericParam->getDeclaredInterfaceType()
                                   ->castTo<GenericTypeParamType>());
    }
  }

  return GenericSignature::get(result, {});
}

GenericSignature GenericContext::getGenericSignature() const {
  // Don't use evaluateOrDefault() here, because getting the 'default value'
  // is slightly expensive here so we don't want to do it eagerly.
  auto result = getASTContext().evaluator(
      GenericSignatureRequest{const_cast<GenericContext *>(this)});
  if (auto err = result.takeError()) {
    llvm::handleAllErrors(std::move(err),
      [](const CyclicalRequestError<GenericSignatureRequest> &E) {
        // cycle detected
      });
    return getPlaceholderGenericSignature(this);
  }
  return *result;
}

GenericEnvironment *GenericContext::getGenericEnvironment() const {
  return getGenericSignature().getGenericEnvironment();
}

void GenericContext::setGenericSignature(GenericSignature genericSig) {
  assert(!GenericSigAndBit.getPointer() && "Generic signature cannot be changed");
  getASTContext().evaluator.cacheOutput(GenericSignatureRequest{this},
                                        std::move(genericSig));
}

SourceRange GenericContext::getGenericTrailingWhereClauseSourceRange() const {
  if (const auto *where = getTrailingWhereClause())
    return where->getSourceRange();

  return SourceRange();
}

ImportDecl *ImportDecl::create(ASTContext &Ctx, DeclContext *DC,
                               SourceLoc ImportLoc, ImportKind Kind,
                               SourceLoc KindLoc,
                               ImportPath Path,
                               ClangNode ClangN) {
  assert(!Path.empty());
  assert(Kind == ImportKind::Module || Path.size() > 1);
  assert(ClangN.isNull() || ClangN.getAsModule() ||
         isa<clang::ImportDecl>(ClangN.getAsDecl()));
  size_t Size = totalSizeToAlloc<ImportPath::Element>(Path.size());
  void *ptr = allocateMemoryForDecl<ImportDecl>(Ctx, Size, !ClangN.isNull());
  auto D = new (ptr) ImportDecl(DC, ImportLoc, Kind, KindLoc, Path);
  if (ClangN)
    D->setClangNode(ClangN);
  auto realNameIfExists = Ctx.getRealModuleName(Path.front().Item,
                                                ASTContext::ModuleAliasLookupOption::realNameFromAlias);
  if (!realNameIfExists.empty()) {
    D->RealModuleName = realNameIfExists;
  }
  return D;
}

ImportDecl::ImportDecl(DeclContext *DC, SourceLoc ImportLoc, ImportKind K,
                       SourceLoc KindLoc, ImportPath Path)
  : Decl(DeclKind::Import, DC), ImportLoc(ImportLoc), KindLoc(KindLoc) {
  Bits.ImportDecl.NumPathElements = Path.size();
  assert(Bits.ImportDecl.NumPathElements == Path.size() && "Truncation error");
  Bits.ImportDecl.ImportKind = static_cast<unsigned>(K);
  assert(getImportKind() == K && "not enough bits for ImportKind");
  std::uninitialized_copy(Path.begin(), Path.end(),
                          getTrailingObjects<ImportPath::Element>());
}

ImportKind ImportDecl::getBestImportKind(const ValueDecl *VD) {
  switch (VD->getKind()) {
  case DeclKind::Import:
  case DeclKind::Extension:
  case DeclKind::PatternBinding:
  case DeclKind::TopLevelCode:
  case DeclKind::InfixOperator:
  case DeclKind::PrefixOperator:
  case DeclKind::PostfixOperator:
  case DeclKind::EnumCase:
  case DeclKind::IfConfig:
  case DeclKind::PoundDiagnostic:
  case DeclKind::PrecedenceGroup:
  case DeclKind::MissingMember:
    llvm_unreachable("not a ValueDecl");

  case DeclKind::AssociatedType:
  case DeclKind::Constructor:
  case DeclKind::Destructor:
  case DeclKind::GenericTypeParam:
  case DeclKind::Subscript:
  case DeclKind::EnumElement:
  case DeclKind::Param:
    llvm_unreachable("not a top-level ValueDecl");

  case DeclKind::Protocol:
    return ImportKind::Protocol;

  case DeclKind::Class:
    return ImportKind::Class;
  case DeclKind::Enum:
    return ImportKind::Enum;
  case DeclKind::Struct:
    return ImportKind::Struct;
      
  case DeclKind::OpaqueType:
    return ImportKind::Type;

  case DeclKind::TypeAlias: {
    Type type = cast<TypeAliasDecl>(VD)->getDeclaredInterfaceType();
    auto *nominal = type->getAnyNominal();
    if (!nominal)
      return ImportKind::Type;
    return getBestImportKind(nominal);
  }

  case DeclKind::Accessor:
  case DeclKind::Func:
    return ImportKind::Func;

  case DeclKind::Var:
    return ImportKind::Var;

  case DeclKind::Module:
    return ImportKind::Module;
  }
  llvm_unreachable("bad DeclKind");
}

Optional<ImportKind>
ImportDecl::findBestImportKind(ArrayRef<ValueDecl *> Decls) {
  assert(!Decls.empty());
  ImportKind FirstKind = ImportDecl::getBestImportKind(Decls.front());

  // FIXME: Only functions can be overloaded.
  if (Decls.size() == 1)
    return FirstKind;
  if (FirstKind != ImportKind::Func)
    return None;

  for (auto NextDecl : Decls.slice(1)) {
    if (ImportDecl::getBestImportKind(NextDecl) != FirstKind)
      return None;
  }

  return FirstKind;
}

ImportPath ImportDecl::getRealImportPath(ImportPath::Builder &scratch) const {
  assert(scratch.empty() && "scratch ImportPath::Builder must be initially empty");
  auto path = getImportPath();
  if (RealModuleName.empty())
    return path;

  for (auto elem : path) {
    if (scratch.empty()) {
      // Add the real module name instead of its alias
      scratch.push_back(RealModuleName);
    } else {
      // Add the rest if any (access path elements)
      scratch.push_back(elem.Item);
    }
  }
  return scratch.get();
}

ArrayRef<ValueDecl *> ImportDecl::getDecls() const {
  // If this isn't a scoped import, there's nothing to do.
  if (getImportKind() == ImportKind::Module)
    return {};

  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<ImportDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           ScopedImportLookupRequest{mutableThis}, {});
}

void NominalTypeDecl::setConformanceLoader(LazyMemberLoader *lazyLoader,
                                           uint64_t contextData) {
  assert(!Bits.NominalTypeDecl.HasLazyConformances &&
         "Already have lazy conformances");
  Bits.NominalTypeDecl.HasLazyConformances = true;

  ASTContext &ctx = getASTContext();
  auto contextInfo = ctx.getOrCreateLazyIterableContextData(this, lazyLoader);
  contextInfo->allConformancesData = contextData;
}

std::pair<LazyMemberLoader *, uint64_t>
NominalTypeDecl::takeConformanceLoaderSlow() {
  assert(Bits.NominalTypeDecl.HasLazyConformances && "not lazy conformances");
  Bits.NominalTypeDecl.HasLazyConformances = false;
  auto contextInfo =
    getASTContext().getOrCreateLazyIterableContextData(this, nullptr);
  return { contextInfo->loader, contextInfo->allConformancesData };
}

InheritedEntry::InheritedEntry(const TypeLoc &typeLoc)
    : TypeLoc(typeLoc), isUnchecked(false) {
  if (auto typeRepr = typeLoc.getTypeRepr())
    isUnchecked = typeRepr->findUncheckedAttrLoc().isValid();
}

ExtensionDecl::ExtensionDecl(SourceLoc extensionLoc,
                             TypeRepr *extendedType,
                             ArrayRef<InheritedEntry> inherited,
                             DeclContext *parent,
                             TrailingWhereClause *trailingWhereClause)
  : GenericContext(DeclContextKind::ExtensionDecl, parent, nullptr),
    Decl(DeclKind::Extension, parent),
    IterableDeclContext(IterableDeclContextKind::ExtensionDecl),
    ExtensionLoc(extensionLoc),
    ExtendedTypeRepr(extendedType),
    Inherited(inherited)
{
  Bits.ExtensionDecl.DefaultAndMaxAccessLevel = 0;
  Bits.ExtensionDecl.HasLazyConformances = false;
  setTrailingWhereClause(trailingWhereClause);
}

ExtensionDecl *ExtensionDecl::create(ASTContext &ctx, SourceLoc extensionLoc,
                                     TypeRepr *extendedType,
                                     ArrayRef<InheritedEntry> inherited,
                                     DeclContext *parent,
                                     TrailingWhereClause *trailingWhereClause,
                                     ClangNode clangNode) {
  unsigned size = sizeof(ExtensionDecl);

  void *declPtr = allocateMemoryForDecl<ExtensionDecl>(ctx, size,
                                                       !clangNode.isNull());

  // Construct the extension.
  auto result = ::new (declPtr) ExtensionDecl(extensionLoc, extendedType,
                                              inherited, parent,
                                              trailingWhereClause);
  if (clangNode)
    result->setClangNode(clangNode);

  return result;
}

void ExtensionDecl::setConformanceLoader(LazyMemberLoader *lazyLoader,
                                         uint64_t contextData) {
  assert(!Bits.ExtensionDecl.HasLazyConformances && 
         "Already have lazy conformances");
  Bits.ExtensionDecl.HasLazyConformances = true;

  ASTContext &ctx = getASTContext();
  auto contextInfo = ctx.getOrCreateLazyIterableContextData(this, lazyLoader);
  contextInfo->allConformancesData = contextData;
}

std::pair<LazyMemberLoader *, uint64_t>
ExtensionDecl::takeConformanceLoaderSlow() {
  assert(Bits.ExtensionDecl.HasLazyConformances && "no conformance loader?");
  Bits.ExtensionDecl.HasLazyConformances = false;

  auto contextInfo =
    getASTContext().getOrCreateLazyIterableContextData(this, nullptr);
  return { contextInfo->loader, contextInfo->allConformancesData };
}

NominalTypeDecl *ExtensionDecl::getExtendedNominal() const {
  if (hasBeenBound()) {
    return ExtendedNominal.getPointer();
  } else if (canNeverBeBound()) {
    return computeExtendedNominal();
  }

  llvm_unreachable(
      "Extension must have already been bound (by bindExtensions)");
}

NominalTypeDecl *ExtensionDecl::computeExtendedNominal() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(
      ctx.evaluator, ExtendedNominalRequest{const_cast<ExtensionDecl *>(this)},
      nullptr);
}

bool ExtensionDecl::canNeverBeBound() const {
  // \c bindExtensions() only looks at valid parents for extensions.
  return !hasValidParent();
}

bool ExtensionDecl::hasValidParent() const {
  return getDeclContext()->canBeParentOfExtension();
}

bool ExtensionDecl::isConstrainedExtension() const {
  auto nominal = getExtendedNominal();
  if (!nominal)
    return false;

  auto typeSig = nominal->getGenericSignature();
  if (!typeSig)
    return false;

  auto extSig = getGenericSignature();
  if (!extSig)
    return false;

  // If the generic signature differs from that of the nominal type, it's a
  // constrained extension.
  return !typeSig->isEqual(extSig);
}

bool ExtensionDecl::isEquivalentToExtendedContext() const {
  auto decl = getExtendedNominal();
  bool extendDeclFromSameModule = false;
  auto extensionAlterName = getAlternateModuleName();
  auto typeAlterName = decl->getAlternateModuleName();

  if (!extensionAlterName.empty()) {
    if (!typeAlterName.empty()) {
      // Case I: type and extension are both moved from somewhere else
      extendDeclFromSameModule = typeAlterName == extensionAlterName;
    } else {
      // Case II: extension alone was moved from somewhere else
      extendDeclFromSameModule = extensionAlterName ==
        decl->getParentModule()->getNameStr();
    }
  } else {
    if (!typeAlterName.empty()) {
      // Case III: extended type alone was moved from somewhere else
      extendDeclFromSameModule = typeAlterName == getParentModule()->getNameStr();
    } else {
      // Case IV: neither of type and extension was moved from somewhere else
      extendDeclFromSameModule = getParentModule() == decl->getParentModule();
    }
  }

  return extendDeclFromSameModule
    && !isConstrainedExtension()
    && !getDeclaredInterfaceType()->isExistentialType();
}

AccessLevel ExtensionDecl::getDefaultAccessLevel() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    DefaultAndMaxAccessLevelRequest{const_cast<ExtensionDecl *>(this)},
    {AccessLevel::Private, AccessLevel::Private}).first;
}

AccessLevel ExtensionDecl::getMaxAccessLevel() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    DefaultAndMaxAccessLevelRequest{const_cast<ExtensionDecl *>(this)},
    {AccessLevel::Private, AccessLevel::Private}).second;
}
      
Type ExtensionDecl::getExtendedType() const {
  ASTContext &ctx = getASTContext();
  if (auto type = evaluateOrDefault(ctx.evaluator,
          ExtendedTypeRequest{const_cast<ExtensionDecl *>(this)},
          Type()))
    return type;
  return ErrorType::get(ctx);
}

PatternBindingDecl::PatternBindingDecl(SourceLoc StaticLoc,
                                       StaticSpellingKind StaticSpelling,
                                       SourceLoc VarLoc,
                                       unsigned NumPatternEntries,
                                       DeclContext *Parent)
  : Decl(DeclKind::PatternBinding, Parent),
    StaticLoc(StaticLoc), VarLoc(VarLoc) {
  Bits.PatternBindingDecl.IsStatic = StaticLoc.isValid();
  Bits.PatternBindingDecl.StaticSpelling =
       static_cast<unsigned>(StaticSpelling);
  Bits.PatternBindingDecl.NumPatternEntries = NumPatternEntries;
}

PatternBindingDecl *
PatternBindingDecl::create(ASTContext &Ctx, SourceLoc StaticLoc,
                           StaticSpellingKind StaticSpelling, SourceLoc VarLoc,
                           Pattern *Pat, SourceLoc EqualLoc, Expr *E,
                           DeclContext *Parent) {
  DeclContext *BindingInitContext = nullptr;
  if (!Parent->isLocalContext())
    BindingInitContext = new (Ctx) PatternBindingInitializer(Parent);

  auto PBE = PatternBindingEntry(Pat, EqualLoc, E, BindingInitContext);
  auto *Result = create(Ctx, StaticLoc, StaticSpelling, VarLoc, PBE, Parent);

  if (BindingInitContext)
    cast<PatternBindingInitializer>(BindingInitContext)->setBinding(Result, 0);

  return Result;
}

PatternBindingDecl *PatternBindingDecl::createImplicit(
    ASTContext &Ctx, StaticSpellingKind StaticSpelling, Pattern *Pat, Expr *E,
    DeclContext *Parent, SourceLoc VarLoc) {
  auto *Result = create(Ctx, /*StaticLoc*/ SourceLoc(), StaticSpelling, VarLoc,
                        Pat, /*EqualLoc*/ SourceLoc(), nullptr, Parent);
  Result->setImplicit();
  Result->setInit(0, E);
  return Result;
}

PatternBindingDecl *PatternBindingDecl::createForDebugger(
    ASTContext &Ctx, StaticSpellingKind StaticSpelling, Pattern *Pat, Expr *E,
    DeclContext *Parent) {
  auto *Result = createImplicit(Ctx, StaticSpelling, Pat, E, Parent);
  Result->Bits.PatternBindingDecl.IsDebugger = true;
  for (auto &entry : Result->getMutablePatternList()) {
    entry.setFromDebugger();
  }
  return Result;
}

PatternBindingDecl *
PatternBindingDecl::create(ASTContext &Ctx, SourceLoc StaticLoc,
                           StaticSpellingKind StaticSpelling,
                           SourceLoc VarLoc,
                           ArrayRef<PatternBindingEntry> PatternList,
                           DeclContext *Parent) {
  size_t Size = totalSizeToAlloc<PatternBindingEntry>(PatternList.size());
  void *D = allocateMemoryForDecl<PatternBindingDecl>(Ctx, Size,
                                                      /*ClangNode*/false);
  auto PBD = ::new (D) PatternBindingDecl(StaticLoc, StaticSpelling, VarLoc,
                                          PatternList.size(), Parent);

  // Set up the patterns.
  auto entries = PBD->getMutablePatternList();
  unsigned elt = 0U-1;
  for (auto pe : PatternList) {
    ++elt;
    auto &newEntry = entries[elt];
    newEntry = pe; // This should take care of initializer with flags
    DeclContext *initContext = pe.getInitContext();
    if (!initContext && !Parent->isLocalContext()) {
      auto pbi = new (Ctx) PatternBindingInitializer(Parent);
      pbi->setBinding(PBD, elt);
      initContext = pbi;
    }

    PBD->setPattern(elt, pe.getPattern(), initContext);
  }
  return PBD;
}

PatternBindingDecl *PatternBindingDecl::createDeserialized(
                      ASTContext &Ctx, SourceLoc StaticLoc,
                      StaticSpellingKind StaticSpelling,
                      SourceLoc VarLoc,
                      unsigned NumPatternEntries,
                      DeclContext *Parent) {
  size_t Size = totalSizeToAlloc<PatternBindingEntry>(NumPatternEntries);
  void *D = allocateMemoryForDecl<PatternBindingDecl>(Ctx, Size,
                                                      /*ClangNode*/false);
  auto PBD = ::new (D) PatternBindingDecl(StaticLoc, StaticSpelling, VarLoc,
                                          NumPatternEntries, Parent);
  for (auto &entry : PBD->getMutablePatternList()) {
    entry = PatternBindingEntry(/*Pattern*/ nullptr, /*EqualLoc*/ SourceLoc(),
                                /*Init*/ nullptr, /*InitContext*/ nullptr);
  }
  return PBD;
}

ParamDecl *PatternBindingInitializer::getImplicitSelfDecl() const {
  if (SelfParam)
    return SelfParam;

  if (auto singleVar = getInitializedLazyVar()) {
    auto DC = singleVar->getDeclContext();
    if (DC->isTypeContext()) {
      auto specifier = (DC->getDeclaredInterfaceType()->hasReferenceSemantics()
                        ? ParamSpecifier::Default
                        : ParamSpecifier::InOut);

      ASTContext &C = DC->getASTContext();
      auto *mutableThis = const_cast<PatternBindingInitializer *>(this);
      auto *LazySelfParam = new (C) ParamDecl(SourceLoc(), SourceLoc(),
                                    Identifier(), singleVar->getLoc(),
                                    C.Id_self, mutableThis);
      LazySelfParam->setImplicit();
      LazySelfParam->setSpecifier(specifier);
      LazySelfParam->setInterfaceType(DC->getSelfInterfaceType());

      // Lazy members of actors have an isolated 'self', assuming there is
      // no "nonisolated" attribute.
      if (auto nominal = DC->getSelfNominalTypeDecl()) {
        if (nominal->isActor() &&
            !singleVar->getAttrs().hasAttribute<NonisolatedAttr>())
          LazySelfParam->setIsolated();
      }

      mutableThis->SelfParam = LazySelfParam;
    }
  }

  return SelfParam;
}

VarDecl *PatternBindingInitializer::getInitializedLazyVar() const {
  if (auto binding = getBinding()) {
    if (auto var = binding->getSingleVar()) {
      if (var->getAttrs().hasAttribute<LazyAttr>())
        return var;
    }
  }
  return nullptr;
}

unsigned PatternBindingDecl::getPatternEntryIndexForVarDecl(const VarDecl *VD) const {
  assert(VD && "Cannot find a null VarDecl");
  
  auto List = getPatternList();
  if (List.size() == 1) {
    assert(List[0].getPattern()->containsVarDecl(VD) &&
           "Single entry PatternBindingDecl is set up wrong");
    return 0;
  }
  
  unsigned Result = 0;
  for (auto entry : List) {
    if (entry.getPattern()->containsVarDecl(VD))
      return Result;
    ++Result;
  }
  
  assert(0 && "PatternBindingDecl doesn't bind the specified VarDecl!");
  return ~0U;
}

Expr *PatternBindingEntry::getOriginalInit() const {
  return InitContextAndFlags.getInt().contains(PatternFlags::IsText)
             ? nullptr
             : InitExpr.originalInit;
}

SourceRange PatternBindingEntry::getOriginalInitRange() const {
  if (auto *i = getOriginalInit())
    return i->getSourceRange();
  return SourceRange();
}

void PatternBindingEntry::setOriginalInit(Expr *E) {
  InitExpr.originalInit = E;
  InitContextAndFlags.setInt(InitContextAndFlags.getInt() -
                             PatternFlags::IsText);
}

bool PatternBindingEntry::isInitialized(bool onlyExplicit) const {
  // Directly initialized.
  if (getInit() && (!onlyExplicit || getEqualLoc().isValid()))
    return true;

  // Initialized via a property wrapper.
  if (auto var = getPattern()->getSingleVar()) {
    auto customAttrs = var->getAttachedPropertyWrappers();
    if (customAttrs.size() > 0 && customAttrs[0]->hasArgs())
      return true;
  }

  return false;
}

void PatternBindingEntry::setInit(Expr *E) {
  auto F = PatternAndFlags.getInt();
  if (E) {
    PatternAndFlags.setInt(F - Flags::Removed);
  } else {
    PatternAndFlags.setInt(F | Flags::Removed);
  }
  InitExpr.initAfterSynthesis = E;
  InitContextAndFlags.setInt(InitContextAndFlags.getInt() -
                             PatternFlags::IsText);
}

VarDecl *PatternBindingEntry::getAnchoringVarDecl() const {
  SmallVector<VarDecl *, 8> variables;
  getPattern()->collectVariables(variables);
  if (variables.empty())
    return nullptr;
  return variables[0];
}

SourceLoc PatternBindingEntry::getLastAccessorEndLoc() const {
  SourceLoc lastAccessorEnd;
  getPattern()->forEachVariable([&](VarDecl *var) {
    auto accessorsEndLoc = var->getBracesRange().End;
    if (accessorsEndLoc.isValid())
      lastAccessorEnd = accessorsEndLoc;
  });
  return lastAccessorEnd;
}

SourceLoc PatternBindingEntry::getStartLoc() const {
  return getPattern()->getStartLoc();
}

SourceLoc PatternBindingEntry::getEndLoc(bool omitAccessors) const {
  // Accessors are last
  if (!omitAccessors) {
    const auto lastAccessorEnd = getLastAccessorEndLoc();
    if (lastAccessorEnd.isValid())
      return lastAccessorEnd;
  }
  const auto initEnd = getOriginalInitRange().End;
  if (initEnd.isValid())
    return initEnd;

  return getPattern()->getEndLoc();
}

SourceRange PatternBindingEntry::getSourceRange(bool omitAccessors) const {
  const SourceLoc startLoc = getStartLoc();
  if (startLoc.isInvalid())
    return SourceRange();
  const SourceLoc endLoc = getEndLoc(omitAccessors);
  if (endLoc.isInvalid())
    return SourceRange();
  return SourceRange(startLoc, endLoc);
}

bool PatternBindingEntry::hasInitStringRepresentation() const {
  if (InitContextAndFlags.getInt().contains(PatternFlags::IsText))
    return !InitStringRepresentation.empty();
  return getOriginalInit() && getOriginalInit()->getSourceRange().isValid();
}

StringRef PatternBindingEntry::getInitStringRepresentation(
  SmallVectorImpl<char> &scratch) const {

  assert(hasInitStringRepresentation() &&
         "must check if pattern has string representation");

  if (InitContextAndFlags.getInt().contains(PatternFlags::IsText) &&
      !InitStringRepresentation.empty())
    return InitStringRepresentation;
  auto &sourceMgr = getAnchoringVarDecl()->getASTContext().SourceMgr;
  auto init = getOriginalInit();
  return extractInlinableText(sourceMgr, init, scratch);
}

SourceRange PatternBindingDecl::getSourceRange() const {
  SourceLoc startLoc = getStartLoc();
  SourceLoc endLoc = getPatternList().back().getSourceRange().End;
  if (startLoc.isValid() != endLoc.isValid()) return SourceRange();
  return { startLoc, endLoc };
}

static StaticSpellingKind getCorrectStaticSpellingForDecl(const Decl *D) {
  if (auto classDecl = D->getDeclContext()->getSelfClassDecl()) {
    if (!classDecl->isActor())
      return StaticSpellingKind::KeywordClass;
  }

  return StaticSpellingKind::KeywordStatic;
}

StaticSpellingKind PatternBindingDecl::getCorrectStaticSpelling() const {
  if (!isStatic())
    return StaticSpellingKind::None;
  if (getStaticSpelling() != StaticSpellingKind::None)
    return getStaticSpelling();

  return getCorrectStaticSpellingForDecl(this);
}

bool PatternBindingDecl::isAsyncLet() const {
  if (auto var = getAnchoringVarDecl(0))
    return var->isAsyncLet();

  return false;
}


bool PatternBindingDecl::hasStorage() const {
  // Walk the pattern, to check to see if any of the VarDecls included in it
  // have storage.
  for (auto entry : getPatternList())
    if (entry.getPattern()->hasStorage())
      return true;
  return false;
}

void PatternBindingDecl::setPattern(unsigned i, Pattern *P,
                                    DeclContext *InitContext,
                                    bool isFullyValidated) {
  auto PatternList = getMutablePatternList();
  PatternList[i].setPattern(P);
  PatternList[i].setInitContext(InitContext);
  
  // Make sure that any VarDecl's contained within the pattern know about this
  // PatternBindingDecl as their parent.
  if (P) {
    P->forEachVariable([&](VarDecl *VD) {
      if (!VD->isCaptureList())
        VD->setParentPatternBinding(this);
    });

    if (isFullyValidated)
      PatternList[i].setFullyValidated();
  }
}


VarDecl *PatternBindingDecl::getSingleVar() const {
  if (getNumPatternEntries() == 1)
    return getPatternList()[0].getPattern()->getSingleVar();
  return nullptr;
}

VarDecl *PatternBindingDecl::getAnchoringVarDecl(unsigned i) const {
  return getPatternList()[i].getAnchoringVarDecl();
}

bool VarDecl::isInitExposedToClients() const {
  // 'lazy' initializers are emitted inside the getter, which is never
  // @inlinable.
  if (getAttrs().hasAttribute<LazyAttr>())
    return false;

  return hasInitialValue() && isLayoutExposedToClients();
}

bool VarDecl::isLayoutExposedToClients() const {
  auto parent = dyn_cast<NominalTypeDecl>(getDeclContext());
  if (!parent) return false;
  if (isStatic()) return false;

  if (!hasStorage() &&
      !getAttrs().hasAttribute<LazyAttr>() &&
      !hasAttachedPropertyWrapper()) {
    return false;
  }

  auto nominalAccess =
    parent->getFormalAccessScope(/*useDC=*/nullptr,
                                 /*treatUsableFromInlineAsPublic=*/true);
  if (!nominalAccess.isPublic()) return false;

  return (parent->getAttrs().hasAttribute<FrozenAttr>() ||
          parent->getAttrs().hasAttribute<FixedLayoutAttr>());
}

/// Check whether the given type representation will be
/// default-initializable.
static bool isDefaultInitializable(const TypeRepr *typeRepr, ASTContext &ctx) {
  // Look through most attributes.
  if (const auto attributed = dyn_cast<AttributedTypeRepr>(typeRepr)) {
    // Ownership kinds have optionalness requirements.
    if (optionalityOf(attributed->getAttrs().getOwnership()) ==
        ReferenceOwnershipOptionality::Required)
      return true;

    return isDefaultInitializable(attributed->getTypeRepr(), ctx);
  }

  // Optional types are default-initializable.
  if (isa<OptionalTypeRepr>(typeRepr) ||
      isa<ImplicitlyUnwrappedOptionalTypeRepr>(typeRepr))
    return true;

  // Also support the desugared 'Optional<T>' spelling.
  if (!ctx.isSwiftVersionAtLeast(5)) {
    if (auto *identRepr = dyn_cast<SimpleIdentTypeRepr>(typeRepr)) {
      if (identRepr->getNameRef().getBaseIdentifier() == ctx.Id_Void)
        return true;
    }

    if (auto *identRepr = dyn_cast<GenericIdentTypeRepr>(typeRepr)) {
      if (identRepr->getNameRef().getBaseIdentifier() == ctx.Id_Optional &&
          identRepr->getNumGenericArgs() == 1)
        return true;
    }
  }

  // Tuple types are default-initializable if all of their element
  // types are.
  if (const auto tuple = dyn_cast<TupleTypeRepr>(typeRepr)) {
    // ... but not variadic ones.
    if (tuple->hasEllipsis())
      return false;

    for (const auto &elt : tuple->getElements()) {
      if (!isDefaultInitializable(elt.Type, ctx))
        return false;
    }

    return true;
  }

  // Not default initializable.
  return false;
}

// @NSManaged properties never get default initialized, nor do debugger
// variables and immutable properties.
bool Pattern::isNeverDefaultInitializable() const {
  bool result = false;

  forEachVariable([&](const VarDecl *var) {
    if (var->getAttrs().hasAttribute<NSManagedAttr>())
      return;

    if (var->isDebuggerVar() ||
        var->isLet())
      result = true;
  });

  return result;
}

bool PatternBindingDecl::isDefaultInitializableViaPropertyWrapper(unsigned i) const {
  if (auto singleVar = getSingleVar()) {
    if (auto wrapperInfo = singleVar->getAttachedPropertyWrapperTypeInfo(0)) {
      if (wrapperInfo.defaultInit)
        return true;
    }
  }

  return false;
}

bool PatternBindingDecl::isDefaultInitializable(unsigned i) const {
  const auto entry = getPatternList()[i];

  // If it has an initializer expression, this is trivially true.
  if (entry.isInitialized())
    return true;

  // If the outermost attached property wrapper vends an `init()`, use that
  // for default initialization.
  if (isDefaultInitializableViaPropertyWrapper(i))
    return true;

  // If one of the attached wrappers is missing a wrappedValue
  // initializer, cannot default-initialize.
  if (auto singleVar = getSingleVar()) {
    if (auto wrapperInfo = singleVar->getAttachedPropertyWrapperTypeInfo(0)) {
      if (!singleVar->allAttachedPropertyWrappersHaveWrappedValueInit())
        return false;
    }
  }

  if (entry.getPattern()->isNeverDefaultInitializable())
    return false;

  auto &ctx = getASTContext();

  // If the pattern is typed as optional (or tuples thereof), it is
  // default initializable.
  if (const auto typedPattern = dyn_cast<TypedPattern>(entry.getPattern())) {
    if (const auto typeRepr = typedPattern->getTypeRepr()) {
      if (::isDefaultInitializable(typeRepr, ctx))
        return true;
    } else if (typedPattern->isImplicit()) {
      // Lazy vars have implicit storage assigned to back them. Because the
      // storage is implicit, the pattern is typed and has a TypeLoc, but not a
      // TypeRepr.
      //
      // All lazy storage is implicitly default initializable, though, because
      // lazy backing storage is optional.
      if (const auto *varDecl = typedPattern->getSingleVar()) {
        // Lazy storage is never user accessible.
        if (!varDecl->isUserAccessible()) {
          if (typedPattern->hasType() &&
              typedPattern->getType()->getOptionalObjectType()) {
            return true;
          }
        }
      }
    }
  }

  // Otherwise, we can't default initialize this binding.
  return false;
}


bool PatternBindingDecl::isComputingPatternBindingEntry(
    const VarDecl *vd) const {
  unsigned i = getPatternEntryIndexForVarDecl(vd);
  return getASTContext().evaluator.hasActiveRequest(
      PatternBindingEntryRequest{const_cast<PatternBindingDecl *>(this), i});
}

bool PatternBindingDecl::isExplicitlyInitialized(unsigned i) const {
  const auto &entry = getPatternList()[i];
  return entry.isInitialized(/*onlyExplicit=*/true);
}

SourceLoc PatternBindingDecl::getEqualLoc(unsigned i) const {
  const auto &entry = getPatternList()[i];
  return entry.getEqualLoc();
}

SourceLoc TopLevelCodeDecl::getStartLoc() const {
  return Body->getStartLoc();
}

SourceRange TopLevelCodeDecl::getSourceRange() const {
  return Body->getSourceRange();
}

SourceRange IfConfigDecl::getSourceRange() const {
  return SourceRange(getLoc(), EndLoc);
}

static bool isPolymorphic(const AbstractStorageDecl *storage) {
  if (storage->shouldUseObjCDispatch())
    return true;


  // Imported declarations behave like they are dynamic, even if they're
  // not marked as such explicitly.
  if (storage->isObjC() && storage->hasClangNode())
    return true;

  if (auto *classDecl = dyn_cast<ClassDecl>(storage->getDeclContext())) {
    if (storage->isFinal() || classDecl->isFinal())
      return false;

    return true;
  }

  if (isa<ProtocolDecl>(storage->getDeclContext()))
    return true;

  return false;
}

static bool isDirectToStorageAccess(const DeclContext *UseDC,
                                    const VarDecl *var, bool isAccessOnSelf) {
  if (!var->hasStorage())
    return false;

  auto *AFD = dyn_cast<AbstractFunctionDecl>(UseDC);
  if (AFD == nullptr)
    return false;

  // The property reference is for immediate class, not a derived class.
  if (AFD->getParent()->getSelfNominalTypeDecl() !=
      var->getDeclContext()->getSelfNominalTypeDecl())
    return false;

  // If the storage is resilient, we cannot access it directly at all.
  if (var->isResilient(UseDC->getParentModule(),
                       UseDC->getResilienceExpansion()))
    return var->getModuleContext()->getBypassResilience();

  if (isa<ConstructorDecl>(AFD) || isa<DestructorDecl>(AFD)) {
    // The access must also be a member access on 'self' in all language modes.
    if (!isAccessOnSelf)
      return false;

    return true;
  } else if (auto *accessor = dyn_cast<AccessorDecl>(AFD)) {
    // The accessor must be for the variable itself.
    if (accessor->getStorage() != var)
      return false;

    // In Swift 5 and later, the access must also be a member access on 'self'.
    if (!isAccessOnSelf &&
        var->getDeclContext()->isTypeContext() &&
        var->getASTContext().isSwiftVersionAtLeast(5))
      return false;

    // As a special case, 'read' and 'modify' coroutines with forced static
    // dispatch must use ordinary semantics, so that the 'modify' coroutine for a
    // 'dynamic' property uses Objective-C message sends and not direct access to
    // storage.
    if (accessor->hasForcedStaticDispatch())
      return false;

    return true;
  }

  return false;
}

/// Determines the access semantics to use in a DeclRefExpr or
/// MemberRefExpr use of this value in the specified context.
AccessSemantics
ValueDecl::getAccessSemanticsFromContext(const DeclContext *UseDC,
                                         bool isAccessOnSelf) const {
  if (auto *var = dyn_cast<VarDecl>(this))
    if (isDirectToStorageAccess(UseDC, var, isAccessOnSelf))
      return AccessSemantics::DirectToStorage;

  // Otherwise, it's a semantically normal access.  The client should be
  // able to figure out the most efficient way to do this access.
  return AccessSemantics::Ordinary;
}

static AccessStrategy
getDirectReadAccessStrategy(const AbstractStorageDecl *storage) {
  switch (storage->getReadImpl()) {
  case ReadImplKind::Stored:
    return AccessStrategy::getStorage();
  case ReadImplKind::Inherited:
    // TODO: maybe add a specific strategy for this?
    return AccessStrategy::getAccessor(AccessorKind::Get,
                                       /*dispatch*/ false);
  case ReadImplKind::Get:
    return AccessStrategy::getAccessor(AccessorKind::Get,
                                       /*dispatch*/ false);
  case ReadImplKind::Address:
    return AccessStrategy::getAccessor(AccessorKind::Address,
                                       /*dispatch*/ false);
  case ReadImplKind::Read:
    return AccessStrategy::getAccessor(AccessorKind::Read,
                                       /*dispatch*/ false);
  }
  llvm_unreachable("bad impl kind");
}

static AccessStrategy
getDirectWriteAccessStrategy(const AbstractStorageDecl *storage) {
  switch (storage->getWriteImpl()) {
  case WriteImplKind::Immutable:
    assert(isa<VarDecl>(storage) && cast<VarDecl>(storage)->isLet() &&
           "mutation of a immutable variable that isn't a let");
    return AccessStrategy::getStorage();
  case WriteImplKind::Stored:
    return AccessStrategy::getStorage();
  case WriteImplKind::StoredWithObservers:
    // TODO: maybe add a specific strategy for this?
    return AccessStrategy::getAccessor(AccessorKind::Set,
                                       /*dispatch*/ false);
  case WriteImplKind::InheritedWithObservers:
    // TODO: maybe add a specific strategy for this?
    return AccessStrategy::getAccessor(AccessorKind::Set,
                                       /*dispatch*/ false);
  case WriteImplKind::Set:
    return AccessStrategy::getAccessor(AccessorKind::Set,
                                       /*dispatch*/ false);
  case WriteImplKind::MutableAddress:
    return AccessStrategy::getAccessor(AccessorKind::MutableAddress,
                                       /*dispatch*/ false);
  case WriteImplKind::Modify:
    return AccessStrategy::getAccessor(AccessorKind::Modify,
                                       /*dispatch*/ false);
  }
  llvm_unreachable("bad impl kind");
}

static AccessStrategy
getOpaqueReadAccessStrategy(const AbstractStorageDecl *storage, bool dispatch);
static AccessStrategy
getOpaqueWriteAccessStrategy(const AbstractStorageDecl *storage, bool dispatch);

static AccessStrategy
getDirectReadWriteAccessStrategy(const AbstractStorageDecl *storage) {
  switch (storage->getReadWriteImpl()) {
  case ReadWriteImplKind::Immutable:
    assert(isa<VarDecl>(storage) && cast<VarDecl>(storage)->isLet() &&
           "mutation of a immutable variable that isn't a let");
    return AccessStrategy::getStorage();
  case ReadWriteImplKind::Stored: {
    // If the storage isDynamic (and not @objc) use the accessors.
    if (storage->shouldUseNativeDynamicDispatch())
      return AccessStrategy::getMaterializeToTemporary(
          getOpaqueReadAccessStrategy(storage, false),
          getOpaqueWriteAccessStrategy(storage, false));
    return AccessStrategy::getStorage();
  }
  case ReadWriteImplKind::MutableAddress:
    return AccessStrategy::getAccessor(AccessorKind::MutableAddress,
                                       /*dispatch*/ false);
  case ReadWriteImplKind::Modify:
    return AccessStrategy::getAccessor(AccessorKind::Modify,
                                       /*dispatch*/ false);
  case ReadWriteImplKind::StoredWithDidSet:
  case ReadWriteImplKind::InheritedWithDidSet:
    if (storage->requiresOpaqueModifyCoroutine() &&
        storage->getParsedAccessor(AccessorKind::DidSet)->isSimpleDidSet()) {
      return AccessStrategy::getAccessor(AccessorKind::Modify,
                                         /*dispatch*/ false);
    } else {
      return AccessStrategy::getMaterializeToTemporary(
          getDirectReadAccessStrategy(storage),
          getDirectWriteAccessStrategy(storage));
    }
  case ReadWriteImplKind::MaterializeToTemporary:
    return AccessStrategy::getMaterializeToTemporary(
                                       getDirectReadAccessStrategy(storage),
                                       getDirectWriteAccessStrategy(storage));
  }
  llvm_unreachable("bad impl kind");
}

static AccessStrategy
getOpaqueReadAccessStrategy(const AbstractStorageDecl *storage, bool dispatch) {
  if (storage->requiresOpaqueReadCoroutine())
    return AccessStrategy::getAccessor(AccessorKind::Read, dispatch);
  return AccessStrategy::getAccessor(AccessorKind::Get, dispatch);
}

static AccessStrategy
getOpaqueWriteAccessStrategy(const AbstractStorageDecl *storage, bool dispatch){
  return AccessStrategy::getAccessor(AccessorKind::Set, dispatch);
}

static AccessStrategy
getOpaqueReadWriteAccessStrategy(const AbstractStorageDecl *storage,
                                 bool dispatch) {
  if (storage->requiresOpaqueModifyCoroutine())
    return AccessStrategy::getAccessor(AccessorKind::Modify, dispatch);
  return AccessStrategy::getMaterializeToTemporary(
           getOpaqueReadAccessStrategy(storage, dispatch),
           getOpaqueWriteAccessStrategy(storage, dispatch));
}

static AccessStrategy
getOpaqueAccessStrategy(const AbstractStorageDecl *storage,
                        AccessKind accessKind, bool dispatch) {
  switch (accessKind) {
  case AccessKind::Read:
    return getOpaqueReadAccessStrategy(storage, dispatch);
  case AccessKind::Write:
    return getOpaqueWriteAccessStrategy(storage, dispatch);
  case AccessKind::ReadWrite:
    return getOpaqueReadWriteAccessStrategy(storage, dispatch);
  }
  llvm_unreachable("bad access kind");
}

AccessStrategy
AbstractStorageDecl::getAccessStrategy(AccessSemantics semantics,
                                       AccessKind accessKind,
                                       ModuleDecl *module,
                                       ResilienceExpansion expansion) const {
  switch (semantics) {
  case AccessSemantics::DirectToStorage:
    assert(hasStorage());
    return AccessStrategy::getStorage();

  case AccessSemantics::Ordinary:
    // Skip these checks for local variables, both because they're unnecessary
    // and because we won't necessarily have computed access.
    if (!getDeclContext()->isLocalContext()) {
      // If the property is defined in a non-final class or a protocol, the
      // accessors are dynamically dispatched, and we cannot do direct access.
      if (isPolymorphic(this))
        return getOpaqueAccessStrategy(this, accessKind, /*dispatch*/ true);

      if (shouldUseNativeDynamicDispatch())
        return getOpaqueAccessStrategy(this, accessKind, /*dispatch*/ false);

      // If the storage is resilient from the given module and resilience
      // expansion, we cannot use direct access.
      //
      // If we end up here with a stored property of a type that's resilient
      // from some resilience domain, we cannot do direct access.
      //
      // As an optimization, we do want to perform direct accesses of stored
      // properties declared inside the same resilience domain as the access
      // context.
      //
      // This is done by using DirectToStorage semantics above, with the
      // understanding that the access semantics are with respect to the
      // resilience domain of the accessor's caller.
      bool resilient;
      if (module)
        resilient = isResilient(module, expansion);
      else
        resilient = isResilient();

      if (resilient)
        return getOpaqueAccessStrategy(this, accessKind, /*dispatch*/ false);
    }

    LLVM_FALLTHROUGH;

  case AccessSemantics::DirectToImplementation:
    switch (accessKind) {
    case AccessKind::Read:
      return getDirectReadAccessStrategy(this);
    case AccessKind::Write:
      return getDirectWriteAccessStrategy(this);
    case AccessKind::ReadWrite:
      return getDirectReadWriteAccessStrategy(this);
    }
    llvm_unreachable("bad access kind");

  }
  llvm_unreachable("bad access semantics");
}

bool AbstractStorageDecl::requiresOpaqueAccessors() const {
  // Subscripts always require opaque accessors, so don't even kick off
  // a request.
  auto *var = dyn_cast<VarDecl>(this);
  if (var == nullptr)
    return true;

  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    RequiresOpaqueAccessorsRequest{const_cast<VarDecl *>(var)},
    false);
}

bool AbstractStorageDecl::requiresOpaqueAccessor(AccessorKind kind) const {
  switch (kind) {
  case AccessorKind::Get:
    return requiresOpaqueGetter();
  case AccessorKind::Set:
    return requiresOpaqueSetter();
  case AccessorKind::Read:
    return requiresOpaqueReadCoroutine();
  case AccessorKind::Modify:
    return requiresOpaqueModifyCoroutine();

  // Other accessors are never part of the opaque-accessors set.
#define OPAQUE_ACCESSOR(ID, KEYWORD)
#define ACCESSOR(ID) \
  case AccessorKind::ID:
#include "swift/AST/AccessorKinds.def"
    return false;
  }
  llvm_unreachable("bad accessor kind");
}

bool AbstractStorageDecl::requiresOpaqueModifyCoroutine() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    RequiresOpaqueModifyCoroutineRequest{const_cast<AbstractStorageDecl *>(this)},
    false);
}

AccessorDecl *AbstractStorageDecl::getSynthesizedAccessor(AccessorKind kind) const {
  if (auto *accessor = getAccessor(kind))
    return accessor;

  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    SynthesizeAccessorRequest{const_cast<AbstractStorageDecl *>(this), kind},
    nullptr);
}

AccessorDecl *AbstractStorageDecl::getEffectfulGetAccessor() const {
    if (getAllAccessors().size() != 1)
      return nullptr;

    if (auto accessor = getAccessor(AccessorKind::Get))
      if (accessor->hasAsync() || accessor->hasThrows())
        return accessor;

  return nullptr;
}

bool AbstractStorageDecl::isLessEffectfulThan(AbstractStorageDecl const* other,
                                              EffectKind kind) const {
  bool allowedByOther = false;
  if (auto otherGetter = other->getEffectfulGetAccessor())
    allowedByOther = otherGetter->hasEffect(kind);

  if (auto getter = getEffectfulGetAccessor())
    if (getter->hasEffect(kind) && !allowedByOther)
      return false; // has the effect when other does not; it's more effectful!

  return true; // OK
}

AccessorDecl *AbstractStorageDecl::getOpaqueAccessor(AccessorKind kind) const {
  auto *accessor = getAccessor(kind);
  if (accessor && !accessor->isImplicit())
    return accessor;

  if (!requiresOpaqueAccessors())
    return nullptr;

  if (!requiresOpaqueAccessor(kind))
    return nullptr;

  return getSynthesizedAccessor(kind);
}

ArrayRef<AccessorDecl*> AbstractStorageDecl::getOpaqueAccessors(
    llvm::SmallVectorImpl<AccessorDecl*> &scratch) const {
  visitOpaqueAccessors([&](AccessorDecl *D) { scratch.push_back(D); });
  return scratch;
}

bool AbstractStorageDecl::hasParsedAccessors() const {
  for (auto *accessor : getAllAccessors())
    if (!accessor->isImplicit())
      return true;
  return false;
}

AccessorDecl *AbstractStorageDecl::getParsedAccessor(AccessorKind kind) const {
  auto *accessor = getAccessor(kind);
  if (accessor && !accessor->isImplicit())
    return accessor;

  return nullptr;
}

void AbstractStorageDecl::visitParsedAccessors(
                        llvm::function_ref<void (AccessorDecl*)> visit) const {
  for (auto *accessor : getAllAccessors())
    if (!accessor->isImplicit())
      visit(accessor);
}

void AbstractStorageDecl::visitEmittedAccessors(
                        llvm::function_ref<void (AccessorDecl*)> visit) const {
  visitParsedAccessors(visit);
  visitOpaqueAccessors([&](AccessorDecl *accessor) {
    if (accessor->isImplicit())
      visit(accessor);
  });
}

void AbstractStorageDecl::visitExpectedOpaqueAccessors(
                        llvm::function_ref<void (AccessorKind)> visit) const {
  if (!requiresOpaqueAccessors())
    return;

  if (requiresOpaqueGetter())
    visit(AccessorKind::Get);

  if (requiresOpaqueReadCoroutine())
    visit(AccessorKind::Read);

  // All mutable storage should have a setter.
  if (requiresOpaqueSetter())
    visit(AccessorKind::Set);

  // Include the modify coroutine if it's required.
  if (requiresOpaqueModifyCoroutine())
    visit(AccessorKind::Modify);
}

void AbstractStorageDecl::visitOpaqueAccessors(
                        llvm::function_ref<void (AccessorDecl*)> visit) const {
  visitExpectedOpaqueAccessors([&](AccessorKind kind) {
    auto accessor = getSynthesizedAccessor(kind);
    assert(!accessor->hasForcedStaticDispatch() &&
            "opaque accessor with forced static dispatch?");
    visit(accessor);
  });
}

static bool hasPrivateOrFilePrivateFormalAccess(const ValueDecl *D) {
  return D->getFormalAccess() <= AccessLevel::FilePrivate;
}

/// Returns true if one of the ancestor DeclContexts of this ValueDecl is either
/// marked private or fileprivate or is a local context.
static bool isInPrivateOrLocalContext(const ValueDecl *D) {
  const DeclContext *DC = D->getDeclContext();
  if (!DC->isTypeContext()) {
    assert((DC->isModuleScopeContext() || DC->isLocalContext()) &&
           "unexpected context kind");
    return DC->isLocalContext();
  }

  auto *nominal = DC->getSelfNominalTypeDecl();
  if (nominal == nullptr)
    return false;

  if (hasPrivateOrFilePrivateFormalAccess(nominal))
    return true;
  return isInPrivateOrLocalContext(nominal);
}

bool ValueDecl::isOutermostPrivateOrFilePrivateScope() const {
  return hasPrivateOrFilePrivateFormalAccess(this) &&
         !isInPrivateOrLocalContext(this);
}

bool AbstractStorageDecl::isFormallyResilient() const {
  // Check for an explicit @_fixed_layout attribute.
  if (getAttrs().hasAttribute<FixedLayoutAttr>())
    return false;

  // If we're an instance property of a nominal type, query the type.
  auto *dc = getDeclContext();
  if (!isStatic())
    if (auto *nominalDecl = dc->getSelfNominalTypeDecl())
      return nominalDecl->isResilient();

  // Non-public global and static variables always have a
  // fixed layout.
  if (!getFormalAccessScope(/*useDC=*/nullptr,
                            /*treatUsableFromInlineAsPublic=*/true).isPublic())
    return false;

  return true;
}

bool AbstractStorageDecl::isResilient() const {
  if (!isFormallyResilient())
    return false;

  return getModuleContext()->isResilient();
}

bool AbstractStorageDecl::isResilient(ModuleDecl *M,
                                      ResilienceExpansion expansion) const {
  switch (expansion) {
  case ResilienceExpansion::Minimal:
    return isResilient();
  case ResilienceExpansion::Maximal:
    return M != getModuleContext() && isResilient();
  }
  llvm_unreachable("bad resilience expansion");
}

bool AbstractStorageDecl::isValidKeyPathComponent() const {
  // Check whether we're an ABI compatible override of another property. If we
  // are, then the key path should refer to the base decl instead.
  auto &ctx = getASTContext();
  auto isABICompatibleOverride = evaluateOrDefault(
      ctx.evaluator,
      IsABICompatibleOverrideRequest{const_cast<AbstractStorageDecl *>(this)},
      false);
  return !isABICompatibleOverride;
}

bool AbstractStorageDecl::isGetterMutating() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    IsGetterMutatingRequest{const_cast<AbstractStorageDecl *>(this)}, {});
}

bool AbstractStorageDecl::isSetterMutating() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    IsSetterMutatingRequest{const_cast<AbstractStorageDecl *>(this)}, {});
}

OpaqueReadOwnership AbstractStorageDecl::getOpaqueReadOwnership() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    OpaqueReadOwnershipRequest{const_cast<AbstractStorageDecl *>(this)}, {});
}

bool ValueDecl::isInstanceMember() const {
  DeclContext *DC = getDeclContext();
  if (!DC->isTypeContext())
    return false;

  switch (getKind()) {
  case DeclKind::Import:
  case DeclKind::Extension:
  case DeclKind::PatternBinding:
  case DeclKind::EnumCase:
  case DeclKind::TopLevelCode:
  case DeclKind::InfixOperator:
  case DeclKind::PrefixOperator:
  case DeclKind::PostfixOperator:
  case DeclKind::IfConfig:
  case DeclKind::PoundDiagnostic:
  case DeclKind::PrecedenceGroup:
  case DeclKind::MissingMember:
    llvm_unreachable("Not a ValueDecl");

  case DeclKind::Class:
  case DeclKind::Enum:
  case DeclKind::Protocol:
  case DeclKind::Struct:
  case DeclKind::TypeAlias:
  case DeclKind::GenericTypeParam:
  case DeclKind::AssociatedType:
  case DeclKind::OpaqueType:
    // Types are not instance members.
    return false;

  case DeclKind::Constructor:
    // Constructors are not instance members.
    return false;

  case DeclKind::Destructor:
    // Destructors are technically instance members, although they
    // can't actually be referenced as such.
    return true;

  case DeclKind::Func:
  case DeclKind::Accessor:
    // Non-static methods are instance members.
    return !cast<FuncDecl>(this)->isStatic();

  case DeclKind::EnumElement:
  case DeclKind::Param:
    // enum elements and function parameters are not instance members.
    return false;

  case DeclKind::Subscript:
  case DeclKind::Var:
    // Non-static variables and subscripts are instance members.
    return !cast<AbstractStorageDecl>(this)->isStatic();

  case DeclKind::Module:
    // Modules are never instance members.
    return false;
  }
  llvm_unreachable("bad DeclKind");
}

unsigned ValueDecl::getLocalDiscriminator() const {
  return LocalDiscriminator;
}

void ValueDecl::setLocalDiscriminator(unsigned index) {
  assert(getDeclContext()->isLocalContext());
  assert(LocalDiscriminator == 0 && "LocalDiscriminator is set multiple times");
  LocalDiscriminator = index;
}

ValueDecl *ValueDecl::getOverriddenDecl() const {
  auto overridden = getOverriddenDecls();
  if (overridden.empty()) return nullptr;

  // FIXME: Arbitrarily pick the first overridden declaration.
  return overridden.front();
}

bool ValueDecl::overriddenDeclsComputed() const {
  return LazySemanticInfo.hasOverriddenComputed;
}

bool swift::conflicting(const OverloadSignature& sig1,
                        const OverloadSignature& sig2,
                        bool skipProtocolExtensionCheck) {
  // A member of a protocol extension never conflicts with a member of a
  // protocol.
  if (!skipProtocolExtensionCheck &&
      sig1.InProtocolExtension != sig2.InProtocolExtension)
    return false;

  // If the base names are different, they can't conflict.
  if (sig1.Name.getBaseName() != sig2.Name.getBaseName())
    return false;

  // If one is an operator and the other is not, they can't conflict.
  if (sig1.UnaryOperator != sig2.UnaryOperator)
    return false;

  // If one is an instance and the other is not, they can't conflict.
  if (sig1.IsInstanceMember != sig2.IsInstanceMember)
    return false;

  // If one is an async function and the other is not, they can't conflict.
  if (sig1.IsAsyncFunction != sig2.IsAsyncFunction)
    return false;

  // If one is a compound name and the other is not, they do not conflict
  // if one is a property and the other is a non-nullary function.
  if (sig1.Name.isCompoundName() != sig2.Name.isCompoundName()) {
    return !((sig1.IsVariable && !sig2.Name.getArgumentNames().empty()) ||
             (sig2.IsVariable && !sig1.Name.getArgumentNames().empty()));
  }

  // Note that we intentionally ignore the HasOpaqueReturnType bit here.
  // For declarations that can't be overloaded by type, we want them to be
  // considered conflicting independent of their type.
  
  return sig1.Name == sig2.Name;
}

bool swift::conflicting(ASTContext &ctx,
                        const OverloadSignature& sig1, CanType sig1Type,
                        const OverloadSignature& sig2, CanType sig2Type,
                        bool *wouldConflictInSwift5,
                        bool skipProtocolExtensionCheck) {
  // If the signatures don't conflict to begin with, we're done.
  if (!conflicting(sig1, sig2, skipProtocolExtensionCheck))
    return false;

  // Functions and enum elements do not conflict with each other if their types
  // are different.
  if (((sig1.IsFunction && sig2.IsEnumElement) ||
       (sig1.IsEnumElement && sig2.IsFunction)) &&
      sig1Type != sig2Type) {
    return false;
  }

  // Nominal types and enum elements always conflict with each other.
  if ((sig1.IsNominal && sig2.IsEnumElement) ||
      (sig1.IsEnumElement && sig2.IsNominal)) {
    return true;
  }

  // Typealiases and enum elements always conflict with each other.
  if ((sig1.IsTypeAlias && sig2.IsEnumElement) ||
      (sig1.IsEnumElement && sig2.IsTypeAlias)) {
    return true;
  }

  // Enum elements always conflict with each other. At this point, they
  // have the same base name but different types.
  if (sig1.IsEnumElement && sig2.IsEnumElement) {
    return true;
  }

  // Functions always conflict with non-functions with the same signature.
  // In practice, this only applies for zero argument functions.
  if (sig1.IsFunction != sig2.IsFunction)
    return true;

  // Variables always conflict with non-variables with the same signature.
  // (e.g variables with zero argument functions, variables with type
  //  declarations)
  if (sig1.IsVariable != sig2.IsVariable) {
    // Prior to Swift 5, we permitted redeclarations of variables as different
    // declarations if the variable was declared in an extension of a generic
    // type. Make sure we maintain this behaviour in versions < 5.
    if (!ctx.isSwiftVersionAtLeast(5)) {
      if ((sig1.IsVariable && sig1.InExtensionOfGenericType) ||
          (sig2.IsVariable && sig2.InExtensionOfGenericType)) {
        if (wouldConflictInSwift5)
          *wouldConflictInSwift5 = true;

        return false;
      }
    }

    return true;
  }

  // Otherwise, the declarations conflict if the overload types are the same.
  if (sig1.HasOpaqueReturnType != sig2.HasOpaqueReturnType)
    return false;
  
  if (sig1Type != sig2Type)
    return false;

  // The Swift 5 overload types are the same, but similar to the above, prior to
  // Swift 5, a variable not in an extension of a generic type got a null
  // overload type instead of a function type as it does now, so we really
  // follow that behaviour and warn if there's going to be a conflict in future.
  if (!ctx.isSwiftVersionAtLeast(5)) {
    auto swift4Sig1Type = sig1.IsVariable && !sig1.InExtensionOfGenericType
                              ? CanType()
                              : sig1Type;
    auto swift4Sig2Type = sig1.IsVariable && !sig2.InExtensionOfGenericType
                              ? CanType()
                              : sig1Type;
    if (swift4Sig1Type != swift4Sig2Type) {
      // Old was different to the new behaviour!
      if (wouldConflictInSwift5)
        *wouldConflictInSwift5 = true;

      return false;
    }
  }

  return true;
}

static Type mapSignatureFunctionType(ASTContext &ctx, Type type,
                                     bool topLevelFunction,
                                     bool isMethod,
                                     bool isInitializer,
                                     unsigned curryLevels);

/// Map a type within the signature of a declaration.
static Type mapSignatureType(ASTContext &ctx, Type type) {
  return type.transform([&](Type type) -> Type {
      if (type->is<FunctionType>()) {
        return mapSignatureFunctionType(ctx, type, false, false, false, 1);
      }
      
      return type;
    });
}

/// Map a signature type for a parameter.
static Type mapSignatureParamType(ASTContext &ctx, Type type) {
  return mapSignatureType(ctx, type);
}

/// Map an ExtInfo for a function type.
///
/// When checking if two signatures should be equivalent for overloading,
/// we may need to compare the extended information.
///
/// In the type of the function declaration, none of the extended information
/// is relevant. We cannot overload purely on 'throws' or the calling
/// convention of the declaration itself.
///
/// For function parameter types, we do want to be able to overload on
/// 'throws', since that is part of the mangled symbol name, but not
/// @noescape.
static AnyFunctionType::ExtInfo
mapSignatureExtInfo(AnyFunctionType::ExtInfo info,
                    bool topLevelFunction) {
  if (topLevelFunction)
    return AnyFunctionType::ExtInfo();
  return AnyFunctionType::ExtInfoBuilder()
      .withRepresentation(info.getRepresentation())
      .withConcurrent(info.isSendable())
      .withAsync(info.isAsync())
      .withThrows(info.isThrowing())
      .withClangFunctionType(info.getClangTypeInfo().getType())
      .build();
}

/// Map a function's type to the type used for computing signatures,
/// which involves stripping some attributes, stripping default arguments,
/// transforming implicitly unwrapped optionals into strict optionals,
/// stripping 'inout' on the 'self' parameter etc.
static Type mapSignatureFunctionType(ASTContext &ctx, Type type,
                                     bool topLevelFunction,
                                     bool isMethod,
                                     bool isInitializer,
                                     unsigned curryLevels) {
  if (type->hasError()) {
    return type;
  }

  if (curryLevels == 0) {
    // In an initializer, ignore optionality.
    if (isInitializer) {
      if (auto objectType = type->getOptionalObjectType()) {
        type = objectType;
      }
    }
    
    // Functions and subscripts cannot overload differing only in opaque return
    // types. Replace the opaque type with `Any`.
    if (type->is<OpaqueTypeArchetypeType>()) {
      type = ProtocolCompositionType::get(ctx, {}, /*hasAnyObject*/ false);
    }

    return mapSignatureParamType(ctx, type);
  }

  auto funcTy = type->castTo<AnyFunctionType>();
  SmallVector<AnyFunctionType::Param, 4> newParams;
  for (const auto &param : funcTy->getParams()) {
    auto newParamType = mapSignatureParamType(ctx, param.getPlainType());

    // Don't allow overloading by @_nonEphemeral or isolated.
    auto newFlags = param.getParameterFlags()
        .withNonEphemeral(false)
        .withIsolated(false);

    // For the 'self' of a method, strip off 'inout'.
    if (isMethod) {
      newFlags = newFlags.withInOut(false);
    }

    AnyFunctionType::Param newParam(newParamType, param.getLabel(), newFlags,
                                    param.getInternalLabel());
    newParams.push_back(newParam);
  }

  // Map the result type.
  auto resultTy = mapSignatureFunctionType(
    ctx, funcTy->getResult(), topLevelFunction, false, isInitializer,
    curryLevels - 1);

  // Map various attributes differently depending on if we're looking at
  // the declaration, or a function parameter type.
  AnyFunctionType::ExtInfo info = mapSignatureExtInfo(
      funcTy->getExtInfo(), topLevelFunction);

  // Rebuild the resulting function type.
  if (auto genericFuncTy = dyn_cast<GenericFunctionType>(funcTy))
    return GenericFunctionType::get(genericFuncTy->getGenericSignature(),
                                    newParams, resultTy, info);

  return FunctionType::get(newParams, resultTy, info);
}

OverloadSignature ValueDecl::getOverloadSignature() const {
  OverloadSignature signature;

  signature.Name = getName();
  signature.InProtocolExtension
    = static_cast<bool>(getDeclContext()->getExtendedProtocolDecl());
  signature.IsInstanceMember = isInstanceMember();
  signature.IsVariable = isa<VarDecl>(this);
  signature.IsEnumElement = isa<EnumElementDecl>(this);
  signature.IsNominal = isa<NominalTypeDecl>(this);
  signature.IsTypeAlias = isa<TypeAliasDecl>(this);
  signature.HasOpaqueReturnType =
                       !signature.IsVariable && (bool)getOpaqueResultTypeDecl();

  // Unary operators also include prefix/postfix.
  if (auto func = dyn_cast<FuncDecl>(this)) {
    if (func->isUnaryOperator()) {
      signature.UnaryOperator = func->getAttrs().getUnaryOperatorKind();
    }
  }

  // Functions include async/not-async.
  if (auto func = dyn_cast<AbstractFunctionDecl>(this)) {
    signature.IsFunction = true;
    if (func->hasAsync())
      signature.IsAsyncFunction = true;
  }

  if (auto *extension = dyn_cast<ExtensionDecl>(getDeclContext()))
    if (extension->isGeneric())
      signature.InExtensionOfGenericType = true;

  return signature;
}

CanType ValueDecl::getOverloadSignatureType() const {
  if (auto *afd = dyn_cast<AbstractFunctionDecl>(this)) {
    bool isMethod = afd->hasImplicitSelfDecl();
    return mapSignatureFunctionType(
                           getASTContext(), getInterfaceType(),
                           /*topLevelFunction=*/true,
                           isMethod,
                           /*isInitializer=*/isa<ConstructorDecl>(afd),
                           getNumCurryLevels())->getCanonicalType();
  }

  if (isa<AbstractStorageDecl>(this)) {
    // First, get the default overload signature type for the decl. For vars,
    // this is the empty tuple type, as variables cannot be overloaded directly
    // by type. For subscripts, it's their interface type.
    CanType defaultSignatureType;
    if (isa<VarDecl>(this)) {
      defaultSignatureType = TupleType::getEmpty(getASTContext());
    } else {
      defaultSignatureType = mapSignatureFunctionType(
          getASTContext(), getInterfaceType(),
          /*topLevelFunction=*/true,
          /*isMethod=*/false,
          /*isInitializer=*/false,
          getNumCurryLevels())->getCanonicalType();
    }

    // We want to curry the default signature type with the 'self' type of the
    // given context (if any) in order to ensure the overload signature type
    // is unique across different contexts, such as between a protocol extension
    // and struct decl.
    return defaultSignatureType->addCurriedSelfType(getDeclContext())
                               ->getCanonicalType();
  }

  if (isa<EnumElementDecl>(this)) {
    auto mappedType = mapSignatureFunctionType(
        getASTContext(), getInterfaceType(), /*topLevelFunction=*/false,
        /*isMethod=*/false, /*isInitializer=*/false, getNumCurryLevels());
    return mappedType->getCanonicalType();
  }

  // Note: If you add more cases to this function, you should update the
  // implementation of the swift::conflicting overload that deals with
  // overload types, in order to account for cases where the overload types
  // don't match, but the decls differ and therefore always conflict.
  assert(isa<TypeDecl>(this));
  return CanType();
}

llvm::TinyPtrVector<ValueDecl *> ValueDecl::getOverriddenDecls() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    OverriddenDeclsRequest{const_cast<ValueDecl *>(this)}, {});
}

void ValueDecl::setOverriddenDecls(ArrayRef<ValueDecl *> overridden) {
  llvm::TinyPtrVector<ValueDecl *> overriddenVec(overridden);
  OverriddenDeclsRequest request{const_cast<ValueDecl *>(this)};
  request.cacheResult(overriddenVec);
}

TypeRepr *ValueDecl::getOpaqueResultTypeRepr() const {
  TypeRepr *returnRepr = nullptr;
  if (auto *VD = dyn_cast<VarDecl>(this)) {
    if (auto *P = VD->getParentPattern()) {
      while (auto *PP = dyn_cast<ParenPattern>(P))
        P = PP->getSubPattern();

      if (auto *TP = dyn_cast<TypedPattern>(P)) {
        P = P->getSemanticsProvidingPattern();
        if (auto *NP = dyn_cast<NamedPattern>(P)) {
          assert(NP->getDecl() == VD);
          (void) NP;

          returnRepr = TP->getTypeRepr();
        }
      }
    } else {
      returnRepr = VD->getTypeReprOrParentPatternTypeRepr();
    }
  } else if (auto *FD = dyn_cast<FuncDecl>(this)) {
    returnRepr = FD->getResultTypeRepr();
  } else if (auto *SD = dyn_cast<SubscriptDecl>(this)) {
    returnRepr = SD->getElementTypeRepr();
  }

  if (returnRepr && returnRepr->hasOpaque()) {
    return returnRepr;
  } else {
    return nullptr;
  }
}

OpaqueTypeDecl *ValueDecl::getOpaqueResultTypeDecl() const {
  if (getOpaqueResultTypeRepr() == nullptr) {
    if (!isa<VarDecl>(this) &&
        !isa<FuncDecl>(this) &&
        !isa<SubscriptDecl>(this))
      return nullptr;
    auto file = cast<FileUnit>(getDeclContext()->getModuleScopeContext());
    // Don't look up when the decl is from source, otherwise a cycle will happen.
    if (file->getKind() == FileUnitKind::SerializedAST) {
      Mangle::ASTMangler mangler;
      auto name = mangler.mangleOpaqueTypeDecl(this);
      return file->lookupOpaqueResultType(name);
    }
    return nullptr;
  }

  return evaluateOrDefault(getASTContext().evaluator,
    OpaqueResultTypeRequest{const_cast<ValueDecl *>(this)},
    nullptr);
}

bool ValueDecl::isObjC() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    IsObjCRequest{const_cast<ValueDecl *>(this)},
    getAttrs().hasAttribute<ObjCAttr>());
}

void ValueDecl::setIsObjC(bool value) {
  assert(!LazySemanticInfo.isObjCComputed || LazySemanticInfo.isObjC == value);

  if (LazySemanticInfo.isObjCComputed) {
    assert(LazySemanticInfo.isObjC == value);
    return;
  }

  LazySemanticInfo.isObjCComputed = true;
  LazySemanticInfo.isObjC = value;
}

bool ValueDecl::isSemanticallyFinal() const {
  // Actor types are semantically final.
  if (auto classDecl = dyn_cast<ClassDecl>(this)) {
    if (classDecl->isActor())
      return true;
  }

  // As are members of actor types.
  if (auto classDecl = getDeclContext()->getSelfClassDecl()) {
    if (classDecl->isActor())
      return true;
  }

  // For everything else, the same as 'final'.
  return isFinal();
}

bool ValueDecl::isFinal() const {
  return evaluateOrDefault(getASTContext().evaluator,
                           IsFinalRequest { const_cast<ValueDecl *>(this) },
                           getAttrs().hasAttribute<FinalAttr>());
}

bool ValueDecl::isDynamic() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    IsDynamicRequest{const_cast<ValueDecl *>(this)},
    getAttrs().hasAttribute<DynamicAttr>());
}

bool ValueDecl::isObjCDynamicInGenericClass() const {
  if (!isObjCDynamic())
    return false;

  auto *DC = this->getDeclContext();
  auto *classDecl = DC->getSelfClassDecl();
  if (!classDecl)
    return false;

  return classDecl->isGenericContext()
             && !classDecl->isTypeErasedGenericClass();
}

bool ValueDecl::shouldUseObjCMethodReplacement() const {
  if (isNativeDynamic())
    return false;

  if (getModuleContext()->isImplicitDynamicEnabled() &&
      isObjCDynamicInGenericClass())
    return false;

  return isObjCDynamic();
}

bool ValueDecl::shouldUseNativeMethodReplacement() const {
  if (isNativeDynamic())
    return true;

  if (!isObjCDynamicInGenericClass())
    return false;

  auto *replacedDecl = getDynamicallyReplacedDecl();
  if (replacedDecl)
    return false;

  return getModuleContext()->isImplicitDynamicEnabled();
}

bool ValueDecl::isNativeMethodReplacement() const {
  // Is this a @_dynamicReplacement(for:) that use the native dynamic function
  // replacement mechanism.
  auto *replacedDecl = getDynamicallyReplacedDecl();
  if (!replacedDecl)
    return false;

  if (isNativeDynamic())
    return true;

  if (isObjCDynamicInGenericClass())
    return replacedDecl->getModuleContext()->isImplicitDynamicEnabled();

  return false;
}

void ValueDecl::setIsDynamic(bool value) {
  assert(!LazySemanticInfo.isDynamicComputed ||
         LazySemanticInfo.isDynamic == value);

  if (LazySemanticInfo.isDynamicComputed) {
    assert(LazySemanticInfo.isDynamic == value);
    return;
  }

  LazySemanticInfo.isDynamicComputed = true;
  LazySemanticInfo.isDynamic = value;
}

ValueDecl *ValueDecl::getDynamicallyReplacedDecl() const {
  return evaluateOrDefault(getASTContext().evaluator,
                           DynamicallyReplacedDeclRequest{
                               const_cast<ValueDecl *>(this)},
                           nullptr);
}

bool ValueDecl::canBeAccessedByDynamicLookup() const {
  if (!hasName())
    return false;

  auto *dc = getDeclContext();
  if (!dc->mayContainMembersAccessedByDynamicLookup())
    return false;

  // Dynamic lookup can find functions, variables, and subscripts.
  if (!isa<FuncDecl>(this) && !isa<VarDecl>(this) && !isa<SubscriptDecl>(this))
    return false;

  return true;
}

bool ValueDecl::isImplicitlyUnwrappedOptional() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    IsImplicitlyUnwrappedOptionalRequest{const_cast<ValueDecl *>(this)},
    false);
}

bool ValueDecl::isLocalCapture() const {
  auto *dc = getDeclContext();

  if (auto *fd = dyn_cast<FuncDecl>(this))
    if (isa<SourceFile>(dc))
      return fd->hasTopLevelLocalContextCaptures();

  return dc->isLocalContext();
}

ArrayRef<ValueDecl *>
ValueDecl::getSatisfiedProtocolRequirements(bool Sorted) const {
  // Dig out the nominal type.
  NominalTypeDecl *NTD = getDeclContext()->getSelfNominalTypeDecl();
  if (!NTD || isa<ProtocolDecl>(NTD))
    return {};

  return NTD->getSatisfiedProtocolRequirementsForMember(this, Sorted);
}

bool ValueDecl::isProtocolRequirement() const {
  assert(isa<ProtocolDecl>(getDeclContext()));

  if (isa<AccessorDecl>(this) ||
      isa<TypeAliasDecl>(this) ||
      isa<NominalTypeDecl>(this))
    return false;
  return true;
}

bool ValueDecl::hasInterfaceType() const {
  return !TypeAndAccess.getPointer().isNull();
}

static bool isComputingInterfaceType(const ValueDecl *VD) {
  return VD->getASTContext().evaluator.hasActiveRequest(
            InterfaceTypeRequest{const_cast<ValueDecl *>(VD)});
}

bool ValueDecl::isRecursiveValidation() const {
  if (isComputingInterfaceType(this) && !hasInterfaceType())
    return true;

  if (auto *vd = dyn_cast<VarDecl>(this))
    if (auto *pbd = vd->getParentPatternBinding())
      if (pbd->isComputingPatternBindingEntry(vd))
        return true;

  auto *dc = getDeclContext();
  while (isa<NominalTypeDecl>(dc))
    dc = dc->getParent();

  if (auto *ext = dyn_cast<ExtensionDecl>(dc)) {
    if (ext->isComputingGenericSignature())
      return true;
  }

  return false;
}

Type ValueDecl::getInterfaceType() const {
  auto &ctx = getASTContext();
  if (auto type =
          evaluateOrDefault(ctx.evaluator,
                            InterfaceTypeRequest{const_cast<ValueDecl *>(this)},
                            Type()))
    return type;
  return ErrorType::get(ctx);
}

void ValueDecl::setInterfaceType(Type type) {
  assert(!type.isNull() && "Resetting the interface type to null is forbidden");
  getASTContext().evaluator.cacheOutput(InterfaceTypeRequest{this},
                                        std::move(type));
}

Optional<ObjCSelector> ValueDecl::getObjCRuntimeName(
                                              bool skipIsObjCResolution) const {
  if (auto func = dyn_cast<AbstractFunctionDecl>(this))
    return func->getObjCSelector(DeclName(), skipIsObjCResolution);

  ASTContext &ctx = getASTContext();
  auto makeSelector = [&](Identifier name) -> ObjCSelector {
    return ObjCSelector(ctx, 0, { name });
  };

  if (auto classDecl = dyn_cast<ClassDecl>(this)) {
    SmallString<32> scratch;
    return makeSelector(
             ctx.getIdentifier(classDecl->getObjCRuntimeName(scratch)));
  }

  if (auto protocol = dyn_cast<ProtocolDecl>(this)) {
    SmallString<32> scratch;
    return makeSelector(
             ctx.getIdentifier(protocol->getObjCRuntimeName(scratch)));
  }

  if (auto var = dyn_cast<VarDecl>(this))
    return makeSelector(var->getObjCPropertyName());

  return None;
}

bool ValueDecl::canInferObjCFromRequirement(ValueDecl *requirement) {
  // Only makes sense for a requirement of an @objc protocol.
  auto proto = cast<ProtocolDecl>(requirement->getDeclContext());
  if (!proto->isObjC()) return false;

  // Only makes sense when this declaration is within a nominal type
  // or extension thereof.
  auto nominal = getDeclContext()->getSelfNominalTypeDecl();
  if (!nominal) return false;

  // If there is already an @objc attribute with an explicit name, we
  // can't infer a name (it's already there).
  if (auto objcAttr = getAttrs().getAttribute<ObjCAttr>()) {
    if (objcAttr->hasName() && !objcAttr->isNameImplicit())
      return false;
  }

  // If the nominal type doesn't conform to the protocol at all, we
  // cannot infer @objc no matter what we do.
  SmallVector<ProtocolConformance *, 1> conformances;
  if (!nominal->lookupConformance(proto, conformances))
    return false;

  // If any of the conformances is attributed to the context in which
  // this declaration resides, we can infer @objc or the Objective-C
  // name.
  auto dc = getDeclContext();
  for (auto conformance : conformances) {
    if (conformance->getDeclContext() == dc)
      return true;
  }

  // Nothing to infer from.
  return false;
}

SourceLoc ValueDecl::getAttributeInsertionLoc(bool forModifier) const {
  if (isImplicit())
    return SourceLoc();

  if (auto var = dyn_cast<VarDecl>(this)) {
    // [attrs] var ...
    // The attributes are part of the VarDecl, but the 'var' is part of the PBD.
    SourceLoc resultLoc = var->getAttrs().getStartLoc(forModifier);
    if (resultLoc.isValid()) {
      return resultLoc;
    } else if (auto pbd = var->getParentPatternBinding()) {
      return pbd->getStartLoc();
    } else {
      return var->getStartLoc();
    }
  }

  SourceLoc resultLoc = getAttrs().getStartLoc(forModifier);
  return resultLoc.isValid() ? resultLoc : getStartLoc();
}

/// Returns true if \p VD needs to be treated as publicly-accessible
/// at the SIL, LLVM, and machine levels due to being @usableFromInline.
bool ValueDecl::isUsableFromInline() const {
  assert(getFormalAccess() <= AccessLevel::Internal);

  if (getAttrs().hasAttribute<UsableFromInlineAttr>() ||
      getAttrs().hasAttribute<AlwaysEmitIntoClientAttr>() ||
      getAttrs().hasAttribute<InlinableAttr>())
    return true;

  if (auto *accessor = dyn_cast<AccessorDecl>(this)) {
    auto *storage = accessor->getStorage();
    if (storage->getAttrs().hasAttribute<UsableFromInlineAttr>() ||
        storage->getAttrs().hasAttribute<AlwaysEmitIntoClientAttr>() ||
        storage->getAttrs().hasAttribute<InlinableAttr>())
      return true;
  }

  if (auto *EED = dyn_cast<EnumElementDecl>(this))
    if (EED->getParentEnum()->getAttrs().hasAttribute<UsableFromInlineAttr>())
      return true;

  if (auto *containingProto = dyn_cast<ProtocolDecl>(getDeclContext())) {
    if (containingProto->getAttrs().hasAttribute<UsableFromInlineAttr>())
      return true;
  }

  if (auto *DD = dyn_cast<DestructorDecl>(this))
    if (auto *CD = dyn_cast<ClassDecl>(DD->getDeclContext()))
      if (CD->getAttrs().hasAttribute<UsableFromInlineAttr>())
        return true;

  return false;
}

bool ValueDecl::shouldHideFromEditor() const {
  // Hide private stdlib declarations.
  if (isPrivateStdlibDecl(/*treatNonBuiltinProtocolsAsPublic*/ false) ||
      // ShowInInterfaceAttr is for decls to show in interface as exception but
      // they are not intended to be used directly.
      getAttrs().hasAttribute<ShowInInterfaceAttr>())
    return true;

  if (AvailableAttr::isUnavailable(this))
    return true;

  if (auto *ClangD = getClangDecl()) {
    if (ClangD->hasAttr<clang::SwiftPrivateAttr>())
      return true;
  }

  if (!isUserAccessible())
    return true;

  // Hide editor placeholders.
  if (getBaseName().isEditorPlaceholder())
    return true;

  // '$__' names are reserved by compiler internal.
  if (!getBaseName().isSpecial() &&
      getBaseIdentifier().str().startswith("$__"))
    return true;

  return false;
}

/// Return maximally open access level which could be associated with the
/// given declaration accounting for @testable importers.
static AccessLevel getMaximallyOpenAccessFor(const ValueDecl *decl) {
  // Non-final classes are considered open to @testable importers.
  if (auto cls = dyn_cast<ClassDecl>(decl)) {
    if (!cls->isSemanticallyFinal())
      return AccessLevel::Open;

  // Non-final overridable class members are considered open to
  // @testable importers.
  } else if (decl->isPotentiallyOverridable()) {
    if (!cast<ValueDecl>(decl)->isSemanticallyFinal())
      return AccessLevel::Open;
  }

  // Everything else is considered public.
  return AccessLevel::Public;
}

/// Adjust \p access based on whether \p VD is \@usableFromInline, has been
/// testably imported from \p useDC or \p VD is an imported SPI.
///
/// \p access isn't always just `VD->getFormalAccess()` because this adjustment
/// may be for a write, in which case the setter's access might be used instead.
static AccessLevel getAdjustedFormalAccess(const ValueDecl *VD,
                                           AccessLevel access,
                                           const DeclContext *useDC,
                                           bool treatUsableFromInlineAsPublic) {
  // If access control is disabled in the current context, adjust
  // access level of the current declaration to be as open as possible.
  if (useDC && VD->getASTContext().isAccessControlDisabled())
    return getMaximallyOpenAccessFor(VD);

  if (treatUsableFromInlineAsPublic &&
      access <= AccessLevel::Internal &&
      VD->isUsableFromInline()) {
    return AccessLevel::Public;
  }

  if (useDC) {
    // Check whether we need to modify the access level based on
    // @testable/@_private import attributes.
    auto *useSF = dyn_cast<SourceFile>(useDC->getModuleScopeContext());
    if (!useSF) return access;
    if (useSF->hasTestableOrPrivateImport(access, VD))
      return getMaximallyOpenAccessFor(VD);
  }

  return access;
}

/// Convenience overload that uses `VD->getFormalAccess()` as the access to
/// adjust.
static AccessLevel
getAdjustedFormalAccess(const ValueDecl *VD, const DeclContext *useDC,
                        bool treatUsableFromInlineAsPublic) {
  return getAdjustedFormalAccess(VD, VD->getFormalAccess(), useDC,
                                 treatUsableFromInlineAsPublic);
}

AccessLevel ValueDecl::getEffectiveAccess() const {
  auto effectiveAccess =
    getAdjustedFormalAccess(this, /*useDC=*/nullptr,
                            /*treatUsableFromInlineAsPublic=*/true);

  // Handle @testable/@_private(sourceFile:)
  switch (effectiveAccess) {
  case AccessLevel::Open:
    break;
  case AccessLevel::Public:
  case AccessLevel::Internal:
    if (getModuleContext()->isTestingEnabled() ||
        getModuleContext()->arePrivateImportsEnabled())
      effectiveAccess = getMaximallyOpenAccessFor(this);
    break;
  case AccessLevel::FilePrivate:
    if (getModuleContext()->arePrivateImportsEnabled())
      effectiveAccess = getMaximallyOpenAccessFor(this);
    break;
  case AccessLevel::Private:
    effectiveAccess = AccessLevel::FilePrivate;
    if (getModuleContext()->arePrivateImportsEnabled())
      effectiveAccess = getMaximallyOpenAccessFor(this);
    break;
  }

  auto restrictToEnclosing = [this](AccessLevel effectiveAccess,
                                    AccessLevel enclosingAccess) -> AccessLevel{
    if (effectiveAccess == AccessLevel::Open &&
        enclosingAccess == AccessLevel::Public &&
        isa<NominalTypeDecl>(this)) {
      // Special case: an open class may be contained in a public
      // class/struct/enum. Leave effectiveAccess as is.
      return effectiveAccess;
    }
    return std::min(effectiveAccess, enclosingAccess);
  };

  if (auto enclosingNominal = dyn_cast<NominalTypeDecl>(getDeclContext())) {
    effectiveAccess =
        restrictToEnclosing(effectiveAccess,
                            enclosingNominal->getEffectiveAccess());

  } else if (auto enclosingExt = dyn_cast<ExtensionDecl>(getDeclContext())) {
    // Just check the base type. If it's a constrained extension, Sema should
    // have already enforced access more strictly.
    if (auto nominal = enclosingExt->getExtendedNominal()) {
      effectiveAccess =
          restrictToEnclosing(effectiveAccess, nominal->getEffectiveAccess());
    }

  } else if (getDeclContext()->isLocalContext()) {
    effectiveAccess = AccessLevel::FilePrivate;
  }

  return effectiveAccess;
}

AccessLevel ValueDecl::getFormalAccess() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    AccessLevelRequest{const_cast<ValueDecl *>(this)}, AccessLevel::Private);
}

bool ValueDecl::hasOpenAccess(const DeclContext *useDC) const {
  AccessLevel access =
      getAdjustedFormalAccess(this, useDC,
                              /*treatUsableFromInlineAsPublic*/false);
  return access == AccessLevel::Open;
}

/// Given the formal access level for using \p VD, compute the scope where
/// \p VD may be accessed, taking \@usableFromInline, \@testable imports,
/// \@_spi imports, and enclosing access levels into account.
///
/// \p access isn't always just `VD->getFormalAccess()` because this adjustment
/// may be for a write, in which case the setter's access might be used instead.
static AccessScope
getAccessScopeForFormalAccess(const ValueDecl *VD,
                              AccessLevel formalAccess,
                              const DeclContext *useDC,
                              bool treatUsableFromInlineAsPublic) {
  AccessLevel access = getAdjustedFormalAccess(VD, formalAccess, useDC,
                                               treatUsableFromInlineAsPublic);
  const DeclContext *resultDC = VD->getDeclContext();

  while (!resultDC->isModuleScopeContext()) {
    if (isa<TopLevelCodeDecl>(resultDC)) {
      return AccessScope(resultDC->getModuleScopeContext(),
                         access == AccessLevel::Private);
    }

    if (resultDC->isLocalContext() || access == AccessLevel::Private)
      return AccessScope(resultDC, /*private*/true);

    if (auto enclosingNominal = dyn_cast<GenericTypeDecl>(resultDC)) {
      auto enclosingAccess =
          getAdjustedFormalAccess(enclosingNominal, useDC,
                                  treatUsableFromInlineAsPublic);
      access = std::min(access, enclosingAccess);

    } else if (auto enclosingExt = dyn_cast<ExtensionDecl>(resultDC)) {
      // Just check the base type. If it's a constrained extension, Sema should
      // have already enforced access more strictly.
      if (auto nominal = enclosingExt->getExtendedNominal()) {
        if (nominal->getParentModule() == enclosingExt->getParentModule()) {
          auto nominalAccess =
              getAdjustedFormalAccess(nominal, useDC,
                                      treatUsableFromInlineAsPublic);
          access = std::min(access, nominalAccess);
        }
      }

    } else {
      llvm_unreachable("unknown DeclContext kind");
    }

    resultDC = resultDC->getParent();
  }

  switch (access) {
  case AccessLevel::Private:
  case AccessLevel::FilePrivate:
    assert(resultDC->isModuleScopeContext());
    return AccessScope(resultDC, access == AccessLevel::Private);
  case AccessLevel::Internal:
    return AccessScope(resultDC->getParentModule());
  case AccessLevel::Public:
  case AccessLevel::Open:
    return AccessScope::getPublic();
  }

  llvm_unreachable("unknown access level");
}

AccessScope
ValueDecl::getFormalAccessScope(const DeclContext *useDC,
                                bool treatUsableFromInlineAsPublic) const {
  return getAccessScopeForFormalAccess(this, getFormalAccess(), useDC,
                                       treatUsableFromInlineAsPublic);
}

/// Checks if \p VD may be used from \p useDC, taking \@testable imports into
/// account.
///
/// Whenever the enclosing context of \p VD is usable from \p useDC, this
/// should compute the same result as checkAccess, below, but more slowly.
///
/// See ValueDecl::isAccessibleFrom for a description of \p forConformance.
static bool checkAccessUsingAccessScopes(const DeclContext *useDC,
                                         const ValueDecl *VD,
                                         AccessLevel access,
                                         bool includeInlineable) {
  if (VD->getASTContext().isAccessControlDisabled())
    return true;

  AccessScope accessScope = getAccessScopeForFormalAccess(
      VD, access, useDC,
      /*treatUsableFromInlineAsPublic*/ includeInlineable);
  if (accessScope.getDeclContext() == useDC) return true;
  if (!AccessScope(useDC).isChildOf(accessScope)) return false;

  // Check SPI access
  if (!useDC || !VD->isSPI()) return true;
  auto useSF = dyn_cast<SourceFile>(useDC->getModuleScopeContext());
  return !useSF || useSF->isImportedAsSPI(VD) ||
         VD->getDeclContext()->getParentModule() == useDC->getParentModule();
}

/// Checks if \p VD may be used from \p useDC, taking \@testable and \@_spi
/// imports into account.
///
/// When \p access is the same as `VD->getFormalAccess()` and the enclosing
/// context of \p VD is usable from \p useDC, this ought to be the same as
/// getting the AccessScope for `VD` and checking if \p useDC is within it.
/// However, there's a source compatibility hack around protocol extensions
/// that makes it not quite the same.
///
/// See ValueDecl::isAccessibleFrom for a description of \p forConformance.
static bool checkAccess(const DeclContext *useDC, const ValueDecl *VD,
                        bool forConformance,
                        bool includeInlineable,
                        llvm::function_ref<AccessLevel()> getAccessLevel) {
  if (VD->getASTContext().isAccessControlDisabled())
    return true;

  auto access = getAccessLevel();
  auto *sourceDC = VD->getDeclContext();

  // Preserve "fast path" behavior for everything inside
  // protocol extensions and operators, otherwise allow access
  // check declarations inside inaccessible members via slower
  // access scope based check, which is helpful for diagnostics.
  if (!(sourceDC->getSelfProtocolDecl() || VD->isOperator()))
    return checkAccessUsingAccessScopes(useDC, VD, access, includeInlineable);

  if (!forConformance) {
    if (auto *proto = sourceDC->getSelfProtocolDecl()) {
      // FIXME: Swift 4.1 allowed accessing protocol extension methods that were
      // marked 'public' if the protocol was '@_versioned' (now
      // '@usableFromInline'). Which works at the ABI level, so let's keep
      // supporting that here by explicitly checking for it.
      if (access == AccessLevel::Public &&
          proto->getFormalAccess() == AccessLevel::Internal &&
          proto->isUsableFromInline()) {
        return true;
      }

      // Skip the fast path below and just compare access scopes.
      return checkAccessUsingAccessScopes(useDC, VD, access, includeInlineable);
    }
  }

  // Fast path: assume that the client context already has access to our parent
  // DeclContext, and only check what might be different about this declaration.
  if (!useDC)
    return access >= AccessLevel::Public;

  switch (access) {
  case AccessLevel::Private:
    if (useDC != sourceDC) {
      auto *useSF = dyn_cast<SourceFile>(useDC->getModuleScopeContext());
      if (useSF && useSF->hasTestableOrPrivateImport(access, VD))
        return true;
    }
    return (useDC == sourceDC ||
      AccessScope::allowsPrivateAccess(useDC, sourceDC));
  case AccessLevel::FilePrivate:
    if (useDC->getModuleScopeContext() != sourceDC->getModuleScopeContext()) {
      auto *useSF = dyn_cast<SourceFile>(useDC->getModuleScopeContext());
      return useSF && useSF->hasTestableOrPrivateImport(access, VD);
    }
    return true;
  case AccessLevel::Internal: {
    const ModuleDecl *sourceModule = sourceDC->getParentModule();
    const DeclContext *useFile = useDC->getModuleScopeContext();
    if (useFile->getParentModule() == sourceModule)
      return true;
    auto *useSF = dyn_cast<SourceFile>(useFile);
    return useSF && useSF->hasTestableOrPrivateImport(access, sourceModule);
  }
  case AccessLevel::Public:
  case AccessLevel::Open:
    return true;
  }
  llvm_unreachable("bad access level");
}

bool ValueDecl::isMoreVisibleThan(ValueDecl *other) const {
  auto scope = getFormalAccessScope(/*UseDC=*/nullptr,
                                    /*treatUsableFromInlineAsPublic=*/true);

  // 'other' may have come from a @testable import, so we need to upgrade it's
  // visibility to public here. That is not the same as whether 'other' is
  // being built with -enable-testing though -- we don't want to treat it
  // differently in that case.
  auto otherScope =
      other->getFormalAccessScope(getDeclContext(),
                                  /*treatUsableFromInlineAsPublic=*/true);

  if (scope.isPublic())
    return !otherScope.isPublic();
  else if (scope.isInternal())
    return !otherScope.isPublic() && !otherScope.isInternal();
  else
    return false;
}

bool ValueDecl::isAccessibleFrom(const DeclContext *useDC,
                                 bool forConformance,
                                 bool allowUsableFromInline) const {
  return checkAccess(useDC, this, forConformance, allowUsableFromInline,
                     [&]() { return getFormalAccess(); });
}

bool AbstractStorageDecl::isSetterAccessibleFrom(const DeclContext *DC,
                                                 bool forConformance) const {
  assert(isSettable(DC));

  // If a stored property does not have a setter, it is still settable from the
  // designated initializer constructor. In this case, don't check setter
  // access; it is not set.
  if (hasStorage() && !isSettable(nullptr))
    return true;

  if (isa<ParamDecl>(this))
    return true;

  return checkAccess(DC, this, forConformance, /*includeInlineable*/ false,
                     [&]() { return getSetterFormalAccess(); });
}

void ValueDecl::copyFormalAccessFrom(const ValueDecl *source,
                                     bool sourceIsParentContext) {
  assert(!hasAccess());

  AccessLevel access = source->getFormalAccess();

  // To make something have the same access as a 'private' parent, it has to
  // be 'fileprivate' or greater.
  if (sourceIsParentContext && access == AccessLevel::Private)
    access = AccessLevel::FilePrivate;

  // Only certain declarations can be 'open'.
  if (access == AccessLevel::Open && !isPotentiallyOverridable()) {
    assert(!isa<ClassDecl>(this) &&
           "copying 'open' onto a class has complications");
    access = AccessLevel::Public;
  }

  setAccess(access);

  // Inherit the @usableFromInline attribute.
  if (source->getAttrs().hasAttribute<UsableFromInlineAttr>() &&
      !getAttrs().hasAttribute<UsableFromInlineAttr>() &&
      !getAttrs().hasAttribute<InlinableAttr>() &&
      DeclAttribute::canAttributeAppearOnDecl(DAK_UsableFromInline, this)) {
    auto &ctx = getASTContext();
    auto *clonedAttr = new (ctx) UsableFromInlineAttr(/*implicit=*/true);
    getAttrs().add(clonedAttr);
  }
}

Type TypeDecl::getDeclaredInterfaceType() const {
  if (auto *NTD = dyn_cast<NominalTypeDecl>(this))
    return NTD->getDeclaredInterfaceType();

  if (auto *ATD = dyn_cast<AssociatedTypeDecl>(this)) {
    auto &ctx = getASTContext();
    auto selfTy = getDeclContext()->getSelfInterfaceType();
    if (!selfTy)
      return ErrorType::get(ctx);
    return DependentMemberType::get(
        selfTy, const_cast<AssociatedTypeDecl *>(ATD));
  }

  return getInterfaceType()->getMetatypeInstanceType();
}

int TypeDecl::compare(const TypeDecl *type1, const TypeDecl *type2) {
  // Order based on the enclosing declaration.
  auto dc1 = type1->getDeclContext();
  auto dc2 = type2->getDeclContext();

  // Prefer lower depths.
  auto depth1 = dc1->getSemanticDepth();
  auto depth2 = dc2->getSemanticDepth();
  if (depth1 != depth2)
    return depth1 < depth2 ? -1 : +1;

  // Prefer module names earlier in the alphabet.
  if (dc1->isModuleScopeContext() && dc2->isModuleScopeContext()) {
    auto module1 = dc1->getParentModule();
    auto module2 = dc2->getParentModule();
    if (int result = module1->getName().str().compare(module2->getName().str()))
      return result;
  }

  auto nominal1 = dc1->getSelfNominalTypeDecl();
  auto nominal2 = dc2->getSelfNominalTypeDecl();
  if (static_cast<bool>(nominal1) != static_cast<bool>(nominal2)) {
    return static_cast<bool>(nominal1) ? -1 : +1;
  }
  if (nominal1 && nominal2) {
    if (int result = compare(nominal1, nominal2))
      return result;
  }

  if (int result = type1->getBaseIdentifier().str().compare(
                                  type2->getBaseIdentifier().str()))
    return result;

  // Error case: two type declarations that cannot be distinguished.
  if (type1 < type2)
    return -1;
  if (type1 > type2)
    return +1;
  return 0;
}

bool NominalTypeDecl::isFormallyResilient() const {
  // Private and (unversioned) internal types always have a
  // fixed layout.
  if (!getFormalAccessScope(/*useDC=*/nullptr,
                            /*treatUsableFromInlineAsPublic=*/true).isPublic())
    return false;

  // Check for an explicit @_fixed_layout or @frozen attribute.
  if (getAttrs().hasAttribute<FixedLayoutAttr>() ||
      getAttrs().hasAttribute<FrozenAttr>()) {
    return false;
  }

  // Structs and enums imported from C *always* have a fixed layout.
  // We know their size, and pass them as values in SIL and IRGen.
  if (hasClangNode())
    return false;

  // @objc enums and protocols always have a fixed layout.
  if ((isa<EnumDecl>(this) || isa<ProtocolDecl>(this)) && isObjC())
    return false;

  // Otherwise, the declaration behaves as if it was accessed via indirect
  // "resilient" interfaces, even if the module is not built with resilience.
  return true;
}

bool NominalTypeDecl::isResilient() const {
  if (!isFormallyResilient())
    return false;

  return getModuleContext()->isResilient();
}

static bool isOriginallyDefinedIn(const Decl *D, const ModuleDecl* MD) {
  if (!MD)
    return false;
  if (D->getAlternateModuleName().empty())
    return false;
  return D->getAlternateModuleName() == MD->getName().str();
}

bool NominalTypeDecl::isResilient(ModuleDecl *M,
                                  ResilienceExpansion expansion) const {
  switch (expansion) {
  case ResilienceExpansion::Minimal:
    return isResilient();
  case ResilienceExpansion::Maximal:
    // We consider this decl belongs to the module either it's currently
    // defined in this module or it's originally defined in this module, which
    // is specified by @_originallyDefinedIn
    return M != getModuleContext() && !isOriginallyDefinedIn(this, M) &&
      isResilient();
  }
  llvm_unreachable("bad resilience expansion");
}

enum class DeclTypeKind : unsigned {
  DeclaredType,
  DeclaredInterfaceType
};

static Type computeNominalType(NominalTypeDecl *decl, DeclTypeKind kind) {
  ASTContext &ctx = decl->getASTContext();

  // If `decl` is a nested type, find the parent type.
  Type ParentTy;
  DeclContext *dc = decl->getDeclContext();
  if (!isa<ProtocolDecl>(decl) && dc->isTypeContext()) {
    switch (kind) {
    case DeclTypeKind::DeclaredType: {
      if (auto *nominal = dc->getSelfNominalTypeDecl())
        ParentTy = nominal->getDeclaredType();
      break;
    }
    case DeclTypeKind::DeclaredInterfaceType:
      ParentTy = dc->getDeclaredInterfaceType();
      if (ParentTy->is<ErrorType>())
        ParentTy = Type();
      break;
    }
  }

  if (!isa<ProtocolDecl>(decl) && decl->getGenericParams()) {
    switch (kind) {
    case DeclTypeKind::DeclaredType:
      return UnboundGenericType::get(decl, ParentTy, ctx);
    case DeclTypeKind::DeclaredInterfaceType: {
      // Note that here, we need to be able to produce a type
      // before the decl has been validated, so we rely on
      // the generic parameter list directly instead of looking
      // at the signature.
      SmallVector<Type, 4> args;
      for (auto param : decl->getGenericParams()->getParams())
        args.push_back(param->getDeclaredInterfaceType());

      return BoundGenericType::get(decl, ParentTy, args);
    }
    }

    llvm_unreachable("Unhandled DeclTypeKind in switch.");
  } else {
    return NominalType::get(decl, ParentTy, ctx);
  }
}

Type NominalTypeDecl::getDeclaredType() const {
  if (DeclaredTy)
    return DeclaredTy;

  auto *decl = const_cast<NominalTypeDecl *>(this);
  decl->DeclaredTy = computeNominalType(decl, DeclTypeKind::DeclaredType);
  return DeclaredTy;
}

Type NominalTypeDecl::getDeclaredInterfaceType() const {
  if (DeclaredInterfaceTy)
    return DeclaredInterfaceTy;

  auto *decl = const_cast<NominalTypeDecl *>(this);
  decl->DeclaredInterfaceTy = computeNominalType(decl,
                                                 DeclTypeKind::DeclaredInterfaceType);
  return DeclaredInterfaceTy;
}

void NominalTypeDecl::prepareExtensions() {
  // Types in local contexts can't have extensions
  if (getLocalContext() != nullptr) {
    return;
  }
  
  auto &context = Decl::getASTContext();

  // If our list of extensions is out of date, update it now.
  if (context.getCurrentGeneration() > ExtensionGeneration) {
    unsigned previousGeneration = ExtensionGeneration;
    ExtensionGeneration = context.getCurrentGeneration();
    context.loadExtensions(this, previousGeneration);
  }
}

ExtensionRange NominalTypeDecl::getExtensions() {
  prepareExtensions();
  return ExtensionRange(ExtensionIterator(FirstExtension), ExtensionIterator());
}

void NominalTypeDecl::addExtension(ExtensionDecl *extension) {
  assert(!extension->alreadyBoundToNominal() && "Already added extension");
  extension->NextExtension.setInt(true);
  
  // First extension; set both first and last.
  if (!FirstExtension) {
    FirstExtension = extension;
    LastExtension = extension;

    addedExtension(extension);
    return;
  }

  // Add to the end of the list.
  LastExtension->NextExtension.setPointer(extension);
  LastExtension = extension;

  addedExtension(extension);
}

ArrayRef<VarDecl *> NominalTypeDecl::getStoredProperties() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator,
      StoredPropertiesRequest{mutableThis},
      {});
}

ArrayRef<Decl *>
NominalTypeDecl::getStoredPropertiesAndMissingMemberPlaceholders() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator,
      StoredPropertiesAndMissingMembersRequest{mutableThis},
      {});
}

bool NominalTypeDecl::isOptionalDecl() const {
  return this == getASTContext().getOptionalDecl();
}

Optional<KeyPathTypeKind> NominalTypeDecl::getKeyPathTypeKind() const {
  auto &ctx = getASTContext();
#define CASE(NAME) if (this == ctx.get##NAME##Decl()) return KPTK_##NAME;
  CASE(KeyPath)
  CASE(WritableKeyPath)
  CASE(ReferenceWritableKeyPath)
  CASE(AnyKeyPath)
  CASE(PartialKeyPath)
#undef CASE
  return None;
}

PropertyWrapperTypeInfo NominalTypeDecl::getPropertyWrapperTypeInfo() const {
  ASTContext &ctx = getASTContext();
  auto mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           PropertyWrapperTypeInfoRequest{mutableThis},
                           PropertyWrapperTypeInfo());
}

bool NominalTypeDecl::isActor() const {
  auto mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(getASTContext().evaluator,
                           IsActorRequest{mutableThis},
                           false);
}

bool NominalTypeDecl::isDistributedActor() const {
  auto mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(getASTContext().evaluator,
                           IsDistributedActorRequest{mutableThis},
                           false);
}

bool NominalTypeDecl::isAnyActor() const {
  return isActor() || isDistributedActor();
}

GenericTypeDecl::GenericTypeDecl(DeclKind K, DeclContext *DC,
                                 Identifier name, SourceLoc nameLoc,
                                 ArrayRef<InheritedEntry> inherited,
                                 GenericParamList *GenericParams) :
    GenericContext(DeclContextKind::GenericTypeDecl, DC, GenericParams),
    TypeDecl(K, DC, name, nameLoc, inherited) {}

TypeAliasDecl::TypeAliasDecl(SourceLoc TypeAliasLoc, SourceLoc EqualLoc,
                             Identifier Name, SourceLoc NameLoc,
                             GenericParamList *GenericParams, DeclContext *DC)
  : GenericTypeDecl(DeclKind::TypeAlias, DC, Name, NameLoc, {}, GenericParams),
    TypeAliasLoc(TypeAliasLoc), EqualLoc(EqualLoc) {
  Bits.TypeAliasDecl.IsCompatibilityAlias = false;
  Bits.TypeAliasDecl.IsDebuggerAlias = false;
}

SourceRange TypeAliasDecl::getSourceRange() const {
  auto TrailingWhereClauseSourceRange = getGenericTrailingWhereClauseSourceRange();
  if (TrailingWhereClauseSourceRange.isValid())
    return { TypeAliasLoc, TrailingWhereClauseSourceRange.End };
  if (UnderlyingTy.hasLocation())
    return { TypeAliasLoc, UnderlyingTy.getSourceRange().End };
  if (TypeEndLoc.isValid())
    return { TypeAliasLoc, TypeEndLoc };
  return { TypeAliasLoc, getNameLoc() };
}

Type TypeAliasDecl::getUnderlyingType() const {
  auto &ctx = getASTContext();
  if (auto type = evaluateOrDefault(ctx.evaluator,
           UnderlyingTypeRequest{const_cast<TypeAliasDecl *>(this)},
           Type()))
    return type;
  return ErrorType::get(ctx);
}
      
void TypeAliasDecl::setUnderlyingType(Type underlying) {
  // lldb creates global typealiases containing archetypes
  // sometimes...
  if (underlying->hasArchetype() && isGenericContext())
    underlying = underlying->mapTypeOutOfContext();
  getASTContext().evaluator.cacheOutput(
          StructuralTypeRequest{const_cast<TypeAliasDecl *>(this)},
          std::move(underlying));
  getASTContext().evaluator.cacheOutput(
          UnderlyingTypeRequest{const_cast<TypeAliasDecl *>(this)},
          std::move(underlying));
}

UnboundGenericType *TypeAliasDecl::getUnboundGenericType() const {
  assert(getGenericParams());

  Type parentTy;
  auto parentDC = getDeclContext();
  if (auto nominal = parentDC->getSelfNominalTypeDecl())
    parentTy = nominal->getDeclaredType();

  return UnboundGenericType::get(
      const_cast<TypeAliasDecl *>(this),
      parentTy, getASTContext());
}

Type TypeAliasDecl::getStructuralType() const {
  auto &ctx = getASTContext();
  if (auto type = evaluateOrDefault(
      ctx.evaluator,
      StructuralTypeRequest{const_cast<TypeAliasDecl *>(this)},
      Type()))
    return type;
  return ErrorType::get(ctx);
}

Type AbstractTypeParamDecl::getSuperclass() const {
  auto *genericEnv = getDeclContext()->getGenericEnvironmentOfContext();
  assert(genericEnv != nullptr && "Too much circularity");

  auto contextTy = genericEnv->mapTypeIntoContext(getDeclaredInterfaceType());
  if (auto *archetype = contextTy->getAs<ArchetypeType>())
    return archetype->getSuperclass();

  // FIXME: Assert that this is never queried.
  return nullptr;
}

ArrayRef<ProtocolDecl *>
AbstractTypeParamDecl::getConformingProtocols() const {
  auto *genericEnv = getDeclContext()->getGenericEnvironmentOfContext();
  assert(genericEnv != nullptr && "Too much circularity");

  auto contextTy = genericEnv->mapTypeIntoContext(getDeclaredInterfaceType());
  if (auto *archetype = contextTy->getAs<ArchetypeType>())
    return archetype->getConformsTo();

  // FIXME: Assert that this is never queried.
  return { };
}

GenericTypeParamDecl::GenericTypeParamDecl(DeclContext *dc, Identifier name,
                                           SourceLoc nameLoc,
                                           bool isTypeSequence, unsigned depth,
                                           unsigned index)
    : AbstractTypeParamDecl(DeclKind::GenericTypeParam, dc, name, nameLoc) {
  Bits.GenericTypeParamDecl.Depth = depth;
  assert(Bits.GenericTypeParamDecl.Depth == depth && "Truncation");
  Bits.GenericTypeParamDecl.Index = index;
  assert(Bits.GenericTypeParamDecl.Index == index && "Truncation");
  Bits.GenericTypeParamDecl.TypeSequence = isTypeSequence;
  auto &ctx = dc->getASTContext();
  auto type = new (ctx, AllocationArena::Permanent) GenericTypeParamType(this);
  setInterfaceType(MetatypeType::get(type, ctx));
}

SourceRange GenericTypeParamDecl::getSourceRange() const {
  SourceLoc endLoc = getNameLoc();

  if (!getInherited().empty()) {
    endLoc = getInherited().back().getSourceRange().End;
  }
  return SourceRange(getNameLoc(), endLoc);
}

AssociatedTypeDecl::AssociatedTypeDecl(DeclContext *dc, SourceLoc keywordLoc,
                                       Identifier name, SourceLoc nameLoc,
                                       TypeRepr *defaultDefinition,
                                       TrailingWhereClause *trailingWhere)
    : AbstractTypeParamDecl(DeclKind::AssociatedType, dc, name, nameLoc),
      KeywordLoc(keywordLoc), DefaultDefinition(defaultDefinition),
      TrailingWhere(trailingWhere) {
}

AssociatedTypeDecl::AssociatedTypeDecl(DeclContext *dc, SourceLoc keywordLoc,
                                       Identifier name, SourceLoc nameLoc,
                                       TrailingWhereClause *trailingWhere,
                                       LazyMemberLoader *definitionResolver,
                                       uint64_t resolverData)
    : AbstractTypeParamDecl(DeclKind::AssociatedType, dc, name, nameLoc),
      KeywordLoc(keywordLoc), DefaultDefinition(nullptr),
      TrailingWhere(trailingWhere), Resolver(definitionResolver),
      ResolverContextData(resolverData) {
  assert(Resolver && "missing resolver");
}

Type AssociatedTypeDecl::getDefaultDefinitionType() const {
  return evaluateOrDefault(getASTContext().evaluator,
           DefaultDefinitionTypeRequest{const_cast<AssociatedTypeDecl *>(this)},
           Type());
}

SourceRange AssociatedTypeDecl::getSourceRange() const {
  SourceLoc endLoc;
  if (auto TWC = getTrailingWhereClause()) {
    endLoc = TWC->getSourceRange().End;
  } else if (auto defaultDefinition = getDefaultDefinitionTypeRepr()) {
    endLoc = defaultDefinition->getEndLoc();
  } else if (!getInherited().empty()) {
    endLoc = getInherited().back().getSourceRange().End;
  } else {
    endLoc = getNameLoc();
  }
  return SourceRange(KeywordLoc, endLoc);
}

llvm::TinyPtrVector<AssociatedTypeDecl *>
AssociatedTypeDecl::getOverriddenDecls() const {
  // FIXME: Performance hack because we end up looking at the overridden
  // declarations of an associated type a *lot*.
  OverriddenDeclsRequest request{const_cast<AssociatedTypeDecl *>(this)};
  llvm::TinyPtrVector<ValueDecl *> overridden;
  if (auto cached = request.getCachedResult())
    overridden = std::move(*cached);
  else
    overridden = AbstractTypeParamDecl::getOverriddenDecls();

  llvm::TinyPtrVector<AssociatedTypeDecl *> assocTypes;
  for (auto decl : overridden) {
    assocTypes.push_back(cast<AssociatedTypeDecl>(decl));
  }
  return assocTypes;
}

namespace {
static AssociatedTypeDecl *getAssociatedTypeAnchor(
                      const AssociatedTypeDecl *ATD,
                      llvm::SmallSet<const AssociatedTypeDecl *, 8> &searched) {
  auto overridden = ATD->getOverriddenDecls();

  // If this declaration does not override any other declarations, it's
  // the anchor.
  if (overridden.empty()) return const_cast<AssociatedTypeDecl *>(ATD);

  // Find the best anchor among the anchors of the overridden decls and avoid
  // reentrancy when erroneous cyclic protocols exist.
  AssociatedTypeDecl *bestAnchor = nullptr;
  for (auto assocType : overridden) {
    if (!searched.insert(assocType).second)
      continue;
    auto anchor = getAssociatedTypeAnchor(assocType, searched);
    if (!anchor)
      continue;
    if (!bestAnchor || AbstractTypeParamDecl::compare(anchor, bestAnchor) < 0)
      bestAnchor = anchor;
  }

  return bestAnchor;
}
}

AssociatedTypeDecl *AssociatedTypeDecl::getAssociatedTypeAnchor() const {
  llvm::SmallSet<const AssociatedTypeDecl *, 8> searched;
  return ::getAssociatedTypeAnchor(this, searched);
}

EnumDecl::EnumDecl(SourceLoc EnumLoc,
                     Identifier Name, SourceLoc NameLoc,
                     ArrayRef<InheritedEntry> Inherited,
                     GenericParamList *GenericParams, DeclContext *Parent)
  : NominalTypeDecl(DeclKind::Enum, Parent, Name, NameLoc, Inherited,
                    GenericParams),
    EnumLoc(EnumLoc)
{
  Bits.EnumDecl.HasAssociatedValues
    = static_cast<unsigned>(AssociatedValueCheck::Unchecked);
  Bits.EnumDecl.HasAnyUnavailableValues
    = false;
}

Type EnumDecl::getRawType() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(
      ctx.evaluator, EnumRawTypeRequest{const_cast<EnumDecl *>(this)}, Type());
}

void EnumDecl::setRawType(Type rawType) {
  getASTContext().evaluator.cacheOutput(EnumRawTypeRequest{this},
                                        std::move(rawType));
}

StructDecl::StructDecl(SourceLoc StructLoc, Identifier Name, SourceLoc NameLoc,
                       ArrayRef<InheritedEntry> Inherited,
                       GenericParamList *GenericParams, DeclContext *Parent)
  : NominalTypeDecl(DeclKind::Struct, Parent, Name, NameLoc, Inherited,
                    GenericParams),
    StructLoc(StructLoc)
{
  Bits.StructDecl.HasUnreferenceableStorage = false;
  Bits.StructDecl.IsCxxNonTrivial = false;
}

bool NominalTypeDecl::hasMemberwiseInitializer() const {
  // Currently only structs can have memberwise initializers.
  auto *sd = dyn_cast<StructDecl>(this);
  if (!sd)
    return false;

  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<StructDecl *>(sd);
  return evaluateOrDefault(ctx.evaluator, HasMemberwiseInitRequest{mutableThis},
                           false);
}

ConstructorDecl *NominalTypeDecl::getMemberwiseInitializer() const {
  if (!hasMemberwiseInitializer())
    return nullptr;

  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator, SynthesizeMemberwiseInitRequest{mutableThis}, nullptr);
}

ConstructorDecl *NominalTypeDecl::getEffectiveMemberwiseInitializer() {
  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           ResolveEffectiveMemberwiseInitRequest{mutableThis},
                           nullptr);
}

bool NominalTypeDecl::hasDefaultInitializer() const {
  // Currently only structs and classes can have default initializers.
  if (!isa<StructDecl>(this) && !isa<ClassDecl>(this))
    return false;

  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(ctx.evaluator, HasDefaultInitRequest{mutableThis},
                           false);
}

bool NominalTypeDecl::isTypeErasedGenericClass() const {
  // ObjC classes are type erased.
  // TODO: Unless they have magic methods...
  if (auto clas = dyn_cast<ClassDecl>(this))
    return clas->isTypeErasedGenericClass();
  return false;
}

ConstructorDecl *NominalTypeDecl::getDefaultInitializer() const {
  if (!hasDefaultInitializer())
    return nullptr;

  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           SynthesizeDefaultInitRequest{mutableThis}, nullptr);
}

void NominalTypeDecl::synthesizeSemanticMembersIfNeeded(DeclName member) {
  // Silently break cycles here because we can't be sure when and where a
  // request to synthesize will come from yet.
  // FIXME: rdar://56844567
  if (Bits.NominalTypeDecl.IsComputingSemanticMembers)
    return;
    
  Bits.NominalTypeDecl.IsComputingSemanticMembers = true;
  SWIFT_DEFER { Bits.NominalTypeDecl.IsComputingSemanticMembers = false; };

  auto baseName = member.getBaseName();
  auto &Context = getASTContext();
  Optional<ImplicitMemberAction> action = None;
  if (baseName == DeclBaseName::createConstructor())
    action.emplace(ImplicitMemberAction::ResolveImplicitInit);

  if (member.isSimpleName() && !baseName.isSpecial()) {
    if (baseName.getIdentifier() == getASTContext().Id_CodingKeys) {
      action.emplace(ImplicitMemberAction::ResolveCodingKeys);
    }
  } else {
    auto argumentNames = member.getArgumentNames();
    if (member.isSimpleName() || argumentNames.size() == 1) {
      if (baseName == DeclBaseName::createConstructor()) {
        if ((member.isSimpleName() || argumentNames.front() == Context.Id_from)) {
          action.emplace(ImplicitMemberAction::ResolveDecodable);
        } else if (argumentNames.front() == Context.Id_transport) {
          action.emplace(ImplicitMemberAction::ResolveDistributedActorTransport);
        }
      } else if (!baseName.isSpecial() &&
           baseName.getIdentifier() == Context.Id_encode &&
           (member.isSimpleName() || argumentNames.front() == Context.Id_to)) {
        action.emplace(ImplicitMemberAction::ResolveEncodable);
      }
    } else if (member.isSimpleName() || argumentNames.size() == 2) {
      if (baseName == DeclBaseName::createConstructor()) {
        if (argumentNames[0] == Context.Id_resolve &&
            argumentNames[1] == Context.Id_using) {
          action.emplace(ImplicitMemberAction::ResolveDistributedActor);
        }
      }
    }
  }

  if (auto actionToTake = action) {
    (void)evaluateOrDefault(Context.evaluator,
        ResolveImplicitMemberRequest{this, actionToTake.getValue()}, {});
  }
}

VarDecl *NominalTypeDecl::getGlobalActorInstance() const {
  auto mutableThis = const_cast<NominalTypeDecl *>(this);
  return evaluateOrDefault(getASTContext().evaluator,
                           GlobalActorInstanceRequest{mutableThis},
                           nullptr);
}

ClassDecl::ClassDecl(SourceLoc ClassLoc, Identifier Name, SourceLoc NameLoc,
                     ArrayRef<InheritedEntry> Inherited,
                     GenericParamList *GenericParams, DeclContext *Parent,
                     bool isActor)
  : NominalTypeDecl(DeclKind::Class, Parent, Name, NameLoc, Inherited,
                    GenericParams),
    ClassLoc(ClassLoc) {
  Bits.ClassDecl.InheritsSuperclassInits = 0;
  Bits.ClassDecl.ComputedInheritsSuperclassInits = 0;
  Bits.ClassDecl.RawForeignKind = 0;
  Bits.ClassDecl.HasMissingDesignatedInitializers = 0;
  Bits.ClassDecl.ComputedHasMissingDesignatedInitializers = 0;
  Bits.ClassDecl.HasMissingVTableEntries = 0;
  Bits.ClassDecl.ComputedHasMissingVTableEntries = 0;
  Bits.ClassDecl.IsIncompatibleWithWeakReferences = 0;
  Bits.ClassDecl.IsActor = isActor;
}

bool ClassDecl::hasResilientMetadata() const {
  // Imported classes don't have a vtable, etc, at all.
  if (hasClangNode())
    return false;

  // If the module is not resilient, neither is the class metadata.
  if (!getModuleContext()->isResilient())
    return false;

  // If the class is not public, we can't use it outside the module at all.
  if (!getFormalAccessScope(/*useDC=*/nullptr,
                            /*treatUsableFromInlineAsPublic=*/true).isPublic())
    return false;

  // Otherwise we access metadata members, such as vtable entries, resiliently.
  return true;
}

bool ClassDecl::hasResilientMetadata(ModuleDecl *M,
                                     ResilienceExpansion expansion) const {
  switch (expansion) {
  case ResilienceExpansion::Minimal:
    return hasResilientMetadata();
  case ResilienceExpansion::Maximal:
    return M != getModuleContext() && hasResilientMetadata();
  }
  llvm_unreachable("bad resilience expansion");
}

DestructorDecl *ClassDecl::getDestructor() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
                           GetDestructorRequest{const_cast<ClassDecl *>(this)},
                           nullptr);
}

/// Synthesizer callback for an empty implicit function body.
static std::pair<BraceStmt *, bool>
synthesizeEmptyFunctionBody(AbstractFunctionDecl *afd, void *context) {
  ASTContext &ctx = afd->getASTContext();
  return { BraceStmt::create(ctx, afd->getLoc(), { }, afd->getLoc(), true),
           /*isTypeChecked=*/true };
}

DestructorDecl *
GetDestructorRequest::evaluate(Evaluator &evaluator, ClassDecl *CD) const {
  auto &ctx = CD->getASTContext();
  auto *DD = new (ctx) DestructorDecl(CD->getLoc(), CD);

  DD->setImplicit();

  // Synthesize an empty body for the destructor as needed.
  DD->setBodySynthesizer(synthesizeEmptyFunctionBody);

  // Propagate access control and versioned-ness.
  DD->copyFormalAccessFrom(CD, /*sourceIsParentContext*/true);

  // Mark DD as ObjC, as all dtors are.
  DD->setIsObjC(ctx.LangOpts.EnableObjCInterop);
  if (ctx.LangOpts.EnableObjCInterop)
    CD->recordObjCMethod(DD, DD->getObjCSelector());

  // Mark it as synthesized to make its location in getEmittedMembers()
  // deterministic.
  DD->setSynthesized(true);

  return DD;
}

bool ClassDecl::isDefaultActor() const {
  return isDefaultActor(getModuleContext(), ResilienceExpansion::Minimal);
}

bool ClassDecl::isDefaultActor(ModuleDecl *M,
                               ResilienceExpansion expansion) const {
  auto mutableThis = const_cast<ClassDecl *>(this);
  return evaluateOrDefault(getASTContext().evaluator,
                           IsDefaultActorRequest{mutableThis, M,
                                                 expansion},
                           false);
}

const ClassDecl *ClassDecl::getRootActorClass() const {
  if (!isActor()) return nullptr;
  auto cur = this;
  while (true) {
    auto super = cur->getSuperclassDecl();
    if (!super || !super->isActor())
      return cur;
    cur = super;
  }
}

bool ClassDecl::hasMissingDesignatedInitializers() const {
  return evaluateOrDefault(
      getASTContext().evaluator,
      HasMissingDesignatedInitializersRequest{const_cast<ClassDecl *>(this)},
      false);
}

bool ClassDecl::hasMissingVTableEntries() const {
  if (!Bits.ClassDecl.ComputedHasMissingVTableEntries) {
    auto *mutableThis = const_cast<ClassDecl *>(this);
    mutableThis->Bits.ClassDecl.ComputedHasMissingVTableEntries = 1;
    mutableThis->loadAllMembers();
  }

  return Bits.ClassDecl.HasMissingVTableEntries;
}

bool ClassDecl::isIncompatibleWithWeakReferences() const {
  if (Bits.ClassDecl.IsIncompatibleWithWeakReferences) {
    return true;
  }
  if (auto superclass = getSuperclassDecl()) {
    return superclass->isIncompatibleWithWeakReferences();
  }
  return false;
}

bool ClassDecl::inheritsSuperclassInitializers() const {
  // If there's no superclass, there's nothing to inherit.
  if (!getSuperclassDecl())
    return false;

  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<ClassDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator, InheritsSuperclassInitializersRequest{mutableThis}, false);
}

AncestryOptions ClassDecl::checkAncestry() const {
  return AncestryOptions(evaluateOrDefault(getASTContext().evaluator,
                           ClassAncestryFlagsRequest{const_cast<ClassDecl *>(this)},
                           AncestryFlags()));
}
        
AncestryFlags
ClassAncestryFlagsRequest::evaluate(Evaluator &evaluator,
                                    ClassDecl *value) const {
  AncestryOptions result;
  const ClassDecl *CD = value;
  const ClassDecl *PreviousCD = nullptr;
  auto *M = value->getParentModule();

  do {
    if (CD->isGenericContext())
      result |= AncestryFlags::Generic;

    // Note: it's OK to check for @objc explicitly instead of calling isObjC()
    // to infer it since we're going to visit every superclass.
    if (CD->getAttrs().hasAttribute<ObjCAttr>())
      result |= AncestryFlags::ObjC;

    if (CD->getAttrs().hasAttribute<ObjCMembersAttr>())
      result |= AncestryFlags::ObjCMembers;

    if (CD->hasClangNode()) {
      result |= AncestryFlags::ClangImported;

      // Inheriting from an ObjC-defined class generally forces the use
      // of the ObjC object model, but certain classes that directly
      // inherit from NSObject can change that.
      if (!PreviousCD ||
          !(CD->isNSObject() && PreviousCD->isNativeNSObjectSubclass()))
        result |= AncestryFlags::ObjCObjectModel;
    }

    if (CD->hasResilientMetadata())
      result |= AncestryFlags::Resilient;

    if (CD->hasResilientMetadata(M, ResilienceExpansion::Maximal))
      result |= AncestryFlags::ResilientOther;

    if (CD->getAttrs().hasAttribute<RequiresStoredPropertyInitsAttr>())
      result |= AncestryFlags::RequiresStoredPropertyInits;

    PreviousCD = CD;
    CD = CD->getSuperclassDecl();
  } while (CD != nullptr);

  return AncestryFlags(result.toRaw());
}
      
void swift::simple_display(llvm::raw_ostream &out, AncestryFlags value) {
  AncestryOptions opts(value);
  out << "{ ";
  // If we have more than one bit set, we need to print the separator.
  bool wantsSeparator = false;
  auto printBit = [&wantsSeparator, &out](bool val, StringRef name) {
    if (wantsSeparator) {
      out << ", ";
    }
      
    if (!wantsSeparator) {
      wantsSeparator = true;
    }
      
    out << name;
    if (val) {
      out << " = true";
    } else {
      out << " = false";
    }
  };
  printBit(opts.contains(AncestryFlags::ObjC), "ObjC");
  printBit(opts.contains(AncestryFlags::ObjCMembers), "ObjCMembers");
  printBit(opts.contains(AncestryFlags::Generic), "Generic");
  printBit(opts.contains(AncestryFlags::Resilient), "Resilient");
  printBit(opts.contains(AncestryFlags::ResilientOther), "ResilientOther");
  printBit(opts.contains(AncestryFlags::ClangImported), "ClangImported");
  printBit(opts.contains(AncestryFlags::RequiresStoredPropertyInits),
           "RequiresStoredPropertyInits");
  out << " }";
}

bool ClassDecl::isSuperclassOf(ClassDecl *other) const {
  llvm::SmallPtrSet<const ClassDecl *, 8> visited;

  do {
    if (!visited.insert(other).second)
      break;

    if (this == other)
      return true;

    other = other->getSuperclassDecl();
  } while (other != nullptr);

  return false;
}

ClassDecl::MetaclassKind ClassDecl::getMetaclassKind() const {
  assert(getASTContext().LangOpts.EnableObjCInterop &&
         "querying metaclass kind without objc interop");
  auto objc = checkAncestry(AncestryFlags::ObjC);
  return objc ? MetaclassKind::ObjC : MetaclassKind::SwiftStub;
}

/// Mangle the name of a protocol or class for use in the Objective-C
/// runtime.
static StringRef mangleObjCRuntimeName(const NominalTypeDecl *nominal,
                                       llvm::SmallVectorImpl<char> &buffer) {
  {
    Mangle::ASTMangler Mangler;
    std::string MangledName = Mangler.mangleObjCRuntimeName(nominal);

    buffer.clear();
    llvm::raw_svector_ostream os(buffer);
    os << MangledName;
  }

  assert(buffer.size() && "Invalid buffer size");
  return StringRef(buffer.data(), buffer.size());
}

StringRef ClassDecl::getObjCRuntimeName(
                       llvm::SmallVectorImpl<char> &buffer) const {
  // If there is a Clang declaration, use it's runtime name.
  if (auto objcClass
        = dyn_cast_or_null<clang::ObjCInterfaceDecl>(getClangDecl()))
    return objcClass->getObjCRuntimeNameAsString();

  // If there is an 'objc' attribute with a name, use that name.
  if (auto attr = getAttrs().getAttribute<ObjCRuntimeNameAttr>())
    return attr->Name;
  if (auto objc = getAttrs().getAttribute<ObjCAttr>()) {
    if (auto name = objc->getName())
      return name->getString(buffer);
  }

  // Produce the mangled name for this class.
  return mangleObjCRuntimeName(this, buffer);
}

ArtificialMainKind Decl::getArtificialMainKind() const {
  if (getAttrs().hasAttribute<UIApplicationMainAttr>())
    return ArtificialMainKind::UIApplicationMain;
  if (getAttrs().hasAttribute<NSApplicationMainAttr>())
    return ArtificialMainKind::NSApplicationMain;
  if (isa<FuncDecl>(this))
    return ArtificialMainKind::TypeMain;
  llvm_unreachable("type has no @Main attr?!");
}

static bool isOverridingDecl(const ValueDecl *Derived,
                             const ValueDecl *Base) {
  while (Derived) {
    if (Derived == Base)
      return true;
    Derived = Derived->getOverriddenDecl();
  }
  return false;
}

static ValueDecl *findOverridingDecl(const ClassDecl *C,
                                     const ValueDecl *Base) {
  // FIXME: This is extremely inefficient. The SILOptimizer should build a
  // reverse lookup table to answer these types of queries.
  for (auto M : C->getMembers()) {
    if (auto *Derived = dyn_cast<ValueDecl>(M))
      if (::isOverridingDecl(Derived, Base))
        return Derived;
  }

  return nullptr;
}

AbstractFunctionDecl *
ClassDecl::findOverridingDecl(const AbstractFunctionDecl *Method) const {
  if (auto *Accessor = dyn_cast<AccessorDecl>(Method)) {
    auto *Storage = Accessor->getStorage();
    if (auto *Derived = ::findOverridingDecl(this, Storage)) {
      auto *DerivedStorage = cast<AbstractStorageDecl>(Derived);
      return DerivedStorage->getOpaqueAccessor(Accessor->getAccessorKind());
    }

    return nullptr;
  }

  return cast_or_null<AbstractFunctionDecl>(::findOverridingDecl(this, Method));
}

AbstractFunctionDecl *
ClassDecl::findImplementingMethod(const AbstractFunctionDecl *Method) const {
  // FIXME: This is extremely inefficient. The SILOptimizer should build a
  // reverse lookup table to answer these types of queries.
  const ClassDecl *C = this;
  while (C) {
    if (C == Method->getDeclContext())
      return const_cast<AbstractFunctionDecl *>(Method);

    if (auto *Derived = C->findOverridingDecl(Method))
      return Derived;

    // Check the superclass
    C = C->getSuperclassDecl();
  }
  return nullptr;
}

bool ClassDecl::walkSuperclasses(
    llvm::function_ref<TypeWalker::Action(ClassDecl *)> fn) const {

  auto *cls = const_cast<ClassDecl *>(this);

  while (cls) {
    switch (fn(cls)) {
    case TypeWalker::Action::Stop:
      return true;
    case TypeWalker::Action::SkipChildren:
      return false;
    case TypeWalker::Action::Continue:
      cls = cls->getSuperclassDecl();
      continue;
    }
  }

  return false;
}

EnumCaseDecl *EnumCaseDecl::create(SourceLoc CaseLoc,
                                   ArrayRef<EnumElementDecl *> Elements,
                                   DeclContext *DC) {
  size_t bytes = totalSizeToAlloc<EnumElementDecl *>(Elements.size());
  void *buf = DC->getASTContext().Allocate(bytes, alignof(EnumCaseDecl));
  return ::new (buf) EnumCaseDecl(CaseLoc, Elements, DC);
}

bool EnumDecl::hasPotentiallyUnavailableCaseValue() const {
  switch (static_cast<AssociatedValueCheck>(Bits.EnumDecl.HasAssociatedValues)) {
    case AssociatedValueCheck::Unchecked:
      // Compute below
      this->hasOnlyCasesWithoutAssociatedValues();
      LLVM_FALLTHROUGH;
    default:
      return static_cast<bool>(Bits.EnumDecl.HasAnyUnavailableValues);
  }
}

bool EnumDecl::hasOnlyCasesWithoutAssociatedValues() const {
  // Check whether we already have a cached answer.
  switch (static_cast<AssociatedValueCheck>(
            Bits.EnumDecl.HasAssociatedValues)) {
    case AssociatedValueCheck::Unchecked:
      // Compute below.
      break;

    case AssociatedValueCheck::NoAssociatedValues:
      return true;

    case AssociatedValueCheck::HasAssociatedValues:
      return false;
  }
  for (auto elt : getAllElements()) {
    for (auto Attr : elt->getAttrs()) {
      if (auto AvAttr = dyn_cast<AvailableAttr>(Attr)) {
        if (!AvAttr->isInvalid()) {
          const_cast<EnumDecl*>(this)->Bits.EnumDecl.HasAnyUnavailableValues
            = true;
        }
      }
    }

    if (elt->hasAssociatedValues()) {
      const_cast<EnumDecl*>(this)->Bits.EnumDecl.HasAssociatedValues
        = static_cast<unsigned>(AssociatedValueCheck::HasAssociatedValues);
      return false;
    }
  }
  const_cast<EnumDecl*>(this)->Bits.EnumDecl.HasAssociatedValues
    = static_cast<unsigned>(AssociatedValueCheck::NoAssociatedValues);
  return true;
}

bool EnumDecl::isFormallyExhaustive(const DeclContext *useDC) const {
  // Enums explicitly marked frozen are exhaustive.
  if (getAttrs().hasAttribute<FrozenAttr>())
    return true;

  // Objective-C enums /not/ marked frozen are /not/ exhaustive.
  // Note: This implicitly holds @objc enums defined in Swift to a higher
  // standard!
  if (hasClangNode())
    return false;

  // Non-imported enums in non-resilient modules are exhaustive.
  const ModuleDecl *containingModule = getModuleContext();
  if (!containingModule->isResilient())
    return true;

  // Non-public, non-versioned enums are always exhaustive.
  AccessScope accessScope = getFormalAccessScope(/*useDC*/nullptr,
                                                 /*respectVersioned*/true);
  if (!accessScope.isPublic())
    return true;

  // All other checks are use-site specific; with no further information, the
  // enum must be treated non-exhaustively.
  if (!useDC)
    return false;

  // Enums in the same module as the use site are exhaustive /unless/ the use
  // site is inlinable.
  if (useDC->getParentModule() == containingModule)
    if (useDC->getResilienceExpansion() == ResilienceExpansion::Maximal)
      return true;

  // Testably imported enums are exhaustive, on the grounds that only the author
  // of the original library can import it testably.
  if (auto *useSF = dyn_cast<SourceFile>(useDC->getModuleScopeContext()))
    if (useSF->hasTestableOrPrivateImport(AccessLevel::Internal,
                                          containingModule))
      return true;

  // Otherwise, the enum is non-exhaustive.
  return false;
}

bool EnumDecl::isEffectivelyExhaustive(ModuleDecl *M,
                                       ResilienceExpansion expansion) const {
  // Generated Swift code commits to handling garbage values of @objc enums,
  // whether imported or not, to deal with C's loose rules around enums.
  // This covers both frozen and non-frozen @objc enums.
  if (isObjC())
    return false;

  // Otherwise, the only non-exhaustive cases are those that don't have a fixed
  // layout.
  assert(isFormallyExhaustive(M) == !isResilient(M,ResilienceExpansion::Maximal)
         && "ignoring the effects of @inlinable, @testable, and @objc, "
            "these should match up");
  return !isResilient(M, expansion);
}
      
void EnumDecl::setHasFixedRawValues() {
  SemanticFlags |= OptionSet<EnumDecl::SemanticInfoFlags>{EnumDecl::HasFixedRawValues};
}

bool EnumDecl::hasCircularRawValue() const {
  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<EnumDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           HasCircularRawValueRequest{mutableThis}, true);
}

ProtocolDecl::ProtocolDecl(DeclContext *DC, SourceLoc ProtocolLoc,
                           SourceLoc NameLoc, Identifier Name,
                           ArrayRef<InheritedEntry> Inherited,
                           TrailingWhereClause *TrailingWhere)
    : NominalTypeDecl(DeclKind::Protocol, DC, Name, NameLoc, Inherited,
                      nullptr),
      ProtocolLoc(ProtocolLoc) {
  Bits.ProtocolDecl.RequiresClassValid = false;
  Bits.ProtocolDecl.RequiresClass = false;
  Bits.ProtocolDecl.ExistentialConformsToSelfValid = false;
  Bits.ProtocolDecl.ExistentialConformsToSelf = false;
  Bits.ProtocolDecl.InheritedProtocolsValid = 0;
  Bits.ProtocolDecl.NumRequirementsInSignature = 0;
  Bits.ProtocolDecl.HasMissingRequirements = false;
  Bits.ProtocolDecl.KnownProtocol = 0;
  Bits.ProtocolDecl.HasAssociatedTypes = 0;
  Bits.ProtocolDecl.HasLazyAssociatedTypes = 0;
  setTrailingWhereClause(TrailingWhere);
}

bool ProtocolDecl::isMarkerProtocol() const {
  return getAttrs().hasAttribute<MarkerAttr>();
}

bool ProtocolDecl::inheritsFromDistributedActor() const {
  auto &C = getASTContext();
  return inheritsFrom(C.getDistributedActorDecl());
}

ArrayRef<ProtocolDecl *> ProtocolDecl::getInheritedProtocols() const {
  auto *mutThis = const_cast<ProtocolDecl *>(this);
  return evaluateOrDefault(getASTContext().evaluator,
                           InheritedProtocolsRequest{mutThis},
                           {});
}

ArrayRef<AssociatedTypeDecl *>
ProtocolDecl::getAssociatedTypeMembers() const {
  if (Bits.ProtocolDecl.HasAssociatedTypes)
    return AssociatedTypes;

  auto *self = const_cast<ProtocolDecl *>(this);
  self->Bits.ProtocolDecl.HasAssociatedTypes = 1;

  // Clang-imported protocols never have associated types.
  if (hasClangNode())
    return ArrayRef<AssociatedTypeDecl *>();

  // Deserialized @objc protocols never have associated types.
  if (getParentSourceFile() == nullptr && isObjC())
    return ArrayRef<AssociatedTypeDecl *>();

  SmallVector<AssociatedTypeDecl *, 2> result;
  if (Bits.ProtocolDecl.HasLazyAssociatedTypes) {
    auto &ctx = getASTContext();
    auto contextData = static_cast<LazyProtocolData *>(
        ctx.getOrCreateLazyContextData(this, nullptr));

    contextData->loader->loadAssociatedTypes(
        this, contextData->associatedTypesData, result);
  } else {
    for (auto member : getMembers()) {
      if (auto ATD = dyn_cast<AssociatedTypeDecl>(member)) {
        result.push_back(ATD);
      }
    }
  }

  self->AssociatedTypes = getASTContext().AllocateCopy(result);
  return AssociatedTypes;
}

ValueDecl *ProtocolDecl::getSingleRequirement(DeclName name) const {
  auto results = const_cast<ProtocolDecl *>(this)->lookupDirect(name);
  ValueDecl *result = nullptr;
  for (auto candidate : results) {
    if (candidate->getDeclContext() != this ||
        !candidate->isProtocolRequirement())
      continue;
    if (result) {
      // Multiple results.
      return nullptr;
    }
    result = candidate;
  }

  return result;
}

AssociatedTypeDecl *ProtocolDecl::getAssociatedType(Identifier name) const {
  auto results = const_cast<ProtocolDecl *>(this)->lookupDirect(name);
  for (auto candidate : results) {
    if (candidate->getDeclContext() == this &&
        isa<AssociatedTypeDecl>(candidate)) {
      return cast<AssociatedTypeDecl>(candidate);
    }
  }
  return nullptr;
}

Type ProtocolDecl::getSuperclass() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    SuperclassTypeRequest{const_cast<ProtocolDecl *>(this),
                          TypeResolutionStage::Interface},
    Type());
}

ClassDecl *ProtocolDecl::getSuperclassDecl() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    SuperclassDeclRequest{const_cast<ProtocolDecl *>(this)}, nullptr);
}

void ProtocolDecl::setSuperclass(Type superclass) {
  assert((!superclass || !superclass->hasArchetype())
         && "superclass must be interface type");
  LazySemanticInfo.SuperclassType.setPointerAndInt(superclass, true);
  LazySemanticInfo.SuperclassDecl.setPointerAndInt(
    superclass ? superclass->getClassOrBoundGenericClass() : nullptr,
    true);
}

bool ProtocolDecl::walkInheritedProtocols(
              llvm::function_ref<TypeWalker::Action(ProtocolDecl *)> fn) const {
  auto self = const_cast<ProtocolDecl *>(this);

  // Visit all of the inherited protocols.
  SmallPtrSet<ProtocolDecl *, 8> visited;
  SmallVector<ProtocolDecl *, 4> stack;
  stack.push_back(self);
  visited.insert(self);
  while (!stack.empty()) {
    // Pull the next protocol off the stack.
    auto proto = stack.back();
    stack.pop_back();

    switch (fn(proto)) {
    case TypeWalker::Action::Stop:
      return true;

    case TypeWalker::Action::Continue:
      // Add inherited protocols to the stack.
      for (auto inherited : proto->getInheritedProtocols()) {
        if (visited.insert(inherited).second)
          stack.push_back(inherited);
      }
      break;

    case TypeWalker::Action::SkipChildren:
      break;
    }
  }

  return false;

}

bool ProtocolDecl::inheritsFrom(const ProtocolDecl *super) const {
  if (this == super)
    return false;

  return walkInheritedProtocols([super](ProtocolDecl *inherited) {
    if (inherited == super)
      return TypeWalker::Action::Stop;

    return TypeWalker::Action::Continue;
  });
}

bool ProtocolDecl::requiresClass() const {
  return evaluateOrDefault(getASTContext().evaluator,
    ProtocolRequiresClassRequest{const_cast<ProtocolDecl *>(this)}, false);
}

bool ProtocolDecl::requiresSelfConformanceWitnessTable() const {
  return isSpecificProtocol(KnownProtocolKind::Error);
}

bool ProtocolDecl::existentialConformsToSelf() const {
  return evaluateOrDefault(getASTContext().evaluator,
    ExistentialConformsToSelfRequest{const_cast<ProtocolDecl *>(this)}, true);
}

/// Classify usages of Self in the given type.
///
/// \param position The position we are currently at, in terms of variance.
static SelfReferenceInfo
findProtocolSelfReferences(const ProtocolDecl *proto, Type type,
                           SelfReferencePosition position) {
  // If there are no type parameters, we're done.
  if (!type->hasTypeParameter())
    return SelfReferenceInfo();

  // Tuples preserve variance.
  if (auto tuple = type->getAs<TupleType>()) {
    auto info = SelfReferenceInfo();
    for (auto &elt : tuple->getElements()) {
      info |= findProtocolSelfReferences(proto, elt.getType(), position);
    }

    // A covariant Self result inside a tuple will not be bona fide.
    info.hasCovariantSelfResult = false;

    return info;
  } 

  // Function preserve variance in the result type, and flip variance in
  // the parameter type.
  if (auto funcTy = type->getAs<AnyFunctionType>()) {
    auto inputInfo = SelfReferenceInfo();
    for (auto param : funcTy->getParams()) {
      // inout parameters are invariant.
      if (param.isInOut()) {
        inputInfo |= findProtocolSelfReferences(
            proto, param.getPlainType(), SelfReferencePosition::Invariant);
        continue;
      }
      inputInfo |= findProtocolSelfReferences(proto, param.getParameterType(),
                                              position.flipped());
    }

    // A covariant Self result inside a parameter will not be bona fide.
    inputInfo.hasCovariantSelfResult = false;

    auto resultInfo =
        findProtocolSelfReferences(proto, funcTy->getResult(), position);
    if (resultInfo.selfRef == SelfReferencePosition::Covariant) {
      resultInfo.hasCovariantSelfResult = true;
    }
    return inputInfo |= resultInfo;
  }

  // Metatypes preserve variance.
  if (auto metaTy = type->getAs<MetatypeType>()) {
    return findProtocolSelfReferences(proto, metaTy->getInstanceType(),
                                      position);
  }

  // Optionals preserve variance.
  if (auto optType = type->getOptionalObjectType()) {
    return findProtocolSelfReferences(proto, optType, position);
  }

  // DynamicSelfType preserves variance.
  // FIXME: This shouldn't ever appear in protocol requirement
  // signatures.
  if (auto selfType = type->getAs<DynamicSelfType>()) {
    return findProtocolSelfReferences(proto, selfType->getSelfType(), position);
  }

  if (auto *const nominal = type->getAs<NominalOrBoundGenericNominalType>()) {
    auto info = SelfReferenceInfo();

    // Don't forget to look in the parent.
    if (const auto parent = nominal->getParent()) {
      info |= findProtocolSelfReferences(proto, parent, position);
    }

    // Most bound generic types are invariant.
    if (auto *const bgt = type->getAs<BoundGenericType>()) {
      if (bgt->isArray()) {
        // Swift.Array preserves variance in its Value type.
        info |= findProtocolSelfReferences(proto, bgt->getGenericArgs().front(),
                                           position);
      } else if (bgt->isDictionary()) {
        // Swift.Dictionary preserves variance in its Element type.
        info |= findProtocolSelfReferences(proto, bgt->getGenericArgs().front(),
                                           SelfReferencePosition::Invariant);
        info |= findProtocolSelfReferences(proto, bgt->getGenericArgs().back(),
                                           position);
      } else {
        for (auto paramType : bgt->getGenericArgs()) {
          info |= findProtocolSelfReferences(proto, paramType,
                                             SelfReferencePosition::Invariant);
        }
      }
    }

    return info;
  }

  // Opaque result types of protocol extension members contain an invariant
  // reference to 'Self'.
  if (type->is<OpaqueTypeArchetypeType>())
    return SelfReferenceInfo::forSelfRef(SelfReferencePosition::Invariant);

  // Protocol compositions preserve variance.
  auto constraint = type;
  if (auto existential = constraint->getAs<ExistentialType>())
    constraint = existential->getConstraintType();
  if (auto *comp = constraint->getAs<ProtocolCompositionType>()) {
    // 'Self' may be referenced only in a superclass component.
    if (const auto superclass = comp->getSuperclass()) {
      return findProtocolSelfReferences(proto, superclass, position);
    }

    return SelfReferenceInfo();
  }

  // A direct reference to 'Self'.
  if (proto->getSelfInterfaceType()->isEqual(type))
    return SelfReferenceInfo::forSelfRef(position);

  // A reference to an associated type rooted on 'Self'.
  if (type->is<DependentMemberType>()) {
    type = type->getRootGenericParam();
    if (proto->getSelfInterfaceType()->isEqual(type))
      return SelfReferenceInfo::forAssocTypeRef(position);
  }

  return SelfReferenceInfo();
}

/// Find Self references within the given requirement.
SelfReferenceInfo ProtocolDecl::findProtocolSelfReferences(
    const ValueDecl *value, bool treatNonResultCovariantSelfAsInvariant) const {
  // Types never refer to 'Self'.
  if (isa<TypeDecl>(value))
    return SelfReferenceInfo();

  auto type = value->getInterfaceType();

  // Skip invalid declarations.
  if (type->hasError())
    return SelfReferenceInfo();

  if (isa<AbstractFunctionDecl>(value) || isa<SubscriptDecl>(value)) {
    // For a method, skip the 'self' parameter.
    if (isa<AbstractFunctionDecl>(value))
      type = type->castTo<AnyFunctionType>()->getResult();

    auto inputInfo = SelfReferenceInfo();
    for (auto param : type->castTo<AnyFunctionType>()->getParams()) {
      // inout parameters are invariant.
      if (param.isInOut()) {
        inputInfo |= ::findProtocolSelfReferences(
            this, param.getPlainType(), SelfReferencePosition::Invariant);
        continue;
      }
      inputInfo |= ::findProtocolSelfReferences(
          this, param.getParameterType(), SelfReferencePosition::Contravariant);
    }

    // A covariant Self result inside a parameter will not be bona fide.
    inputInfo.hasCovariantSelfResult = false;

    // FIXME: Rather than having a special flag for the is-inheritable check,
    // ensure non-result covariant Self is always diagnosed during type
    // resolution.
    //
    // Methods of non-final classes can only contain a covariant 'Self'
    // as their result type.
    if (treatNonResultCovariantSelfAsInvariant &&
        inputInfo.selfRef == SelfReferencePosition::Covariant) {
      inputInfo.selfRef = SelfReferencePosition::Invariant;
    }

    auto resultInfo = ::findProtocolSelfReferences(
        this, type->castTo<AnyFunctionType>()->getResult(),
        SelfReferencePosition::Covariant);
    if (resultInfo.selfRef == SelfReferencePosition::Covariant) {
      resultInfo.hasCovariantSelfResult = true;
    }

    return inputInfo |= resultInfo;
  } else {
    assert(isa<VarDecl>(value));

    auto info = ::findProtocolSelfReferences(this, type,
                                             SelfReferencePosition::Covariant);
    if (info.selfRef == SelfReferencePosition::Covariant) {
      info.hasCovariantSelfResult = true;
    }

    return info;
  }

  return SelfReferenceInfo();
}

bool ProtocolDecl::isAvailableInExistential(const ValueDecl *decl) const {
  // If the member type references 'Self' in non-covariant position, or an
  // associated type in any position, we cannot use the existential type.
  const auto info = findProtocolSelfReferences(
      decl, /*treatNonResultCovariantSelfAsInvariant=*/false);
  if (info.selfRef > SelfReferencePosition::Covariant || info.assocTypeRef) {
    return false;
  }

  // FIXME: Appropriately diagnose assignments instead.
  if (auto *const storageDecl = dyn_cast<AbstractStorageDecl>(decl)) {
    if (info.hasCovariantSelfResult && storageDecl->supportsMutation())
      return false;
  }

  return true;
}

bool ProtocolDecl::existentialRequiresAny() const {
  return evaluateOrDefault(getASTContext().evaluator,
    ExistentialRequiresAnyRequest{const_cast<ProtocolDecl *>(this)}, true);
}

StringRef ProtocolDecl::getObjCRuntimeName(
                          llvm::SmallVectorImpl<char> &buffer) const {
  // If there is an 'objc' attribute with a name, use that name.
  if (auto objc = getAttrs().getAttribute<ObjCAttr>()) {
    if (auto name = objc->getName())
      return name->getString(buffer);
  }

  // Produce the mangled name for this protocol.
  return mangleObjCRuntimeName(this, buffer);
}

ArrayRef<StructuralRequirement>
ProtocolDecl::getStructuralRequirements() const {
  return evaluateOrDefault(getASTContext().evaluator,
               StructuralRequirementsRequest { const_cast<ProtocolDecl *>(this) },
               None);
}

ArrayRef<Requirement>
ProtocolDecl::getTypeAliasRequirements() const {
  return evaluateOrDefault(getASTContext().evaluator,
               TypeAliasRequirementsRequest { const_cast<ProtocolDecl *>(this) },
               None);
}

ArrayRef<ProtocolDecl *>
ProtocolDecl::getProtocolDependencies() const {
  return evaluateOrDefault(getASTContext().evaluator,
               ProtocolDependenciesRequest { const_cast<ProtocolDecl *>(this) },
               None);
}

ArrayRef<Requirement> ProtocolDecl::getRequirementSignature() const {
  return evaluateOrDefault(getASTContext().evaluator,
               RequirementSignatureRequest { const_cast<ProtocolDecl *>(this) },
               None);
}

bool ProtocolDecl::isComputingRequirementSignature() const {
  return getASTContext().evaluator.hasActiveRequest(
                 RequirementSignatureRequest{const_cast<ProtocolDecl*>(this)});
}

void ProtocolDecl::setRequirementSignature(ArrayRef<Requirement> requirements) {
  assert(!RequirementSignature && "requirement signature already set");
  if (requirements.empty()) {
    RequirementSignature = reinterpret_cast<Requirement *>(this + 1);
    Bits.ProtocolDecl.NumRequirementsInSignature = 0;
  } else {
    RequirementSignature = requirements.data();
    Bits.ProtocolDecl.NumRequirementsInSignature = requirements.size();
  }
}

void
ProtocolDecl::setLazyRequirementSignature(LazyMemberLoader *lazyLoader,
                                          uint64_t requirementSignatureData) {
  assert(!RequirementSignature && "requirement signature already set");

  auto contextData = static_cast<LazyProtocolData *>(
      getASTContext().getOrCreateLazyContextData(this, lazyLoader));
  contextData->requirementSignatureData = requirementSignatureData;
  Bits.ProtocolDecl.HasLazyRequirementSignature = true;

  ++NumLazyRequirementSignatures;
  // FIXME: (transitional) increment the redundant "always-on" counter.
  if (auto *Stats = getASTContext().Stats)
    ++Stats->getFrontendCounters().NumLazyRequirementSignatures;
}

void
ProtocolDecl::setLazyAssociatedTypeMembers(LazyMemberLoader *lazyLoader,
                                           uint64_t associatedTypesData) {
  assert(!Bits.ProtocolDecl.HasAssociatedTypes);
  assert(!Bits.ProtocolDecl.HasLazyAssociatedTypes);

  auto contextData = static_cast<LazyProtocolData *>(
      getASTContext().getOrCreateLazyContextData(this, lazyLoader));
  contextData->associatedTypesData = associatedTypesData;
  Bits.ProtocolDecl.HasLazyAssociatedTypes = true;
}

ArrayRef<Requirement> ProtocolDecl::getCachedRequirementSignature() const {
  assert(RequirementSignature &&
         "getting requirement signature before computing it");
  return llvm::makeArrayRef(RequirementSignature,
                            Bits.ProtocolDecl.NumRequirementsInSignature);
}

void ProtocolDecl::computeKnownProtocolKind() const {
  auto module = getModuleContext();
  if (module != module->getASTContext().getStdlibModule() &&
      !module->getName().is("Foundation") &&
      !module->getName().is("_Differentiation") &&
      !module->getName().is("_Concurrency") &&
      !module->getName().is("_Distributed")) {
    const_cast<ProtocolDecl *>(this)->Bits.ProtocolDecl.KnownProtocol = 1;
    return;
  }

  unsigned value =
    llvm::StringSwitch<unsigned>(getBaseName().userFacingName())
#define PROTOCOL_WITH_NAME(Id, Name) \
      .Case(Name, static_cast<unsigned>(KnownProtocolKind::Id) + 2)
#include "swift/AST/KnownProtocols.def"
      .Default(1);

  const_cast<ProtocolDecl *>(this)->Bits.ProtocolDecl.KnownProtocol = value;
}

Optional<KnownDerivableProtocolKind>
    ProtocolDecl::getKnownDerivableProtocolKind() const {
  const auto knownKind = getKnownProtocolKind();
  if (!knownKind)
      return None;

  switch (*knownKind) {
  case KnownProtocolKind::RawRepresentable:
    return KnownDerivableProtocolKind::RawRepresentable;
  case KnownProtocolKind::OptionSet:
    return KnownDerivableProtocolKind::OptionSet;
  case KnownProtocolKind::CaseIterable:
    return KnownDerivableProtocolKind::CaseIterable;
  case KnownProtocolKind::Comparable:
    return KnownDerivableProtocolKind::Comparable;
  case KnownProtocolKind::Equatable:
    return KnownDerivableProtocolKind::Equatable;
  case KnownProtocolKind::Hashable:
    return KnownDerivableProtocolKind::Hashable;
  case KnownProtocolKind::BridgedNSError:
    return KnownDerivableProtocolKind::BridgedNSError;
  case KnownProtocolKind::CodingKey:
    return KnownDerivableProtocolKind::CodingKey;
  case KnownProtocolKind::Encodable:
    return KnownDerivableProtocolKind::Encodable;
  case KnownProtocolKind::Decodable:
    return KnownDerivableProtocolKind::Decodable;
  case KnownProtocolKind::AdditiveArithmetic:
    return KnownDerivableProtocolKind::AdditiveArithmetic;
  case KnownProtocolKind::Differentiable:
    return KnownDerivableProtocolKind::Differentiable;
  case KnownProtocolKind::Actor:
    return KnownDerivableProtocolKind::Actor;
  case KnownProtocolKind::DistributedActor:
    return KnownDerivableProtocolKind::DistributedActor;
  default: return None;
  }
}

bool ProtocolDecl::hasCircularInheritedProtocols() const {
  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<ProtocolDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator, HasCircularInheritedProtocolsRequest{mutableThis}, true);
}

StorageImplInfo AbstractStorageDecl::getImplInfo() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    StorageImplInfoRequest{const_cast<AbstractStorageDecl *>(this)},
    StorageImplInfo::getSimpleStored(StorageIsMutable));
}

bool AbstractStorageDecl::hasPrivateAccessor() const {
  for (auto accessor : getAllAccessors()) {
    if (hasPrivateOrFilePrivateFormalAccess(accessor))
      return true;
  }
  return false;
}

bool AbstractStorageDecl::hasDidSetOrWillSetDynamicReplacement() const {
  if (auto *func = getParsedAccessor(AccessorKind::DidSet))
    return (bool)func->getDynamicallyReplacedDecl();
  if (auto *func = getParsedAccessor(AccessorKind::WillSet))
    return (bool)func->getDynamicallyReplacedDecl();
  return false;
}

bool AbstractStorageDecl::hasAnyNativeDynamicAccessors() const {
  for (auto accessor : getAllAccessors()) {
    if (accessor->shouldUseNativeDynamicDispatch())
      return true;
  }
  return false;
}

void AbstractStorageDecl::setAccessors(SourceLoc lbraceLoc,
                                       ArrayRef<AccessorDecl *> accessors,
                                       SourceLoc rbraceLoc) {
  // This method is called after we've already recorded an accessors clause
  // only on recovery paths and only when that clause was empty.
  auto record = Accessors.getPointer();
  if (record) {
    assert(record->getAllAccessors().empty());
    for (auto accessor : accessors) {
      (void) record->addOpaqueAccessor(accessor);
    }
  } else {
    record = AccessorRecord::create(getASTContext(),
                                    SourceRange(lbraceLoc, rbraceLoc),
                                    accessors);
    Accessors.setPointer(record);
  }
}

// Compute the number of opaque accessors.
const size_t NumOpaqueAccessors =
  0
#define ACCESSOR(ID)
#define OPAQUE_ACCESSOR(ID, KEYWORD) \
  + 1
#include "swift/AST/AccessorKinds.def"
;

AbstractStorageDecl::AccessorRecord *
AbstractStorageDecl::AccessorRecord::create(ASTContext &ctx,
                                            SourceRange braces,
                                            ArrayRef<AccessorDecl*> accessors) {
  // Silently cap the number of accessors we store at a number that should
  // be easily sufficient for all the valid cases, including space for adding
  // implicit opaque accessors later.
  //
  // We should have already emitted a diagnostic in the parser if we have
  // this many accessors, because most of them will necessarily be redundant.
  if (accessors.size() + NumOpaqueAccessors > MaxNumAccessors) {
    accessors = accessors.slice(0, MaxNumAccessors - NumOpaqueAccessors);
  }

  // Make sure that we have enough space to add implicit opaque accessors later.
  size_t numMissingOpaque = NumOpaqueAccessors;
  {
#define ACCESSOR(ID)
#define OPAQUE_ACCESSOR(ID, KEYWORD)          \
    bool has##ID = false;
#include "swift/AST/AccessorKinds.def"
    for (auto accessor : accessors) {
      switch (accessor->getAccessorKind()) {
#define ACCESSOR(ID)                          \
      case AccessorKind::ID:                  \
        continue;
#define OPAQUE_ACCESSOR(ID, KEYWORD)          \
      case AccessorKind::ID:                  \
        if (!has##ID) {                       \
          has##ID = true;                     \
          --numMissingOpaque;                 \
        }                                     \
        continue;
#include "swift/AST/AccessorKinds.def"
      }
      llvm_unreachable("bad accessor kind");
    }
  }

  auto accessorsCapacity = AccessorIndex(accessors.size() + numMissingOpaque);
  void *mem = ctx.Allocate(totalSizeToAlloc<AccessorDecl*>(accessorsCapacity),
                           alignof(AccessorRecord));
  return new (mem) AccessorRecord(braces, accessors, accessorsCapacity);
}

AbstractStorageDecl::AccessorRecord::AccessorRecord(SourceRange braces,
                                            ArrayRef<AccessorDecl *> accessors,
                                            AccessorIndex accessorsCapacity)
    : Braces(braces), NumAccessors(accessors.size()),
      AccessorsCapacity(accessorsCapacity), AccessorIndices{} {

  // Copy the complete accessors list into place.
  memcpy(getAccessorsBuffer().data(), accessors.data(),
         accessors.size() * sizeof(AccessorDecl*));

  // Register all the accessors.
  for (auto index : indices(accessors)) {
    (void) registerAccessor(accessors[index], index);
  }
}

void AbstractStorageDecl::AccessorRecord::addOpaqueAccessor(AccessorDecl *decl){
  assert(decl);

  // Add the accessor to the array.
  assert(NumAccessors < AccessorsCapacity);
  AccessorIndex index = NumAccessors++;
  getAccessorsBuffer()[index] = decl;

  // Register it.
  bool isUnique = registerAccessor(decl, index);
  assert(isUnique && "adding opaque accessor that's already present");
  (void) isUnique;
}

/// Register that we have an accessor of the given kind.
bool AbstractStorageDecl::AccessorRecord::registerAccessor(AccessorDecl *decl,
                                                           AccessorIndex index){
  // Remember that we have at least one accessor of this kind.
  auto &indexSlot = AccessorIndices[unsigned(decl->getAccessorKind())];
  if (indexSlot) {
    return false;
  } else {
    indexSlot = index + 1;

    assert(getAccessor(decl->getAccessorKind()) == decl);
    return true;
  }
}

AccessLevel
AbstractStorageDecl::getSetterFormalAccess() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
        SetterAccessLevelRequest{const_cast<AbstractStorageDecl *>(this)},
        AccessLevel::Private);
}

AccessScope
AbstractStorageDecl::getSetterFormalAccessScope(const DeclContext *useDC,
                                    bool treatUsableFromInlineAsPublic) const {
  return getAccessScopeForFormalAccess(this, getSetterFormalAccess(), useDC,
                                       treatUsableFromInlineAsPublic);
}

void AbstractStorageDecl::setComputedSetter(AccessorDecl *setter) {
  assert(getImplInfo().getReadImpl() == ReadImplKind::Get);
  assert(!getImplInfo().supportsMutation());
  assert(getAccessor(AccessorKind::Get) && "invariant check: missing getter");
  assert(!getAccessor(AccessorKind::Set) && "already has a setter");
  assert(hasClangNode() && "should only be used for ObjC properties");
  assert(setter && "should not be called for readonly properties");
  assert(setter->getAccessorKind() == AccessorKind::Set);

  setImplInfo(StorageImplInfo::getMutableComputed());
  Accessors.getPointer()->addOpaqueAccessor(setter);
}

void
AbstractStorageDecl::setSynthesizedAccessor(AccessorKind kind,
                                            AccessorDecl *accessor) {
  assert(!getAccessor(kind) && "accessor already exists");
  assert(accessor->getAccessorKind() == kind);

  auto accessors = Accessors.getPointer();
  if (!accessors) {
    accessors = AccessorRecord::create(getASTContext(), SourceRange(), {});
    Accessors.setPointer(accessors);
  }

  accessors->addOpaqueAccessor(accessor);
}

static Optional<ObjCSelector>
getNameFromObjcAttribute(const ObjCAttr *attr, DeclName preferredName) {
  if (!attr)
    return None;
  if (auto name = attr->getName()) {
    if (attr->isNameImplicit()) {
      // preferredName > implicit name, because implicit name is just cached
      // actual name.
      if (!preferredName)
        return *name;
    } else {
      // explicit name > preferred name.
      return *name;
    }
  }
  return None;
}

ObjCSelector
AbstractStorageDecl::getObjCGetterSelector(Identifier preferredName) const {
  // If the getter has an @objc attribute with a name, use that.
  if (auto getter = getOpaqueAccessor(AccessorKind::Get)) {
      if (auto name = getNameFromObjcAttribute(getter->getAttrs().
          getAttribute<ObjCAttr>(), preferredName))
        return *name;
  }

  // Subscripts use a specific selector.
  auto &ctx = getASTContext();
  if (auto *SD = dyn_cast<SubscriptDecl>(this)) {
    switch (SD->getObjCSubscriptKind()) {
    case ObjCSubscriptKind::Indexed:
      return ObjCSelector(ctx, 1, ctx.Id_objectAtIndexedSubscript);
    case ObjCSubscriptKind::Keyed:
      return ObjCSelector(ctx, 1, ctx.Id_objectForKeyedSubscript);
    }
  }

  // The getter selector is the property name itself.
  auto var = cast<VarDecl>(this);
  auto name = var->getObjCPropertyName();

  // Use preferred name is specified.
  if (!preferredName.empty())
    name = preferredName;
  return VarDecl::getDefaultObjCGetterSelector(ctx, name);
}

ObjCSelector
AbstractStorageDecl::getObjCSetterSelector(Identifier preferredName) const {
  // If the setter has an @objc attribute with a name, use that.
  auto setter = getOpaqueAccessor(AccessorKind::Set);
  auto objcAttr = setter ? setter->getAttrs().getAttribute<ObjCAttr>()
                         : nullptr;
  if (auto name = getNameFromObjcAttribute(objcAttr, DeclName(preferredName))) {
    return *name;
  }

  // Subscripts use a specific selector.
  auto &ctx = getASTContext();
  if (auto *SD = dyn_cast<SubscriptDecl>(this)) {
    switch (SD->getObjCSubscriptKind()) {
    case ObjCSubscriptKind::Indexed:
      return ObjCSelector(ctx, 2,
                          { ctx.Id_setObject, ctx.Id_atIndexedSubscript });
    case ObjCSubscriptKind::Keyed:
      return ObjCSelector(ctx, 2,
                          { ctx.Id_setObject, ctx.Id_forKeyedSubscript });
    }
  }
  

  // The setter selector for, e.g., 'fooBar' is 'setFooBar:', with the
  // property name capitalized and preceded by 'set'.
  auto var = cast<VarDecl>(this);
  Identifier Name = var->getObjCPropertyName();
  if (!preferredName.empty())
    Name = preferredName;
  auto result = VarDecl::getDefaultObjCSetterSelector(ctx, Name);

  // Cache the result, so we don't perform string manipulation again.
  if (objcAttr && preferredName.empty())
    const_cast<ObjCAttr *>(objcAttr)->setName(result, /*implicit=*/true);

  return result;
}

SourceLoc AbstractStorageDecl::getOverrideLoc() const {
  if (auto *Override = getAttrs().getAttribute<OverrideAttr>())
    return Override->getLocation();
  return SourceLoc();
}

Type AbstractStorageDecl::getValueInterfaceType() const {
  if (auto var = dyn_cast<VarDecl>(this))
    return var->getInterfaceType()->getReferenceStorageReferent();
  return cast<SubscriptDecl>(this)->getElementInterfaceType();
}

VarDecl::VarDecl(DeclKind kind, bool isStatic, VarDecl::Introducer introducer,
                 SourceLoc nameLoc, Identifier name,
                 DeclContext *dc, StorageIsMutable_t supportsMutation)
  : AbstractStorageDecl(kind, isStatic, dc, name, nameLoc, supportsMutation)
{
  Bits.VarDecl.Introducer = unsigned(introducer);
  Bits.VarDecl.IsSelfParamCapture = false;
  Bits.VarDecl.IsDebuggerVar = false;
  Bits.VarDecl.IsLazyStorageProperty = false;
  Bits.VarDecl.IsPropertyWrapperBackingProperty = false;
  Bits.VarDecl.IsTopLevelGlobal = false;
}

Type VarDecl::getType() const {
  return getDeclContext()->mapTypeIntoContext(getInterfaceType());
}

/// Returns whether the var is settable in the specified context: this
/// is either because it is a stored var, because it has a custom setter, or
/// is a let member in an initializer.
bool VarDecl::isSettable(const DeclContext *UseDC,
                         const DeclRefExpr *base) const {
  // Only inout parameters are settable.
  if (auto *PD = dyn_cast<ParamDecl>(this))
    return PD->isInOut();

  // If this is a 'var' decl, then we're settable if we have storage or a
  // setter.
  if (!isLet())
    return supportsMutation();

  // Static 'let's are always immutable.
  if (isStatic()) {
    return false;
  }

  //
  // All the remaining logic handles the special cases where you can
  // assign a 'let'.
  //

  // Debugger expression 'let's are initialized through a side-channel.
  if (isDebuggerVar())
    return false;

  // 'let's are only ever settable from a specific DeclContext.
  if (UseDC == nullptr)
    return false;
  
  // 'let' properties in structs/classes are only ever settable in their
  // designated initializer(s).
  if (isInstanceMember()) {
    auto *CD = dyn_cast<ConstructorDecl>(UseDC);
    if (!CD) return false;
    
    auto *CDC = CD->getDeclContext();

    // 'let' properties are not valid inside protocols.
    if (CDC->getExtendedProtocolDecl())
      return false;

    // If this init is defined inside of the same type (or in an extension
    // thereof) as the let property, then it is mutable.
    if (CDC->getSelfNominalTypeDecl() !=
        getDeclContext()->getSelfNominalTypeDecl())
      return false;

    if (base && CD->getImplicitSelfDecl() != base->getDecl())
      return false;

    // If this is a convenience initializer (i.e. one that calls
    // self.init), then let properties are never mutable in it.  They are
    // only mutable in designated initializers.
    auto initKindAndExpr = CD->getDelegatingOrChainedInitKind();
    if (initKindAndExpr.initKind == BodyInitKind::Delegating)
      return false;

    return true;
  }

  // If the 'let' has a value bound to it but has no PBD, then it is
  // already initializedand not settable.
  if (getParentPatternBinding() == nullptr)
    return false;

  // If the 'let' has an explicitly written initializer with a pattern binding,
  // then it isn't settable.
  if (isParentInitialized())
    return false;

  // Normal lets (e.g. globals) are only mutable in the context of the
  // declaration.  To handle top-level code properly, we look through
  // the TopLevelCode decl on the use (if present) since the vardecl may be
  // one level up.
  if (getDeclContext() == UseDC)
    return true;

  if (isa<TopLevelCodeDecl>(UseDC) &&
      getDeclContext() == UseDC->getParent())
    return true;

  return false;
}

bool VarDecl::isLazilyInitializedGlobal() const {
  assert(!getDeclContext()->isLocalContext() &&
         "not a global variable!");
  assert(hasStorage() && "not a stored global variable!");

  // Imports from C are never lazily initialized.
  if (hasClangNode())
    return false;

  if (isDebuggerVar())
    return false;

  // Top-level global variables in the main source file and in the REPL are not
  // lazily initialized.
  return !isTopLevelGlobal();
}

SourceRange VarDecl::getSourceRange() const {
  if (auto Param = dyn_cast<ParamDecl>(this))
    return Param->getSourceRange();
  return getNameLoc();
}

SourceRange VarDecl::getTypeSourceRangeForDiagnostics() const {
  // For a parameter, map back to its parameter to get the TypeLoc.
  if (auto *PD = dyn_cast<ParamDecl>(this)) {
    if (auto typeRepr = PD->getTypeRepr())
      return typeRepr->getSourceRange();
  }
  
  Pattern *Pat = getParentPattern();
  if (!Pat || Pat->isImplicit())
    return SourceRange();

  if (auto *VP = dyn_cast<BindingPattern>(Pat))
    Pat = VP->getSubPattern();
  if (auto *TP = dyn_cast<TypedPattern>(Pat))
    if (auto typeRepr = TP->getTypeRepr())
      return typeRepr->getSourceRange();

  return SourceRange();
}

static Optional<std::pair<CaseStmt *, Pattern *>>
findParentPatternCaseStmtAndPattern(const VarDecl *inputVD) {
  auto getMatchingPattern = [&](CaseStmt *cs) -> Pattern * {
    // Check if inputVD is in our case body var decls if we have any. If we do,
    // treat its pattern as our first case label item pattern.
    for (auto *vd : cs->getCaseBodyVariablesOrEmptyArray()) {
      if (vd == inputVD) {
        return cs->getMutableCaseLabelItems().front().getPattern();
      }
    }

    // Then check the rest of our case label items.
    for (auto &item : cs->getMutableCaseLabelItems()) {
      if (item.getPattern()->containsVarDecl(inputVD)) {
        return item.getPattern();
      }
    }

    // Otherwise return false if we do not find anything.
    return nullptr;
  };

  // First find our canonical var decl. This is the VarDecl corresponding to the
  // first case label item of the first case block in the fallthrough chain that
  // our case block is within. Grab the case stmt associated with that var decl
  // and start traveling down the fallthrough chain looking for the case
  // statement that the input VD belongs to by using getMatchingPattern().
  auto *canonicalVD = inputVD->getCanonicalVarDecl();
  auto *caseStmt =
      dyn_cast_or_null<CaseStmt>(canonicalVD->getParentPatternStmt());
  if (!caseStmt)
    return None;

  if (auto *p = getMatchingPattern(caseStmt))
    return std::make_pair(caseStmt, p);

  while ((caseStmt = caseStmt->getFallthroughDest().getPtrOrNull())) {
    if (auto *p = getMatchingPattern(caseStmt))
      return std::make_pair(caseStmt, p);
  }

  return None;
}

VarDecl *VarDecl::getCanonicalVarDecl() const {
  // Any var decl without a parent var decl is canonical. This means that before
  // type checking, all var decls are canonical.
  auto *cur = const_cast<VarDecl *>(this);
  auto *vd = cur->getParentVarDecl();
  if (!vd)
    return cur;

#ifndef NDEBUG
  // Make sure that we don't get into an infinite loop.
  SmallPtrSet<VarDecl *, 8> visitedDecls;
  visitedDecls.insert(vd);
  visitedDecls.insert(cur);
#endif
  while (vd) {
    cur = vd;
    vd = vd->getParentVarDecl();
    assert((!vd || visitedDecls.insert(vd).second) && "Infinite loop ?!");
  }

  return cur;
}

Stmt *VarDecl::getRecursiveParentPatternStmt() const {
  // If our parent is already a pattern stmt, just return that.
  if (auto *stmt = getParentPatternStmt())
    return stmt;

  // Otherwise, see if we have a parent var decl. If we do not, then return
  // nullptr. Otherwise, return the case stmt that we found.
  auto result = findParentPatternCaseStmtAndPattern(this);
  if (!result.hasValue())
    return nullptr;
  return result->first;
}

/// Return the Pattern involved in initializing this VarDecl.  Recall that the
/// Pattern may be involved in initializing more than just this one vardecl
/// though.  For example, if this is a VarDecl for "x", the pattern may be
/// "(x, y)" and the initializer on the PatternBindingDecl may be "(1,2)" or
/// "foo()".
///
/// If this has no parent pattern binding decl or statement associated, it
/// returns null.
///
Pattern *VarDecl::getParentPattern() const {
  // If this has a PatternBindingDecl parent, use its pattern.
  if (auto *PBD = getParentPatternBinding()) {
    const auto i = PBD->getPatternEntryIndexForVarDecl(this);
    return PBD->getPattern(i);
  }

  // If this is a statement parent, dig the pattern out of it.
  if (auto *stmt = getParentPatternStmt()) {
    if (auto *FES = dyn_cast<ForEachStmt>(stmt))
      return FES->getPattern();

    if (auto *cs = dyn_cast<CaseStmt>(stmt)) {
      // In a case statement, search for the pattern that contains it.  This is
      // a bit silly, because you can't have something like "case x, y:" anyway.
      for (auto items : cs->getCaseLabelItems()) {
        if (items.getPattern()->containsVarDecl(this))
          return items.getPattern();
      }
    }

    if (auto *LCS = dyn_cast<LabeledConditionalStmt>(stmt)) {
      for (auto &elt : LCS->getCond())
        if (auto pat = elt.getPatternOrNull())
          if (pat->containsVarDecl(this))
            return pat;
    }
  }

  // Otherwise, check if we have to walk our case stmt's var decl list to find
  // the pattern.
  if (auto caseStmtPatternPair = findParentPatternCaseStmtAndPattern(this)) {
    return caseStmtPatternPair->second;
  }

  // Otherwise, this is a case we do not know or understand. Return nullptr to
  // signal we do not have any information.
  return nullptr;
}

NamedPattern *VarDecl::getNamingPattern() const {
  return evaluateOrDefault(getASTContext().evaluator,
                           NamingPatternRequest{const_cast<VarDecl *>(this)},
                           nullptr);
}

void VarDecl::setNamingPattern(NamedPattern *Pat) {
  getASTContext().evaluator.cacheOutput(NamingPatternRequest{this},
                                        std::move(Pat));
}

TypeRepr *VarDecl::getTypeReprOrParentPatternTypeRepr() const {
  if (auto *Param = dyn_cast<ParamDecl>(this))
    return Param->getTypeRepr();

  auto *ParentPattern = getParentPattern();

  if (auto *TyPattern = dyn_cast_or_null<TypedPattern>(ParentPattern))
    return TyPattern->getTypeRepr();

  // Handle typed if/guard/while-let as a special case (e.g. `if let x: Int
  // = Optional.some(4)`), since the `TypedPattern` is not the top-level
  // pattern here - instead it is an implicit `OptionalSomePattern`
  if (auto *SomePattern =
          dyn_cast_or_null<OptionalSomePattern>(ParentPattern)) {
    if (auto *TyPattern =
            dyn_cast<TypedPattern>(SomePattern->getSubPattern())) {
      return TyPattern->getTypeRepr();
    }
  }

  return nullptr;
}

NullablePtr<VarDecl>
VarDecl::getCorrespondingFirstCaseLabelItemVarDecl() const {
  if (!hasName())
    return nullptr;

  auto *caseStmt = dyn_cast_or_null<CaseStmt>(getRecursiveParentPatternStmt());
  if (!caseStmt)
    return nullptr;

  auto *pattern = caseStmt->getCaseLabelItems().front().getPattern();
  SmallVector<VarDecl *, 8> vars;
  pattern->collectVariables(vars);
  for (auto *vd : vars) {
    if (vd->hasName() && vd->getName() == getName())
      return vd;
  }
  return nullptr;
}

bool VarDecl::isCaseBodyVariable() const {
  auto *caseStmt = dyn_cast_or_null<CaseStmt>(getRecursiveParentPatternStmt());
  if (!caseStmt)
    return false;
  return llvm::any_of(caseStmt->getCaseBodyVariablesOrEmptyArray(),
                      [&](VarDecl *vd) { return vd == this; });
}

NullablePtr<VarDecl> VarDecl::getCorrespondingCaseBodyVariable() const {
  // Only var decls associated with case statements can have child var decls.
  auto *caseStmt = dyn_cast_or_null<CaseStmt>(getRecursiveParentPatternStmt());
  if (!caseStmt)
    return nullptr;

  // If this var decl doesn't have a name, it can not have a corresponding case
  // body variable.
  if (!hasName())
    return nullptr;

  auto name = getName();

  // A var decl associated with a case stmt implies that the case stmt has body
  // var decls. So we can access the optional value here without worry.
  auto caseBodyVars = caseStmt->getCaseBodyVariables();
  auto result = llvm::find_if(caseBodyVars, [&](VarDecl *caseBodyVar) {
    return caseBodyVar->getName() == name;
  });
  return (result != caseBodyVars.end()) ? *result : nullptr;
}

bool VarDecl::isSelfParameter() const {
  if (isa<ParamDecl>(this)) {
    if (auto *AFD = dyn_cast<AbstractFunctionDecl>(getDeclContext()))
      return AFD->getImplicitSelfDecl(/*createIfNeeded=*/false) == this;
    if (auto *PBI = dyn_cast<PatternBindingInitializer>(getDeclContext()))
      return PBI->getImplicitSelfDecl() == this;
  }

  return false;
}

bool VarDecl::isActorSelf() const {
  if (!isSelfParameter() && !isSelfParamCapture())
    return false;

  auto *dc = getDeclContext();
  while (!dc->isTypeContext() && !dc->isModuleScopeContext())
    dc = dc->getParent();

  // Check if this `self` parameter belogs to an actor declaration or
  // extension.
  auto nominal = dc->getSelfNominalTypeDecl();
  return nominal && nominal->isActor();
}

/// Whether the given variable is the backing storage property for
/// a declared property that is either `lazy` or has an attached
/// property wrapper.
static bool isBackingStorageForDeclaredProperty(const VarDecl *var) {
  if (var->isLazyStorageProperty())
    return true;

  if (var->getOriginalWrappedProperty())
    return true;

  return false;
}

/// Whether the given variable is a delcared property that has separate backing storage.
static bool isDeclaredPropertyWithBackingStorage(const VarDecl *var) {
  if (var->getAttrs().hasAttribute<LazyAttr>())
    return true;

  if (var->hasAttachedPropertyWrapper())
    return true;

  return false;
}

bool VarDecl::isMemberwiseInitialized(bool preferDeclaredProperties) const {
  // Only non-static properties in type context can be part of a memberwise
  // initializer.
  if (!getDeclContext()->isTypeContext() || isStatic())
    return false;

  // If this is a stored property, and not a backing property in a case where
  // we only want to see the declared properties, it can be memberwise
  // initialized.
  if (hasStorage() && preferDeclaredProperties &&
      isBackingStorageForDeclaredProperty(this))
    return false;

  // If this is a computed property, it's not memberwise initialized unless
  // the caller has asked for the declared properties and it is either a
  // `lazy` property or a property with an attached wrapper.
  if (!hasStorage() &&
      !(preferDeclaredProperties &&
        isDeclaredPropertyWithBackingStorage(this)))
    return false;

  // Initialized 'let' properties have storage, but don't get an argument
  // to the memberwise initializer since they already have an initial
  // value that cannot be overridden.
  if (isLet() && isParentInitialized())
    return false;

  // Properties with attached wrappers that have an access level < internal
  // but do have an initializer don't participate in the memberwise
  // initializer, because they would arbitrarily lower the access of the
  // memberwise initializer.
  auto origVar = this;
  if (auto origWrapped = getOriginalWrappedProperty())
    origVar = origWrapped;
  if (origVar->getFormalAccess() < AccessLevel::Internal &&
      origVar->hasAttachedPropertyWrapper() &&
      (origVar->isParentInitialized() ||
       (origVar->getParentPatternBinding() &&
        origVar->getParentPatternBinding()->isDefaultInitializable())))
    return false;

  return true;
}

bool VarDecl::isAsyncLet() const {
  return getAttrs().hasAttribute<AsyncAttr>();
}

void ParamDecl::setSpecifier(Specifier specifier) {
  // FIXME: Revisit this; in particular shouldn't __owned parameters be
  // ::Let also?
  setIntroducer(specifier == ParamSpecifier::Default
                ? VarDecl::Introducer::Let
                : VarDecl::Introducer::Var);
  Bits.ParamDecl.Specifier = static_cast<unsigned>(specifier);
  Bits.ParamDecl.SpecifierComputed = true;
}

bool ParamDecl::isAnonClosureParam() const {
  auto name = getName();
  if (name.empty())
    return false;

  auto nameStr = name.str();
  if (nameStr.empty())
    return false;

  return nameStr[0] == '$';
}

ParamDecl::Specifier ParamDecl::getSpecifier() const {
  auto &ctx = getASTContext();

  auto mutableThis = const_cast<ParamDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           ParamSpecifierRequest{mutableThis},
                           ParamDecl::Specifier::Default);
}

StaticSpellingKind AbstractStorageDecl::getCorrectStaticSpelling() const {
  if (!isStatic())
    return StaticSpellingKind::None;
  if (auto *VD = dyn_cast<VarDecl>(this)) {
    if (auto *PBD = VD->getParentPatternBinding()) {
      if (PBD->getStaticSpelling() != StaticSpellingKind::None)
        return PBD->getStaticSpelling();
    }
  } else if (auto *SD = dyn_cast<SubscriptDecl>(this)) {
    return SD->getStaticSpelling();
  }

  return getCorrectStaticSpellingForDecl(this);
}

llvm::TinyPtrVector<CustomAttr *> VarDecl::getAttachedPropertyWrappers() const {
  auto mutableThis = const_cast<VarDecl *>(this);
  return evaluateOrDefault(getASTContext().evaluator,
                           AttachedPropertyWrappersRequest{mutableThis},
                           { });
}

/// Whether this property has any attached property wrappers.
bool VarDecl::hasAttachedPropertyWrapper() const {
  if (getAttrs().hasAttribute<CustomAttr>()) {
    if (!getAttachedPropertyWrappers().empty())
      return true;
  }

  if (hasImplicitPropertyWrapper())
    return true;

  return false;
}

bool VarDecl::hasImplicitPropertyWrapper() const {
  if (getAttrs().hasAttribute<CustomAttr>()) {
    if (!getAttachedPropertyWrappers().empty())
      return false;
  }

  if (isImplicit())
    return false;

  if (!isa<ParamDecl>(this))
    return false;

  if (!isa<AbstractClosureExpr>(getDeclContext()))
    return false;

  return getName().hasDollarPrefix();
}

bool VarDecl::hasExternalPropertyWrapper() const {
  if (!hasAttachedPropertyWrapper() || !isa<ParamDecl>(this))
    return false;

  // This decision needs to be made before closures are type checked (and
  // the wrapper types are potentially inferred) so closure parameters with
  // property wrappers are always "external". This is fine, because the
  // type checker will always inject a thunk with the wrapped or projected type
  // around the closure, so the wrapper will never affect the caller's
  // arguments directly anyway.
  if (isa<AbstractClosureExpr>(getDeclContext()))
    return true;

  // Wrappers with attribute arguments are always implementation-detail.
  if (getAttachedPropertyWrappers().front()->hasArgs())
    return false;

  auto wrapperInfo = getAttachedPropertyWrapperTypeInfo(0);
  return wrapperInfo.projectedValueVar && wrapperInfo.hasProjectedValueInit;
}

/// Whether all of the attached property wrappers have an init(wrappedValue:)
/// initializer.
bool VarDecl::allAttachedPropertyWrappersHaveWrappedValueInit() const {
  for (unsigned i : indices(getAttachedPropertyWrappers())) {
    if (!getAttachedPropertyWrapperTypeInfo(i).wrappedValueInit)
      return false;
  }
  
  return true;
}

PropertyWrapperTypeInfo
VarDecl::getAttachedPropertyWrapperTypeInfo(unsigned i) const {
  NominalTypeDecl *nominal;
  if (hasImplicitPropertyWrapper()) {
    assert(i == 0);
    nominal = getInterfaceType()->getAnyNominal();
  } else {
    auto attrs = getAttachedPropertyWrappers();
    if (i >= attrs.size())
      return PropertyWrapperTypeInfo();

    auto attr = attrs[i];
    auto dc = getDeclContext();
    ASTContext &ctx = getASTContext();
    nominal = evaluateOrDefault(
        ctx.evaluator, CustomAttrNominalRequest{attr, dc}, nullptr);
  }

  if (!nominal)
    return PropertyWrapperTypeInfo();

  return nominal->getPropertyWrapperTypeInfo();
}

Type VarDecl::getAttachedPropertyWrapperType(unsigned index) const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<VarDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator,
      AttachedPropertyWrapperTypeRequest{mutableThis, index},
      Type());
}

Type VarDecl::getPropertyWrapperBackingPropertyType() const {
  ASTContext &ctx = getASTContext();
  auto mutableThis = const_cast<VarDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator, PropertyWrapperBackingPropertyTypeRequest{mutableThis},
      Type());
}

PropertyWrapperAuxiliaryVariables
VarDecl::getPropertyWrapperAuxiliaryVariables() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<VarDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator,
      PropertyWrapperAuxiliaryVariablesRequest{mutableThis},
      PropertyWrapperAuxiliaryVariables());
}

PropertyWrapperInitializerInfo
VarDecl::getPropertyWrapperInitializerInfo() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<VarDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator,
      PropertyWrapperInitializerInfoRequest{mutableThis},
      PropertyWrapperInitializerInfo());
}

Optional<PropertyWrapperMutability>
VarDecl::getPropertyWrapperMutability() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<VarDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator,
      PropertyWrapperMutabilityRequest{mutableThis},
      None);
}

Optional<PropertyWrapperSynthesizedPropertyKind>
VarDecl::getPropertyWrapperSynthesizedPropertyKind() const {
  if (getOriginalWrappedProperty(
          PropertyWrapperSynthesizedPropertyKind::Backing))
    return PropertyWrapperSynthesizedPropertyKind::Backing;
  if (getOriginalWrappedProperty(
          PropertyWrapperSynthesizedPropertyKind::Projection))
    return PropertyWrapperSynthesizedPropertyKind::Projection;
  return None;
}

VarDecl *VarDecl::getPropertyWrapperBackingProperty() const {
  return getPropertyWrapperAuxiliaryVariables().backingVar;
}

VarDecl *VarDecl::getPropertyWrapperProjectionVar() const {
  return getPropertyWrapperAuxiliaryVariables().projectionVar;
}

VarDecl *VarDecl::getPropertyWrapperWrappedValueVar() const {
  return getPropertyWrapperAuxiliaryVariables().localWrappedValueVar;
}

void VarDecl::visitAuxiliaryDecls(llvm::function_ref<void(VarDecl *)> visit) const {
  if (getDeclContext()->isTypeContext() || isImplicit())
    return;

  if (getAttrs().hasAttribute<LazyAttr>()) {
    if (auto *backingVar = getLazyStorageProperty())
      visit(backingVar);
  }

  if (getAttrs().hasAttribute<CustomAttr>() || hasImplicitPropertyWrapper()) {
    if (auto *backingVar = getPropertyWrapperBackingProperty())
      visit(backingVar);

    if (auto *projectionVar = getPropertyWrapperProjectionVar())
      visit(projectionVar);

    if (auto *wrappedValueVar = getPropertyWrapperWrappedValueVar())
      visit(wrappedValueVar);
  }
}

VarDecl *VarDecl::getLazyStorageProperty() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<VarDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator,
      LazyStoragePropertyRequest{mutableThis},
      {});
}

bool VarDecl::isPropertyMemberwiseInitializedWithWrappedType() const {
  auto customAttrs = getAttachedPropertyWrappers();
  if (customAttrs.empty())
    return false;

  auto *PBD = getParentPatternBinding();
  if (!PBD)
    return false;

  // If there was an initializer on the original property, initialize
  // via the initial value.
  if (PBD->getEqualLoc(0).isValid())
    return true;

  // If there was an initializer on the outermost wrapper, initialize
  // via the full wrapper.
  if (customAttrs[0]->hasArgs())
    return false;

  // Default initialization does not use a value.
  if (getAttachedPropertyWrapperTypeInfo(0).defaultInit)
    return false;

  // If all property wrappers have a wrappedValue initializer, the property
  // wrapper will be initialized that way.
  return allAttachedPropertyWrappersHaveWrappedValueInit();
}

Type VarDecl::getPropertyWrapperInitValueInterfaceType() const {
  auto initInfo = getPropertyWrapperInitializerInfo();
  if (!initInfo.getWrappedValuePlaceholder())
    return Type();

  Type valueInterfaceTy = initInfo.getWrappedValuePlaceholder()->getType();
  if (valueInterfaceTy->hasArchetype())
    valueInterfaceTy = valueInterfaceTy->mapTypeOutOfContext();

  return valueInterfaceTy;
}

Identifier VarDecl::getObjCPropertyName() const {
  if (auto attr = getAttrs().getAttribute<ObjCAttr>()) {
    if (auto name = attr->getName())
      return name->getSelectorPieces()[0];
  }

  return getName();
}

ObjCSelector VarDecl::getDefaultObjCGetterSelector(ASTContext &ctx,
                                                   Identifier propertyName) {
  return ObjCSelector(ctx, 0, propertyName);
}


ObjCSelector VarDecl::getDefaultObjCSetterSelector(ASTContext &ctx,
                                                   Identifier propertyName) {
  llvm::SmallString<16> scratch;
  scratch += "set";
  camel_case::appendSentenceCase(scratch, propertyName.str());

  return ObjCSelector(ctx, 1, ctx.getIdentifier(scratch));
}

/// If this is a simple 'let' constant, emit a note with a fixit indicating
/// that it can be rewritten to a 'var'.  This is used in situations where the
/// compiler detects obvious attempts to mutate a constant.
void VarDecl::emitLetToVarNoteIfSimple(DeclContext *UseDC) const {
  // If it isn't a 'let', don't touch it.
  if (!isLet()) return;

  // If this is the 'self' argument of a non-mutating method in a value type,
  // suggest adding 'mutating' to the method.
  if (isSelfParameter() && UseDC) {
    // If the problematic decl is 'self', then we might be trying to mutate
    // a property in a non-mutating method.
    auto FD = dyn_cast_or_null<FuncDecl>(UseDC->getInnermostMethodContext());

    if (FD && !FD->isMutating() && !FD->isImplicit() && FD->isInstanceMember()&&
        !FD->getDeclContext()->getDeclaredInterfaceType()
                 ->hasReferenceSemantics()) {
      // Do not suggest the fix-it in implicit getters
      if (auto AD = dyn_cast<AccessorDecl>(FD)) {
        if (AD->isImplicitGetter())
          return;
      }

      auto &d = getASTContext().Diags;
      auto diags = d.diagnose(FD->getFuncLoc(), diag::change_to_mutating,
                              isa<AccessorDecl>(FD));
      if (auto nonmutatingAttr =
              FD->getAttrs().getAttribute<NonMutatingAttr>()) {
        diags.fixItReplace(nonmutatingAttr->getLocation(), "mutating");
      } else {
        diags.fixItInsert(FD->getFuncLoc(), "mutating ");
      }
      return;
    }
  }

  // Besides self, don't suggest mutability for explicit function parameters.
  if (isa<ParamDecl>(this)) return;

  // Don't suggest any fixes for capture list elements.
  if (isCaptureList()) return;

  // If this is a normal variable definition, then we can change 'let' to 'var'.
  // We even are willing to suggest this for multi-variable binding, like
  //   "let (a,b) = "
  // since the user has to choose to apply this anyway.
  if (auto *PBD = getParentPatternBinding()) {
    // Don't touch generated or invalid code.
    if (PBD->getLoc().isInvalid() || PBD->isImplicit())
      return;

    auto &d = getASTContext().Diags;
    d.diagnose(PBD->getLoc(), diag::convert_let_to_var)
     .fixItReplace(PBD->getLoc(), "var");
    return;
  }
}

ParamDecl::ParamDecl(SourceLoc specifierLoc,
                     SourceLoc argumentNameLoc, Identifier argumentName,
                     SourceLoc parameterNameLoc, Identifier parameterName,
                     DeclContext *dc)
    : VarDecl(DeclKind::Param,
              /*IsStatic*/ false,
              VarDecl::Introducer::Let, parameterNameLoc, parameterName, dc,
              StorageIsNotMutable),
      ArgumentNameAndFlags(argumentName, None),
      ParameterNameLoc(parameterNameLoc),
      ArgumentNameLoc(argumentNameLoc), SpecifierLoc(specifierLoc) {
  Bits.ParamDecl.SpecifierComputed = false;
  Bits.ParamDecl.defaultArgumentKind =
    static_cast<unsigned>(DefaultArgumentKind::None);
}

ParamDecl *ParamDecl::cloneWithoutType(const ASTContext &Ctx, ParamDecl *PD) {
  auto *Clone = new (Ctx) ParamDecl(
      SourceLoc(), SourceLoc(), PD->getArgumentName(),
      SourceLoc(), PD->getParameterName(), PD->getDeclContext());
  Clone->DefaultValueAndFlags.setPointerAndInt(
      nullptr, PD->DefaultValueAndFlags.getInt());
  Clone->Bits.ParamDecl.defaultArgumentKind =
      PD->Bits.ParamDecl.defaultArgumentKind;

  Clone->setSpecifier(PD->getSpecifier());
  Clone->setImplicitlyUnwrappedOptional(PD->isImplicitlyUnwrappedOptional());
  if (PD->isImplicit()) {
    Clone->setImplicit();
  }
  return Clone;
}

ParamDecl *ParamDecl::clone(const ASTContext &Ctx, ParamDecl *PD) {
  auto *Clone = ParamDecl::cloneWithoutType(Ctx, PD);
  Clone->setInterfaceType(PD->getInterfaceType());
  return Clone;
}

/// Retrieve the type of 'self' for the given context.
Type DeclContext::getSelfTypeInContext() const {
  assert(isTypeContext());

  // For a protocol or extension thereof, the type is 'Self'.
  if (getSelfProtocolDecl()) {
    auto selfType = getProtocolSelfType();
    if (!selfType)
      return ErrorType::get(getASTContext());
    return mapTypeIntoContext(selfType);
  }
  return getDeclaredTypeInContext();
}

/// Retrieve the interface type of 'self' for the given context.
Type DeclContext::getSelfInterfaceType() const {
  assert(isTypeContext());

  // For a protocol or extension thereof, the type is 'Self'.
  if (getSelfProtocolDecl()) {
    auto selfType = getProtocolSelfType();
    if (!selfType)
      return ErrorType::get(getASTContext());
    return selfType;
  }
  return getDeclaredInterfaceType();
}

/// Return the full source range of this parameter.
SourceRange ParamDecl::getSourceRange() const {
  SourceLoc APINameLoc = getArgumentNameLoc();
  SourceLoc nameLoc = getNameLoc();

  SourceLoc startLoc;
  if (APINameLoc.isValid())
    startLoc = APINameLoc;
  else if (nameLoc.isValid())
    startLoc = nameLoc;
  else if (auto *repr = getTypeRepr())
    startLoc = repr->getStartLoc();

  if (startLoc.isInvalid())
    return SourceRange();

  // It would be nice to extend the front of the range to show where inout is,
  // but we don't have that location info.  Extend the back of the range to the
  // location of the default argument, or the typeloc if they are valid.
  if (auto expr = getStructuralDefaultExpr()) {
    auto endLoc = expr->getEndLoc();
    if (endLoc.isValid())
      return SourceRange(startLoc, endLoc);
  }
  
  // If the typeloc has a valid location, use it to end the range.
  if (auto typeRepr = getTypeRepr()) {
    auto endLoc = typeRepr->getEndLoc();
    if (endLoc.isValid())
      return SourceRange(startLoc, endLoc);
  }

  // The name has a location we can use.
  if (nameLoc.isValid())
    return SourceRange(startLoc, nameLoc);

  return startLoc;
}

bool ParamDecl::isNonEphemeral() const {
  if (getAttrs().hasAttribute<NonEphemeralAttr>())
    return true;

  // Only enum element parameters are non-ephemeral without '@_nonEphemeral'.
  auto *parentDecl = getDeclContext()->getAsDecl();
  if (!parentDecl || !isa<EnumElementDecl>(parentDecl))
    return false;

  // Only pointer parameters can be non-ephemeral.
  auto ty = getInterfaceType();
  if (!ty->lookThroughSingleOptionalType()->getAnyPointerElementType())
    return false;

  return true;
}

void ParamDecl::setNonEphemeralIfPossible() {
  assert(hasInterfaceType() && "Must be pre-typechecked.");
  // Don't apply the attribute if this isn't a pointer param.
  auto type = getInterfaceType();
  if (!type->lookThroughSingleOptionalType()->getAnyPointerElementType())
    return;

  if (!getAttrs().hasAttribute<NonEphemeralAttr>()) {
    auto &ctx = getASTContext();
    getAttrs().add(new (ctx) NonEphemeralAttr(/*IsImplicit*/ true));
  }
}

Type ParamDecl::getVarargBaseTy(Type VarArgT) {
  TypeBase *T = VarArgT.getPointer();
  if (auto *AT = dyn_cast<VariadicSequenceType>(T))
    return AT->getBaseType();
  if (auto *BGT = dyn_cast<BoundGenericType>(T)) {
    // It's the stdlib Array<T>.
    return BGT->getGenericArgs()[0];
  }
  return T;
}

AnyFunctionType::Param ParamDecl::toFunctionParam(Type type) const {
  if (!type) {
    type = getInterfaceType();

    if (hasExternalPropertyWrapper()) {
      if (auto wrapper = getPropertyWrapperBackingPropertyType()) {
        type = wrapper;
      }
    }
  }

  if (isVariadic())
    type = ParamDecl::getVarargBaseTy(type);

  auto label = getArgumentName();
  auto internalLabel = getParameterName();
  auto flags = ParameterTypeFlags::fromParameterType(
      type, isVariadic(), isAutoClosure(), isNonEphemeral(),
      getValueOwnership(), isIsolated(), /*isNoDerivative*/ false,
      isCompileTimeConst());
  return AnyFunctionType::Param(type, label, flags, internalLabel);
}

Optional<Initializer *> ParamDecl::getCachedDefaultArgumentInitContext() const {
  if (auto *defaultInfo = DefaultValueAndFlags.getPointer())
    if (auto *init = defaultInfo->InitContextAndIsTypeChecked.getPointer())
      return init;

  return None;
}

Initializer *ParamDecl::getDefaultArgumentInitContext() const {
  // If this param doesn't need a context, don't bother kicking off a request.
  if (!hasDefaultExpr() && !getStoredProperty())
    return nullptr;

  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<ParamDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator, DefaultArgumentInitContextRequest{mutableThis}, nullptr);
}

bool ParamDecl::hasDefaultExpr() const {
  switch (getDefaultArgumentKind()) {
  case DefaultArgumentKind::None:
  case DefaultArgumentKind::Inherited:
  case DefaultArgumentKind::StoredProperty:
    return false;
  case DefaultArgumentKind::Normal:
#define MAGIC_IDENTIFIER(NAME, STRING, SYNTAX_KIND) \
  case DefaultArgumentKind::NAME:
#include "swift/AST/MagicIdentifierKinds.def"
  case DefaultArgumentKind::NilLiteral:
  case DefaultArgumentKind::EmptyArray:
  case DefaultArgumentKind::EmptyDictionary:
    // Check if we have a structural default expr. This ensures we return false
    // for deserialized decls.
    return getStructuralDefaultExpr();
  }
  llvm_unreachable("Unhandled case in switch");
}

bool ParamDecl::hasCallerSideDefaultExpr() const {
  switch (getDefaultArgumentKind()) {
  case DefaultArgumentKind::None:
  case DefaultArgumentKind::Inherited:
  case DefaultArgumentKind::StoredProperty:
  case DefaultArgumentKind::Normal:
    return false;
#define MAGIC_IDENTIFIER(NAME, STRING, SYNTAX_KIND) \
  case DefaultArgumentKind::NAME:
#include "swift/AST/MagicIdentifierKinds.def"
  case DefaultArgumentKind::NilLiteral:
  case DefaultArgumentKind::EmptyArray:
  case DefaultArgumentKind::EmptyDictionary:
    return true;
  }
  llvm_unreachable("invalid default argument kind");
}

Expr *ParamDecl::getTypeCheckedDefaultExpr() const {
  // Don't kick off a request if we know there's no default expr. The only
  // exception is for inherited default args which we need to perform a couple
  // of semantic checks for.
  if (!hasDefaultExpr() &&
      getDefaultArgumentKind() != DefaultArgumentKind::Inherited) {
    return nullptr;
  }

  auto &ctx = getASTContext();
  if (Expr *E = evaluateOrDefault(
          ctx.evaluator,
          DefaultArgumentExprRequest{const_cast<ParamDecl *>(this)}, nullptr)) {
    return E;
  }
  return new (ctx) ErrorExpr(getSourceRange(), ErrorType::get(ctx));
}

void ParamDecl::setDefaultExpr(Expr *E, bool isTypeChecked) {
  if (!DefaultValueAndFlags.getPointer()) {
    if (!E) return;

    DefaultValueAndFlags.setPointer(
        getASTContext().Allocate<StoredDefaultArgument>());
  }

  auto *defaultInfo = DefaultValueAndFlags.getPointer();
  assert(defaultInfo->DefaultArg.isNull() ||
         defaultInfo->DefaultArg.is<Expr *>());

  if (!isTypeChecked) {
    assert(!defaultInfo->InitContextAndIsTypeChecked.getInt() &&
           "Can't overwrite type-checked default with un-type-checked default");
  }
  defaultInfo->DefaultArg = E;
  defaultInfo->InitContextAndIsTypeChecked.setInt(isTypeChecked);
}

void ParamDecl::setStoredProperty(VarDecl *var) {
  if (!DefaultValueAndFlags.getPointer()) {
    if (!var) return;

    DefaultValueAndFlags.setPointer(
      getASTContext().Allocate<StoredDefaultArgument>());
  }

  auto *defaultInfo = DefaultValueAndFlags.getPointer();
  assert(defaultInfo->DefaultArg.isNull() ||
         defaultInfo->DefaultArg.is<VarDecl *>());
  defaultInfo->DefaultArg = var;
}

Type ValueDecl::getResultBuilderType() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<ValueDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           ResultBuilderTypeRequest{mutableThis},
                           Type());
}

CustomAttr *ValueDecl::getAttachedResultBuilder() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<ValueDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           AttachedResultBuilderRequest{mutableThis},
                           nullptr);
}

void ParamDecl::setDefaultArgumentInitContext(Initializer *initContext) {
  auto oldContext = getCachedDefaultArgumentInitContext();
  assert((!oldContext || oldContext == initContext) &&
         "Cannot change init context after setting");

  auto *defaultInfo = DefaultValueAndFlags.getPointer();
  assert(defaultInfo);
  defaultInfo->InitContextAndIsTypeChecked.setPointer(initContext);
}

void ParamDecl::setDefaultArgumentCaptureInfo(CaptureInfo captures) {
  assert(DefaultValueAndFlags.getPointer());
  DefaultValueAndFlags.getPointer()->Captures = captures;
}

PropertyWrapperValuePlaceholderExpr *
swift::findWrappedValuePlaceholder(Expr *init) {
  class Walker : public ASTWalker {
  public:
    PropertyWrapperValuePlaceholderExpr *placeholder = nullptr;

    virtual std::pair<bool, Expr *> walkToExprPre(Expr *E) override {
      if (placeholder)
        return { false, E };

      if (auto *value = dyn_cast<PropertyWrapperValuePlaceholderExpr>(E)) {
        placeholder = value;
        return { false, value };
      }

      return { true, E };
    }
  } walker;
  init->walk(walker);

  return walker.placeholder;
}

/// Writes a tuple expression where each element is either `nil` or another such
/// tuple of nils.
/// This comes up when printing default arguments for memberwise initializers
/// that were created implicitly.
/// For example, this var:
/// ```
/// var x: (Int?, (Int?, Int?, ()))
/// ```
/// will produce `(nil, (nil, nil, ()))`
static void writeTupleOfNils(TupleType *type, llvm::raw_ostream &os) {
  os << '(';
  for (unsigned i = 0; i < type->getNumElements(); ++i) {
    auto &elt = type->getElement(i);
    if (elt.hasName()) {
      os << elt.getName().str() << ": ";
    }

    if (elt.getType()->getOptionalObjectType()) {
      os << "nil";
    } else {
      writeTupleOfNils(elt.getType()->castTo<TupleType>(), os);
    }
    if (i < type->getNumElements() - 1) {
      os << ", ";
    }
  }
  os << ')';
}

/// Determines if the given type is a potentially nested tuple of optional
/// types.
static bool isTupleOfOptionals(Type type) {
  auto tuple = type->getAs<TupleType>();
  if (!tuple) return false;
  for (auto elt : tuple->getElementTypes())
    if (!elt->getOptionalObjectType() && !isTupleOfOptionals(elt))
      return false;
  return true;
}

StringRef
ParamDecl::getDefaultValueStringRepresentation(
  SmallVectorImpl<char> &scratch) const {
  switch (getDefaultArgumentKind()) {
  case DefaultArgumentKind::None:
    llvm_unreachable("called on a ParamDecl with no default value");
  case DefaultArgumentKind::Normal: {
    assert(DefaultValueAndFlags.getPointer() &&
           "default value not provided yet");
    auto existing = DefaultValueAndFlags.getPointer()->StringRepresentation;
    if (!existing.empty())
      return existing;

    assert(hasDefaultExpr()
           && "Normal default argument with no default expression?!");
    return extractInlinableText(getASTContext().SourceMgr,
                                getStructuralDefaultExpr(), scratch);
  }
  case DefaultArgumentKind::StoredProperty: {
    assert(DefaultValueAndFlags.getPointer() &&
           "default value not provided yet");
    auto existing = DefaultValueAndFlags.getPointer()->StringRepresentation;
    if (!existing.empty())
      return existing;
    auto var = getStoredProperty();

    if (auto original = var->getOriginalWrappedProperty()) {
      auto wrapperAttrs = original->getAttachedPropertyWrappers();
      if (wrapperAttrs.size() > 0) {
        auto attr = wrapperAttrs.front();
        if (auto *args = attr->getArgs()) {
          SourceRange fullRange(attr->getTypeRepr()->getSourceRange().Start,
                                args->getEndLoc());
          auto charRange = Lexer::getCharSourceRangeFromSourceRange(
              getASTContext().SourceMgr, fullRange);
          return getASTContext().SourceMgr.extractText(charRange);
        }

        // If there is no initial wrapped value, we used the default initializer.
        Expr *wrappedValue = nullptr;
        if (auto *parentInit = original->getParentInitializer())
          if (auto *placeholder = findWrappedValuePlaceholder(parentInit))
            wrappedValue = placeholder->getOriginalWrappedValue();

        if (!wrappedValue) {
          if (auto type = original->getPropertyWrapperBackingPropertyType()) {
            if (auto nominal = type->getAnyNominal()) {
              scratch.clear();
              auto typeName = nominal->getName().str();
              scratch.append(typeName.begin(), typeName.end());
              scratch.push_back('(');
              scratch.push_back(')');
              return {scratch.data(), scratch.size()};
            }
          }

          return ".init()";
        }

        auto &sourceMgr = getASTContext().SourceMgr;
        return extractInlinableText(sourceMgr, wrappedValue, scratch);
      }
    }

    auto init = var->getParentInitializer();
    if (!init || !init->getSourceRange().isValid()) {
      // Special case: There are two possible times where we will synthesize a
      //               default initial value for a stored property: if the type
      //               is Optional, or if it's a (potentially nested) tuple of
      //               all Optional elements. If it's Optional, we'll set
      //               the DefaultArgumentKind to NilLiteral, but if we're still
      //               handling a StoredProperty, then we know it's a tuple.
      if (isTupleOfOptionals(getInterfaceType())) {
        llvm::raw_svector_ostream os(scratch);
        writeTupleOfNils(getInterfaceType()->castTo<TupleType>(), os);
        return os.str();
      }
      return "<<empty>>";
    }

    return extractInlinableText(getASTContext().SourceMgr,
                                init,
                                scratch);
  }
  case DefaultArgumentKind::Inherited: return "super";
#define MAGIC_IDENTIFIER(NAME, STRING, SYNTAX_KIND) \
  case DefaultArgumentKind::NAME: return STRING;
#include "swift/AST/MagicIdentifierKinds.def"
  case DefaultArgumentKind::NilLiteral: return "nil";
  case DefaultArgumentKind::EmptyArray: return "[]";
  case DefaultArgumentKind::EmptyDictionary: return "[:]";
  }
  llvm_unreachable("unhandled kind");
}

void
ParamDecl::setDefaultValueStringRepresentation(StringRef stringRepresentation) {
  assert(getDefaultArgumentKind() == DefaultArgumentKind::Normal ||
         getDefaultArgumentKind() == DefaultArgumentKind::StoredProperty);
  assert(!stringRepresentation.empty());

  if (!DefaultValueAndFlags.getPointer()) {
    DefaultValueAndFlags.setPointer(
        getASTContext().Allocate<StoredDefaultArgument>());
  }

  DefaultValueAndFlags.getPointer()->StringRepresentation =
      stringRepresentation;
}

void DefaultArgumentInitializer::changeFunction(
    DeclContext *parent, ParameterList *paramList) {
  if (parent->isLocalContext()) {
    setParent(parent);
  }

  auto param = paramList->get(getIndex());
  if (param->hasDefaultExpr() || param->getStoredProperty())
    param->setDefaultArgumentInitContext(this);
}

/// Determine whether the given Swift type is an integral type, i.e.,
/// a type that wraps a builtin integer.
static bool isIntegralType(Type type) {
  // Consider structs in the standard library module that wrap a builtin
  // integer type to be integral types.
  if (auto structTy = type->getAs<StructType>()) {
    auto structDecl = structTy->getDecl();
    const DeclContext *DC = structDecl->getDeclContext();
    if (!DC->isModuleScopeContext() || !DC->getParentModule()->isStdlibModule())
      return false;

    // Find the single ivar.
    VarDecl *singleVar = nullptr;
    for (auto member : structDecl->getStoredProperties()) {
      if (singleVar)
        return false;
      singleVar = member;
    }

    if (!singleVar)
      return false;

    // Check whether it has integer type.
    return singleVar->getInterfaceType()->is<BuiltinIntegerType>();
  }

  return false;
}

void SubscriptDecl::setIndices(ParameterList *p) {
  Indices = p;
  
  if (Indices)
    Indices->setDeclContextOfParamDecls(this);
}

Type SubscriptDecl::getElementInterfaceType() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<SubscriptDecl *>(this);
  if (auto type = evaluateOrDefault(ctx.evaluator,
                           ResultTypeRequest{mutableThis},
                           Type()))
    return type;
  return ErrorType::get(ctx);
}

ObjCSubscriptKind SubscriptDecl::getObjCSubscriptKind() const {
  // If the index type is an integral type, we have an indexed
  // subscript.
  if (auto funcTy = getInterfaceType()->getAs<AnyFunctionType>()) {
    auto params = funcTy->getParams();
    if (params.size() == 1)
      if (isIntegralType(params[0].getPlainType()))
        return ObjCSubscriptKind::Indexed;
  }

  // If the index type is an object type in Objective-C, we have a
  // keyed subscript.
  return ObjCSubscriptKind::Keyed;
}

void SubscriptDecl::setElementInterfaceType(Type type) {
  getASTContext().evaluator.cacheOutput(ResultTypeRequest{this},
                                        std::move(type));
}

SubscriptDecl *
SubscriptDecl::createDeserialized(ASTContext &Context, DeclName Name,
                                  StaticSpellingKind StaticSpelling,
                                  Type ElementTy, DeclContext *Parent,
                                  GenericParamList *GenericParams) {
  assert(ElementTy && "Deserialized element type must not be null");
  auto *const SD = new (Context)
      SubscriptDecl(Name, SourceLoc(), StaticSpelling, SourceLoc(), nullptr,
                    SourceLoc(), /*ElementTyR=*/nullptr, Parent, GenericParams);
  SD->setElementInterfaceType(ElementTy);
  return SD;
}

SubscriptDecl *SubscriptDecl::create(ASTContext &Context, DeclName Name,
                                     SourceLoc StaticLoc,
                                     StaticSpellingKind StaticSpelling,
                                     SourceLoc SubscriptLoc,
                                     ParameterList *Indices, SourceLoc ArrowLoc,
                                     TypeRepr *ElementTyR, DeclContext *Parent,
                                     GenericParamList *GenericParams) {
  assert(ElementTyR);
  auto *const SD = new (Context)
      SubscriptDecl(Name, StaticLoc, StaticSpelling, SubscriptLoc, Indices,
                    ArrowLoc, ElementTyR, Parent, GenericParams);
  return SD;
}

SubscriptDecl *SubscriptDecl::createImported(ASTContext &Context, DeclName Name,
                                             SourceLoc SubscriptLoc,
                                             ParameterList *Indices,
                                             SourceLoc ArrowLoc, Type ElementTy,
                                             DeclContext *Parent,
                                             ClangNode ClangN) {
  assert(ClangN && ElementTy);
  auto *DeclPtr = allocateMemoryForDecl<SubscriptDecl>(
      Context, sizeof(SubscriptDecl), /*includeSpaceForClangNode=*/true);

  auto *const SD = ::new (DeclPtr)
      SubscriptDecl(Name, SourceLoc(), StaticSpellingKind::None, SubscriptLoc,
                    Indices, ArrowLoc, /*ElementTyR=*/nullptr, Parent,
                    /*GenericParams=*/nullptr);
  SD->setElementInterfaceType(ElementTy);
  SD->setClangNode(ClangN);
  return SD;
}

SourceRange SubscriptDecl::getSourceRange() const {
  return {getSubscriptLoc(), getEndLoc()};
}

SourceRange SubscriptDecl::getSignatureSourceRange() const {
  if (isImplicit())
    return SourceRange();
  if (auto Indices = getIndices()) {
    auto End = Indices->getEndLoc();
    if (End.isValid()) {
      return SourceRange(getSubscriptLoc(), End);
    }
  }
  return getSubscriptLoc();
}

DeclName AbstractFunctionDecl::getEffectiveFullName() const {
  if (getName())
    return getName();

  if (auto accessor = dyn_cast<AccessorDecl>(this)) {
    auto &ctx = getASTContext();
    auto storage = accessor->getStorage();
    auto subscript = dyn_cast<SubscriptDecl>(storage);
    switch (accessor->getAccessorKind()) {
    // These don't have any extra implicit parameters.
    case AccessorKind::Address:
    case AccessorKind::MutableAddress:
    case AccessorKind::Get:
    case AccessorKind::Read:
    case AccessorKind::Modify:
      return subscript ? subscript->getName()
                       : DeclName(ctx, storage->getBaseName(),
                                  ArrayRef<Identifier>());

    case AccessorKind::Set:
    case AccessorKind::DidSet:
    case AccessorKind::WillSet: {
      SmallVector<Identifier, 4> argNames;
      // The implicit value/buffer parameter.
      argNames.push_back(Identifier());
      // The subscript index parameters.
      if (subscript) {
        argNames.append(subscript->getName().getArgumentNames().begin(),
                        subscript->getName().getArgumentNames().end());
      }
      return DeclName(ctx, storage->getBaseName(), argNames);
    }
    }
    llvm_unreachable("bad accessor kind");
  }

  return DeclName();
}

ParameterList *swift::getParameterList(ValueDecl *source) {
  if (auto *AFD = dyn_cast<AbstractFunctionDecl>(source)) {
    return AFD->getParameters();
  } else if (auto *EED = dyn_cast<EnumElementDecl>(source)) {
    return EED->getParameterList();
  } else if (auto *SD = dyn_cast<SubscriptDecl>(source)) {
    return SD->getIndices();
  }

  return nullptr;
}

const ParamDecl *swift::getParameterAt(const ValueDecl *source,
                                       unsigned index) {
  if (auto *params = getParameterList(const_cast<ValueDecl *>(source))) {
    return params->get(index);
  }
  return nullptr;
}

Type AbstractFunctionDecl::getMethodInterfaceType() const {
  assert(getDeclContext()->isTypeContext());
  auto Ty = getInterfaceType();
  if (Ty->is<ErrorType>())
    return Ty;
  return Ty->castTo<AnyFunctionType>()->getResult();
}

bool AbstractFunctionDecl::hasDynamicSelfResult() const {
  if (auto *funcDecl = dyn_cast<FuncDecl>(this))
    return funcDecl->getResultInterfaceType()->hasDynamicSelfType();
  return isa<ConstructorDecl>(this);
}

AbstractFunctionDecl *AbstractFunctionDecl::getAsyncAlternative() const {
  // Async functions can't have async alternatives
  if (hasAsync())
    return nullptr;

  const AvailableAttr *avAttr = nullptr;
  for (const auto *attr : getAttrs().getAttributes<AvailableAttr>()) {
    // If there's an attribute with an already-resolved rename decl, use it
    if (attr->RenameDecl) {
      avAttr = attr;
      break;
    }

    // Otherwise prefer the first availability attribute with no platform and
    // rename parameter, falling back to the first with a rename. Note that
    // `getAttrs` is in reverse source order, so the last attribute is the
    // first in source
    if (!attr->Rename.empty() && (attr->Platform == PlatformKind::none ||
                                  !avAttr))
      avAttr = attr;
  }

  auto *renamedDecl = evaluateOrDefault(
      getASTContext().evaluator, RenamedDeclRequest{this, avAttr}, nullptr);
  auto *alternative = dyn_cast_or_null<AbstractFunctionDecl>(renamedDecl);
  if (!alternative || !alternative->hasAsync())
    return nullptr;
  return alternative;
}

static bool isPotentialCompletionHandler(const ParamDecl *param) {
  if (!param->getType())
    return false;

  const AnyFunctionType *paramType = param->getType()->getAs<AnyFunctionType>();
  return paramType && paramType->getResult()->isVoid() &&
         !paramType->isNoEscape() && !param->isAutoClosure();
}

Optional<unsigned> AbstractFunctionDecl::findPotentialCompletionHandlerParam(
    const AbstractFunctionDecl *asyncAlternative) const {
  const ParameterList *params = getParameters();
  if (params->size() == 0)
    return None;

  // If no async alternative given, just find the last parameter that matches
  // a completion handler signature
  if (!asyncAlternative) {
    for (int i = params->size() - 1; i >= 0; --i) {
      if (isPotentialCompletionHandler(params->get(i)))
        return i;
    }
    return None;
  }

  // If this is an imported function with an async convention then we already
  // have the index, grab it from there
  auto asyncConvention = asyncAlternative->getForeignAsyncConvention();
  if (asyncConvention) {
    auto errorConvention = asyncAlternative->getForeignErrorConvention();
    unsigned handlerIndex = asyncConvention->completionHandlerParamIndex();
    if (errorConvention &&
        !errorConvention->isErrorParameterReplacedWithVoid() &&
        handlerIndex >= errorConvention->getErrorParameterIndex()) {
      handlerIndex--;
    }
    return handlerIndex;
  }

  // Otherwise, match up the parameters of each function and return the single
  // missing parameter that must also match a completion handler signature.
  // Ignore any defaulted params in the alternative if their label is different
  // to the corresponding param in the original function.

  const ParameterList *asyncParams = asyncAlternative->getParameters();
  unsigned paramIndex = 0;
  unsigned asyncParamIndex = 0;
  Optional<unsigned> potentialParam;
  while (paramIndex < params->size() || asyncParamIndex < asyncParams->size()) {
    if (paramIndex >= params->size()) {
      // Have more async params than original params, if we haven't found a
      // completion handler then there isn't going to be any. If we have then
      // ensure the rest of the async params are defaulted
      if (!potentialParam ||
          !asyncParams->get(asyncParamIndex)->isDefaultArgument())
        return None;
      asyncParamIndex++;
      continue;
    }

    auto *param = params->get(paramIndex);
    bool paramMatches = false;
    if (asyncParamIndex < asyncParams->size()) {
      const ParamDecl *asyncParam = asyncParams->get(asyncParamIndex);

      // Skip if the labels are different and it's defaulted
      if (param->getArgumentName() != asyncParam->getArgumentName() &&
          asyncParam->isDefaultArgument()) {
        asyncParamIndex++;
        continue;
      }

      // Don't have types for some reason, just return no match
      if (!param->getType() || !asyncParam->getType())
        return None;

      paramMatches = param->getType()->matchesParameter(asyncParam->getType(),
                                                        TypeMatchOptions());
    }

    if (paramMatches) {
      paramIndex++;
      asyncParamIndex++;
      continue;
    }

    // Param doesn't match, either it's the first completion handler or these
    // functions don't match
    if (potentialParam || !isPotentialCompletionHandler(param))
      return None;

    // The next original param should match the current async, so don't
    // increment the async index
    potentialParam = paramIndex;
    paramIndex++;
  }
  return potentialParam;
}

bool AbstractFunctionDecl::argumentNameIsAPIByDefault() const {
  // Initializers have argument labels.
  if (isa<ConstructorDecl>(this))
    return true;

  if (auto func = dyn_cast<FuncDecl>(this)) {
    // Operators do not have argument labels.
    if (func->isOperator())
      return false;

    // Other functions have argument labels for all arguments
    return true;
  }

  assert(isa<DestructorDecl>(this));
  return false;
}

bool AbstractFunctionDecl::isSendable() const {
  return getAttrs().hasAttribute<SendableAttr>();
}

bool AbstractFunctionDecl::isDistributed() const {
  return this->getAttrs().hasAttribute<DistributedActorAttr>();
}

AbstractFunctionDecl*
AbstractFunctionDecl::getDistributedActorRemoteFuncDecl() const {
  if (!this->isDistributed())
    return nullptr;

  auto mutableThis = const_cast<AbstractFunctionDecl *>(this);
  return evaluateOrDefault(
      getASTContext().evaluator,
      GetDistributedRemoteFuncRequest{mutableThis},
      nullptr);
}

BraceStmt *AbstractFunctionDecl::getBody(bool canSynthesize) const {
  if ((getBodyKind() == BodyKind::Synthesize ||
       getBodyKind() == BodyKind::Unparsed) &&
      !canSynthesize)
    return nullptr;

  ASTContext &ctx = getASTContext();

  // Don't allow getBody() to trigger parsing of an unparsed body containing the
  // code completion location.
  if (getBodyKind() == BodyKind::Unparsed &&
      ctx.SourceMgr.rangeContainsCodeCompletionLoc(getBodySourceRange())) {
    return nullptr;
  }

  auto mutableThis = const_cast<AbstractFunctionDecl *>(this);
  return evaluateOrDefault(ctx.evaluator,
                           ParseAbstractFunctionBodyRequest{mutableThis},
                           nullptr);
}

BraceStmt *AbstractFunctionDecl::getTypecheckedBody() const {
  auto &ctx = getASTContext();
  auto *mutableThis = const_cast<AbstractFunctionDecl *>(this);
  return evaluateOrDefault(
      ctx.evaluator, TypeCheckFunctionBodyRequest{mutableThis}, nullptr);
}

void AbstractFunctionDecl::setBody(BraceStmt *S, BodyKind NewBodyKind) {
  assert(getBodyKind() != BodyKind::Skipped &&
         "cannot set a body if it was skipped");

  Body = S;
  setBodyKind(NewBodyKind);

  // Need to recompute init body kind.
  if (NewBodyKind < BodyKind::TypeChecked) {
    if (auto *ctor = dyn_cast<ConstructorDecl>(this))
      ctor->clearCachedDelegatingOrChainedInitKind();
  }
}

void AbstractFunctionDecl::setBodyToBeReparsed(SourceRange bodyRange) {
  assert(bodyRange.isValid());
  assert(getBodyKind() == BodyKind::Unparsed ||
         getBodyKind() == BodyKind::Parsed ||
         getBodyKind() == BodyKind::TypeChecked);
  assert(getASTContext().SourceMgr.rangeContainsTokenLoc(
             bodyRange, getASTContext().SourceMgr.getCodeCompletionLoc()) &&
         "This function is only intended to be used for code completion");

  keepOriginalBodySourceRange();
  BodyRange = bodyRange;
  setBodyKind(BodyKind::Unparsed);
}

SourceRange AbstractFunctionDecl::getBodySourceRange() const {
  switch (getBodyKind()) {
  case BodyKind::None:
  case BodyKind::SILSynthesize:
  case BodyKind::Deserialized:
  case BodyKind::Synthesize:
    return SourceRange();

  case BodyKind::Parsed:
  case BodyKind::TypeChecked:
    if (auto body = getBody(/*canSynthesize=*/false))
      return body->getSourceRange();

    return SourceRange();

  case BodyKind::Skipped:
  case BodyKind::Unparsed:
    return BodyRange;
  }
  llvm_unreachable("bad BodyKind");
}

SourceRange AbstractFunctionDecl::getSignatureSourceRange() const {
  if (isImplicit())
    return SourceRange();

  auto paramList = getParameters();

  auto endLoc = paramList->getSourceRange().End;
  if (endLoc.isValid())
    return SourceRange(getNameLoc(), endLoc);

  return getNameLoc();
}

ObjCSelector
AbstractFunctionDecl::getObjCSelector(DeclName preferredName,
                                      bool skipIsObjCResolution) const {
  // FIXME: Forces computation of the Objective-C selector.
  if (!skipIsObjCResolution)
    (void)isObjC();

  // If there is an @objc attribute with a name, use that name.
  auto *objc = getAttrs().getAttribute<ObjCAttr>();
  if (auto name = getNameFromObjcAttribute(objc, preferredName)) {
    return *name;
  }

  auto &ctx = getASTContext();

  StringRef baseNameStr;
  if (auto destructor = dyn_cast<DestructorDecl>(this)) {
    return destructor->getObjCSelector();
  } else if (auto func = dyn_cast<FuncDecl>(this)) {
    // Otherwise cast this to be able to access getName()
    baseNameStr = func->getBaseIdentifier().str();
  } else if (isa<ConstructorDecl>(this)) {
    baseNameStr = "init";
  } else {
    llvm_unreachable("Unknown subclass of AbstractFunctionDecl");
  }

  auto argNames = getName().getArgumentNames();

  // Use the preferred name if specified
  if (preferredName) {
    // Return invalid selector if argument count doesn't match.
    if (argNames.size() != preferredName.getArgumentNames().size()) {
      return ObjCSelector();
    }
    baseNameStr = preferredName.getBaseName().userFacingName();
    argNames = preferredName.getArgumentNames();
  }

  auto baseName = ctx.getIdentifier(baseNameStr);

  if (auto accessor = dyn_cast<AccessorDecl>(this)) {
    // For a getter or setter, go through the variable or subscript decl.
    auto asd = accessor->getStorage();
    if (accessor->isGetter())
      return asd->getObjCGetterSelector(baseName);
    if (accessor->isSetter())
      return asd->getObjCSetterSelector(baseName);
  }

  // If this is a zero-parameter initializer with a long selector
  // name, form that selector.
  auto ctor = dyn_cast<ConstructorDecl>(this);
  if (ctor && ctor->isObjCZeroParameterWithLongSelector()) {
    Identifier firstName = argNames[0];
    llvm::SmallString<16> scratch;
    scratch += "init";

    // If the first argument name doesn't start with a preposition, add "with".
    if (!isPreposition(camel_case::getFirstWord(firstName.str()))) {
      camel_case::appendSentenceCase(scratch, "With");
    }

    camel_case::appendSentenceCase(scratch, firstName.str());
    return ObjCSelector(ctx, 0, ctx.getIdentifier(scratch));
  }

  // The number of selector pieces we'll have.
  Optional<ForeignAsyncConvention> asyncConvention
    = getForeignAsyncConvention();
  Optional<ForeignErrorConvention> errorConvention
    = getForeignErrorConvention();
  unsigned numSelectorPieces
    = argNames.size() + (asyncConvention.hasValue() ? 1 : 0)
    + (errorConvention.hasValue() ? 1 : 0);

  // If we have no arguments, it's a nullary selector.
  if (numSelectorPieces == 0) {
    return ObjCSelector(ctx, 0, baseName);
  }

 // If it's a unary selector with no name for the first argument, we're done.
  if (numSelectorPieces == 1 && argNames.size() == 1 && argNames[0].empty()) {
    return ObjCSelector(ctx, 1, baseName);
  }

  /// Collect the selector pieces.
  SmallVector<Identifier, 4> selectorPieces;
  selectorPieces.reserve(numSelectorPieces);
  bool didStringManipulation = false;
  unsigned argIndex = 0;
  for (unsigned piece = 0; piece != numSelectorPieces; ++piece) {
    if (piece > 0) {
      // If we have an async convention that inserts a completion handler
      // parameter here, add "completionHandler".
      if (asyncConvention &&
          piece == asyncConvention->completionHandlerParamIndex()) {
        selectorPieces.push_back(ctx.getIdentifier("completionHandler"));
        continue;
      }

      // If we have an error convention that inserts an error parameter
      // here, add "error".
      if (errorConvention &&
          piece == errorConvention->getErrorParameterIndex()) {
        selectorPieces.push_back(ctx.Id_error);
        continue;
      }

      // Selector pieces beyond the first are simple.
      selectorPieces.push_back(argNames[argIndex++]);
      continue;
    }

    // For the first selector piece, attach either the first parameter,
    // "withCompletionHandler", or "AndReturnError" to the base name,
    // if appropriate.
    auto firstPiece = baseName;
    llvm::SmallString<32> scratch;
    scratch += firstPiece.str();
    if (asyncConvention &&
        piece == asyncConvention->completionHandlerParamIndex()) {
      // The completion handler is first; append "WithCompletionHandler".
      camel_case::appendSentenceCase(scratch, "WithCompletionHandler");

      firstPiece = ctx.getIdentifier(scratch);
      didStringManipulation = true;
    } else if (errorConvention &&
               piece == errorConvention->getErrorParameterIndex()) {
      // The error is first; append "AndReturnError".
      camel_case::appendSentenceCase(scratch, "AndReturnError");

      firstPiece = ctx.getIdentifier(scratch);
      didStringManipulation = true;
    } else if (!argNames[argIndex].empty()) {
      // If the first argument name doesn't start with a preposition, and the
      // method name doesn't end with a preposition, add "with".
      auto firstName = argNames[argIndex++];
      if (!isPreposition(camel_case::getFirstWord(firstName.str())) &&
          !isPreposition(camel_case::getLastWord(firstPiece.str()))) {
        camel_case::appendSentenceCase(scratch, "With");
      }

      camel_case::appendSentenceCase(scratch, firstName.str());
      firstPiece = ctx.getIdentifier(scratch);
      didStringManipulation = true;
    } else {
      ++argIndex;
    }

    selectorPieces.push_back(firstPiece);
  }
  assert(argIndex == argNames.size());

  // Form the result.
  auto result = ObjCSelector(ctx, selectorPieces.size(), selectorPieces);

  // If we did any string manipulation, cache the result. We don't want to
  // do that again.
  if (didStringManipulation && objc && !preferredName)
    const_cast<ObjCAttr *>(objc)->setName(result, /*implicit=*/true);

  return result;
}

bool AbstractFunctionDecl::isObjCInstanceMethod() const {
  return isInstanceMember() || isa<ConstructorDecl>(this);
}

bool AbstractFunctionDecl::needsNewVTableEntry() const {
  auto &ctx = getASTContext();
  return evaluateOrDefault(
      ctx.evaluator,
      NeedsNewVTableEntryRequest{const_cast<AbstractFunctionDecl *>(this)},
      false);
}

ParamDecl *AbstractFunctionDecl::getImplicitSelfDecl(bool createIfNeeded) {
  auto **selfDecl = getImplicitSelfDeclStorage();

  // If this is not a method, return nullptr.
  if (selfDecl == nullptr)
    return nullptr;

  // If we've already created a 'self' parameter, just return it.
  if (*selfDecl != nullptr)
    return *selfDecl;

  // If we're not allowed to create one, return nullptr.
  if (!createIfNeeded)
    return nullptr;

  // Create and save our 'self' parameter.
  auto &ctx = getASTContext();
  *selfDecl = new (ctx) ParamDecl(SourceLoc(), SourceLoc(), Identifier(),
                                  getLoc(), ctx.Id_self, this);
  (*selfDecl)->setImplicit();

  return *selfDecl;
}

void AbstractFunctionDecl::setParameters(ParameterList *BodyParams) {
#ifndef NDEBUG
  const auto Name = getName();
  if (!isa<DestructorDecl>(this))
    assert((!Name || !Name.isSimpleName()) && "Must have a compound name");
  assert(!Name || (Name.getArgumentNames().size() == BodyParams->size()));
#endif

  Params = BodyParams;
  BodyParams->setDeclContextOfParamDecls(this);
}

OpaqueTypeDecl::OpaqueTypeDecl(ValueDecl *NamingDecl,
                               GenericParamList *GenericParams, DeclContext *DC,
                               GenericSignature OpaqueInterfaceGenericSignature,
                               OpaqueReturnTypeRepr *UnderlyingInterfaceRepr,
                               GenericTypeParamType *UnderlyingInterfaceType)
    : GenericTypeDecl(DeclKind::OpaqueType, DC, Identifier(), SourceLoc(), {},
                      GenericParams),
      NamingDecl(NamingDecl),
      OpaqueInterfaceGenericSignature(OpaqueInterfaceGenericSignature),
      UnderlyingInterfaceRepr(UnderlyingInterfaceRepr),
      UnderlyingInterfaceType(UnderlyingInterfaceType) {
  // Always implicit.
  setImplicit();
}

bool OpaqueTypeDecl::isOpaqueReturnTypeOfFunction(
                                       const AbstractFunctionDecl *func) const {
  // Either the function is declared with its own opaque return type...
  if (getNamingDecl() == func)
    return true;

  // ...or the function is a getter for a property or subscript with an
  // opaque return type.
  if (auto accessor = dyn_cast<AccessorDecl>(func)) {
    return accessor->isGetter() && getNamingDecl() == accessor->getStorage();
  }

  return false;
}

unsigned OpaqueTypeDecl::getAnonymousOpaqueParamOrdinal(
    OpaqueReturnTypeRepr *repr) const {
  // TODO [OPAQUE SUPPORT]: we will need to generalize here when we allow
  // multiple "some" types.
  assert(UnderlyingInterfaceRepr &&
         "can't do opaque param lookup without underlying interface repr");
  return repr == UnderlyingInterfaceRepr ? 0 : -1;
}

Identifier OpaqueTypeDecl::getOpaqueReturnTypeIdentifier() const {
  assert(getNamingDecl() && "not an opaque return type");
  if (!OpaqueReturnTypeIdentifier.empty())
    return OpaqueReturnTypeIdentifier;
  
  SmallString<64> mangleBuf;
  {
    llvm::raw_svector_ostream os(mangleBuf);
    Mangle::ASTMangler mangler;
    os << mangler.mangleOpaqueTypeDecl(this);
  }

  OpaqueReturnTypeIdentifier = getASTContext().getIdentifier(mangleBuf);
  return OpaqueReturnTypeIdentifier;
}

bool AbstractFunctionDecl::hasInlinableBodyText() const {
  switch (getBodyKind()) {
  case BodyKind::Deserialized:
    return true;

  case BodyKind::Unparsed:
  case BodyKind::Parsed:
  case BodyKind::TypeChecked:
    if (auto body = getBody())
      return !body->isImplicit();
    return false;

  case BodyKind::None:
  case BodyKind::Synthesize:
  case BodyKind::Skipped:
  case BodyKind::SILSynthesize:
    return false;
  }
  llvm_unreachable("covered switch");
}

StringRef AbstractFunctionDecl::getInlinableBodyText(
  SmallVectorImpl<char> &scratch) const {
  assert(hasInlinableBodyText() &&
         "can't get string representation of function with no text");

  if (getBodyKind() == BodyKind::Deserialized)
    return BodyStringRepresentation;

  auto body = getBody();
  return extractInlinableText(getASTContext().SourceMgr, body, scratch);
}

/// A uniqued list of derivative function configurations.
struct AbstractFunctionDecl::DerivativeFunctionConfigurationList
    : public ASTAllocated<DerivativeFunctionConfigurationList>,
      public llvm::SetVector<AutoDiffConfig> {};

void AbstractFunctionDecl::prepareDerivativeFunctionConfigurations() {
  if (DerivativeFunctionConfigs)
    return;
  auto &ctx = getASTContext();
  DerivativeFunctionConfigs = new (ctx) DerivativeFunctionConfigurationList();
  // Register an `ASTContext` cleanup calling the list destructor.
  ctx.addCleanup([this]() {
    this->DerivativeFunctionConfigs->~DerivativeFunctionConfigurationList();
  });
}

ArrayRef<AutoDiffConfig>
AbstractFunctionDecl::getDerivativeFunctionConfigurations() {
  prepareDerivativeFunctionConfigurations();

  // Resolve derivative function configurations from `@differentiable`
  // attributes by type-checking them.
  for (auto *diffAttr : getAttrs().getAttributes<DifferentiableAttr>())
    (void)diffAttr->getParameterIndices();
  // For accessors: resolve derivative function configurations from storage
  // `@differentiable` attributes by type-checking them.
  if (auto *accessor = dyn_cast<AccessorDecl>(this)) {
    auto *storage = accessor->getStorage();
    for (auto *diffAttr : storage->getAttrs().getAttributes<DifferentiableAttr>())
      (void)diffAttr->getParameterIndices();
  }

  // Load derivative configurations from imported modules.
  auto &ctx = getASTContext();
  if (ctx.getCurrentGeneration() > DerivativeFunctionConfigGeneration) {
    unsigned previousGeneration = DerivativeFunctionConfigGeneration;
    DerivativeFunctionConfigGeneration = ctx.getCurrentGeneration();
    ctx.loadDerivativeFunctionConfigurations(this, previousGeneration,
                                             *DerivativeFunctionConfigs);
  }
  return DerivativeFunctionConfigs->getArrayRef();
}

void AbstractFunctionDecl::addDerivativeFunctionConfiguration(
    const AutoDiffConfig &config) {
  prepareDerivativeFunctionConfigurations();
  DerivativeFunctionConfigs->insert(config);
}

void FuncDecl::setResultInterfaceType(Type type) {
  getASTContext().evaluator.cacheOutput(ResultTypeRequest{this},
                                        std::move(type));
}

FuncDecl *FuncDecl::createImpl(ASTContext &Context,
                               SourceLoc StaticLoc,
                               StaticSpellingKind StaticSpelling,
                               SourceLoc FuncLoc,
                               DeclName Name, SourceLoc NameLoc,
                               bool Async, SourceLoc AsyncLoc,
                               bool Throws, SourceLoc ThrowsLoc,
                               GenericParamList *GenericParams,
                               DeclContext *Parent,
                               ClangNode ClangN) {
  bool HasImplicitSelfDecl = Parent->isTypeContext();
  size_t Size = sizeof(FuncDecl) + (HasImplicitSelfDecl
                                    ? sizeof(ParamDecl *)
                                    : 0);
  void *DeclPtr = allocateMemoryForDecl<FuncDecl>(Context, Size,
                                                  !ClangN.isNull());
  auto D = ::new (DeclPtr)
      FuncDecl(DeclKind::Func, StaticLoc, StaticSpelling, FuncLoc,
               Name, NameLoc, Async, AsyncLoc, Throws, ThrowsLoc,
               HasImplicitSelfDecl, GenericParams, Parent);
  if (ClangN)
    D->setClangNode(ClangN);
  if (HasImplicitSelfDecl)
    *D->getImplicitSelfDeclStorage() = nullptr;

  return D;
}

FuncDecl *FuncDecl::createDeserialized(ASTContext &Context,
                                       StaticSpellingKind StaticSpelling,
                                       DeclName Name, bool Async, bool Throws,
                                       GenericParamList *GenericParams,
                                       Type FnRetType, DeclContext *Parent) {
  assert(FnRetType && "Deserialized result type must not be null");
  auto *const FD =
      FuncDecl::createImpl(Context, SourceLoc(), StaticSpelling, SourceLoc(),
                           Name, SourceLoc(), Async, SourceLoc(), Throws,
                           SourceLoc(), GenericParams, Parent, ClangNode());
  FD->setResultInterfaceType(FnRetType);
  return FD;
}

FuncDecl *FuncDecl::create(ASTContext &Context, SourceLoc StaticLoc,
                           StaticSpellingKind StaticSpelling, SourceLoc FuncLoc,
                           DeclName Name, SourceLoc NameLoc, bool Async,
                           SourceLoc AsyncLoc, bool Throws, SourceLoc ThrowsLoc,
                           GenericParamList *GenericParams,
                           ParameterList *BodyParams, TypeRepr *ResultTyR,
                           DeclContext *Parent) {
  auto *const FD = FuncDecl::createImpl(
      Context, StaticLoc, StaticSpelling, FuncLoc, Name, NameLoc, Async,
      AsyncLoc, Throws, ThrowsLoc, GenericParams, Parent, ClangNode());
  FD->setParameters(BodyParams);
  FD->FnRetType = TypeLoc(ResultTyR);
  return FD;
}

FuncDecl *FuncDecl::createImplicit(ASTContext &Context,
                                   StaticSpellingKind StaticSpelling,
                                   DeclName Name, SourceLoc NameLoc, bool Async,
                                   bool Throws, GenericParamList *GenericParams,
                                   ParameterList *BodyParams, Type FnRetType,
                                   DeclContext *Parent) {
  assert(FnRetType);
  auto *const FD = FuncDecl::createImpl(
      Context, SourceLoc(), StaticSpelling, SourceLoc(), Name, NameLoc, Async,
      SourceLoc(), Throws, SourceLoc(), GenericParams, Parent, ClangNode());
  FD->setImplicit();
  FD->setParameters(BodyParams);
  FD->setResultInterfaceType(FnRetType);
  return FD;
}

FuncDecl *FuncDecl::createImported(ASTContext &Context, SourceLoc FuncLoc,
                                   DeclName Name, SourceLoc NameLoc, bool Async,
                                   bool Throws, ParameterList *BodyParams,
                                   Type FnRetType,
                                   GenericParamList *GenericParams,
                                   DeclContext *Parent, ClangNode ClangN) {
  assert(ClangN);
  auto *const FD = FuncDecl::createImpl(
      Context, SourceLoc(), StaticSpellingKind::None, FuncLoc, Name, NameLoc,
      Async, SourceLoc(), Throws, SourceLoc(), GenericParams, Parent, ClangN);
  FD->setParameters(BodyParams);
  FD->setResultInterfaceType(FnRetType);
  return FD;
}

OperatorDecl *FuncDecl::getOperatorDecl() const {
  // Fast-path: Most functions are not operators.
  if (!isOperator()) {
    return nullptr;
  }
  return evaluateOrDefault(getASTContext().evaluator,
                           FunctionOperatorRequest{
                             const_cast<FuncDecl *>(this)
                           },
                           nullptr);
}

bool FuncDecl::isStatic() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    IsStaticRequest{const_cast<FuncDecl *>(this)},
    false);
}

AccessorDecl *AccessorDecl::createImpl(ASTContext &ctx,
                                       SourceLoc declLoc,
                                       SourceLoc accessorKeywordLoc,
                                       AccessorKind accessorKind,
                                       AbstractStorageDecl *storage,
                                       SourceLoc staticLoc,
                                       StaticSpellingKind staticSpelling,
                                       bool async, SourceLoc asyncLoc,
                                       bool throws, SourceLoc throwsLoc,
                                       GenericParamList *genericParams,
                                       DeclContext *parent,
                                       ClangNode clangNode) {
  bool hasImplicitSelfDecl = parent->isTypeContext();
  size_t size = sizeof(AccessorDecl) + (hasImplicitSelfDecl
                                        ? sizeof(ParamDecl *)
                                        : 0);
  void *buffer = allocateMemoryForDecl<AccessorDecl>(ctx, size,
                                                     !clangNode.isNull());
  auto D = ::new (buffer)
      AccessorDecl(declLoc, accessorKeywordLoc, accessorKind,
                   storage, staticLoc, staticSpelling, async, asyncLoc, throws, throwsLoc,
                   hasImplicitSelfDecl, genericParams, parent);
  if (clangNode)
    D->setClangNode(clangNode);
  if (hasImplicitSelfDecl)
    *D->getImplicitSelfDeclStorage() = nullptr;

  return D;
}

AccessorDecl *
AccessorDecl::createDeserialized(ASTContext &ctx, AccessorKind accessorKind,
                                 AbstractStorageDecl *storage,
                                 StaticSpellingKind staticSpelling,
                                 bool async, bool throws, GenericParamList *genericParams,
                                 Type fnRetType, DeclContext *parent) {
  assert(fnRetType && "Deserialized result type must not be null");
  auto *const D = AccessorDecl::createImpl(
      ctx, SourceLoc(), SourceLoc(), accessorKind, storage, SourceLoc(),
      staticSpelling, async, SourceLoc(), throws, SourceLoc(), genericParams, parent, ClangNode());
  D->setResultInterfaceType(fnRetType);
  return D;
}

AccessorDecl *AccessorDecl::create(ASTContext &ctx,
                                   SourceLoc declLoc,
                                   SourceLoc accessorKeywordLoc,
                                   AccessorKind accessorKind,
                                   AbstractStorageDecl *storage,
                                   SourceLoc staticLoc,
                                   StaticSpellingKind staticSpelling,
                                   bool async, SourceLoc asyncLoc,
                                   bool throws, SourceLoc throwsLoc,
                                   GenericParamList *genericParams,
                                   ParameterList * bodyParams,
                                   Type fnRetType,
                                   DeclContext *parent,
                                   ClangNode clangNode) {
  auto *D = AccessorDecl::createImpl(
      ctx, declLoc, accessorKeywordLoc, accessorKind, storage,
      staticLoc, staticSpelling, async, asyncLoc, throws, throwsLoc,
      genericParams, parent, clangNode);
  D->setParameters(bodyParams);
  D->setResultInterfaceType(fnRetType);
  return D;
}

bool AccessorDecl::isAssumedNonMutating() const {
  switch (getAccessorKind()) {
  case AccessorKind::Get:
  case AccessorKind::Address:
  case AccessorKind::Read:
    return true;

  case AccessorKind::Set:
  case AccessorKind::WillSet:
  case AccessorKind::DidSet:
  case AccessorKind::MutableAddress:
  case AccessorKind::Modify:
    return false;
  }
  llvm_unreachable("bad accessor kind");
}

bool AccessorDecl::isExplicitNonMutating() const {
  return !isMutating() &&
    !isAssumedNonMutating() &&
    isInstanceMember() &&
    !getDeclContext()->getDeclaredInterfaceType()->hasReferenceSemantics();
}

bool AccessorDecl::isSimpleDidSet() const {
  auto mutableThis = const_cast<AccessorDecl *>(this);
  return evaluateOrDefault(getASTContext().evaluator,
                           SimpleDidSetRequest{mutableThis}, false);
}

void AccessorDecl::printUserFacingName(raw_ostream &out) const {
  switch (getAccessorKind()) {
  case AccessorKind::Get:
    out << "getter:";
    break;
  case AccessorKind::Set:
    out << "setter:";
    break;
  default:
    out << getName();
    return;
  }

  out << getStorage()->getName() << "(";
  if (this->isSetter()) {
    for (const auto *param : *getParameters()) {
      out << param->getName() << ":";
    }
  }
  out << ")";
}

StaticSpellingKind FuncDecl::getCorrectStaticSpelling() const {
  assert(getDeclContext()->isTypeContext());
  if (!isStatic())
    return StaticSpellingKind::None;
  if (getStaticSpelling() != StaticSpellingKind::None)
    return getStaticSpelling();

  return getCorrectStaticSpellingForDecl(this);
}

Type FuncDecl::getResultInterfaceType() const {
  auto &ctx = getASTContext();
  auto mutableThis = const_cast<FuncDecl *>(this);
  if (auto type = evaluateOrDefault(ctx.evaluator,
                           ResultTypeRequest{mutableThis},
                           Type()))
    return type;
  return ErrorType::get(ctx);
}

bool FuncDecl::isUnaryOperator() const {
  if (!isOperator())
    return false;
  
  auto *params = getParameters();
  return params->size() == 1 && !params->get(0)->isVariadic();
}

bool FuncDecl::isBinaryOperator() const {
  if (!isOperator())
    return false;
  
  auto *params = getParameters();
  return params->size() == 2 &&
    !params->get(0)->isVariadic() &&
    !params->get(1)->isVariadic();
}

SelfAccessKind FuncDecl::getSelfAccessKind() const {
  auto &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
                           SelfAccessKindRequest{const_cast<FuncDecl *>(this)},
                           SelfAccessKind::NonMutating);
}

bool FuncDecl::isCallAsFunctionMethod() const {
  return getBaseIdentifier() == getASTContext().Id_callAsFunction &&
         isInstanceMember();
}

bool FuncDecl::isMainTypeMainMethod() const {
  return (getBaseIdentifier() == getASTContext().Id_main) &&
         !isInstanceMember() && getResultInterfaceType()->isVoid() &&
         getParameters()->size() == 0;
}

ConstructorDecl::ConstructorDecl(DeclName Name, SourceLoc ConstructorLoc,
                                 bool Failable, SourceLoc FailabilityLoc,
                                 bool Async, SourceLoc AsyncLoc,
                                 bool Throws, SourceLoc ThrowsLoc,
                                 ParameterList *BodyParams,
                                 GenericParamList *GenericParams,
                                 DeclContext *Parent)
  : AbstractFunctionDecl(DeclKind::Constructor, Parent, Name, ConstructorLoc,
                         Async, AsyncLoc, Throws, ThrowsLoc,
                         /*HasImplicitSelfDecl=*/true,
                         GenericParams),
    FailabilityLoc(FailabilityLoc),
    SelfDecl(nullptr)
{
  if (BodyParams)
    setParameters(BodyParams);
  
  Bits.ConstructorDecl.HasStubImplementation = 0;
  Bits.ConstructorDecl.Failable = Failable;

  assert(Name.getBaseName() == DeclBaseName::createConstructor());
}

ConstructorDecl *ConstructorDecl::createImported(
    ASTContext &ctx, ClangNode clangNode, DeclName name,
    SourceLoc constructorLoc, bool failable, SourceLoc failabilityLoc,
    bool async, SourceLoc asyncLoc,
    bool throws, SourceLoc throwsLoc, ParameterList *bodyParams,
    GenericParamList *genericParams, DeclContext *parent) {
  void *declPtr = allocateMemoryForDecl<ConstructorDecl>(
      ctx, sizeof(ConstructorDecl), true);
  auto ctor = ::new (declPtr)
      ConstructorDecl(name, constructorLoc,
                      failable, failabilityLoc, 
                      async, asyncLoc,
                      throws, throwsLoc, 
                      bodyParams, genericParams, parent);
  ctor->setClangNode(clangNode);
  return ctor;
}

bool ConstructorDecl::isObjCZeroParameterWithLongSelector() const {
  // The initializer must have a single, non-empty argument name.
  if (getName().getArgumentNames().size() != 1 ||
      getName().getArgumentNames()[0].empty())
    return false;

  auto *params = getParameters();
  if (params->size() != 1)
    return false;

  return params->get(0)->getInterfaceType()->isVoid();
}


DestructorDecl::DestructorDecl(SourceLoc DestructorLoc, DeclContext *Parent)
  : AbstractFunctionDecl(DeclKind::Destructor, Parent,
                         DeclBaseName::createDestructor(), DestructorLoc,
                         /*Async=*/false, /*AsyncLoc=*/SourceLoc(),
                         /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                         /*HasImplicitSelfDecl=*/true,
                         /*GenericParams=*/nullptr),
    SelfDecl(nullptr) {
  setParameters(ParameterList::createEmpty(Parent->getASTContext()));
}

ObjCSelector DestructorDecl::getObjCSelector() const {
  // Deinitializers are always called "dealloc".
  auto &ctx = getASTContext();
  return ObjCSelector(ctx, 0, ctx.Id_dealloc);
}

SourceRange FuncDecl::getSourceRange() const {
  SourceLoc StartLoc = getStartLoc();

  if (StartLoc.isInvalid())
    return SourceRange();

  if (getBodyKind() == BodyKind::Unparsed ||
      getBodyKind() == BodyKind::Skipped)
    return { StartLoc, BodyRange.End };

  SourceLoc RBraceLoc = getOriginalBodySourceRange().End;
  if (RBraceLoc.isValid()) {
    return { StartLoc, RBraceLoc };
  }

  if (isa<AccessorDecl>(this))
    return StartLoc;

  if (getBodyKind() == BodyKind::Synthesize)
    return SourceRange();

  auto TrailingWhereClauseSourceRange = getGenericTrailingWhereClauseSourceRange();
  if (TrailingWhereClauseSourceRange.isValid())
    return { StartLoc, TrailingWhereClauseSourceRange.End };

  const auto ResultTyEndLoc = getResultTypeSourceRange().End;
  if (ResultTyEndLoc.isValid())
    return { StartLoc, ResultTyEndLoc };

  if (hasThrows())
    return { StartLoc, getThrowsLoc() };

  if (hasAsync())
    return { StartLoc, getAsyncLoc() };

  auto LastParamListEndLoc = getParameters()->getSourceRange().End;
  if (LastParamListEndLoc.isValid())
    return { StartLoc, LastParamListEndLoc };
  return StartLoc;
}

EnumElementDecl::EnumElementDecl(SourceLoc IdentifierLoc, DeclName Name,
                                 ParameterList *Params,
                                 SourceLoc EqualsLoc,
                                 LiteralExpr *RawValueExpr,
                                 DeclContext *DC)
  : DeclContext(DeclContextKind::EnumElementDecl, DC),
    ValueDecl(DeclKind::EnumElement, DC, Name, IdentifierLoc),
    EqualsLoc(EqualsLoc),
    RawValueExpr(RawValueExpr) {
  setParameterList(Params);
}

SourceRange EnumElementDecl::getSourceRange() const {
  if (RawValueExpr && !RawValueExpr->isImplicit())
    return {getStartLoc(), RawValueExpr->getEndLoc()};
  if (auto *PL = getParameterList())
    return {getStartLoc(), PL->getSourceRange().End};
  return {getStartLoc(), getNameLoc()};
}

Type EnumElementDecl::getArgumentInterfaceType() const {
  if (!hasAssociatedValues())
    return nullptr;

  auto interfaceType = getInterfaceType();
  if (interfaceType->is<ErrorType>()) {
    return interfaceType;
  }

  auto funcTy = interfaceType->castTo<AnyFunctionType>();
  funcTy = funcTy->getResult()->castTo<FunctionType>();

  auto &ctx = getASTContext();
  SmallVector<TupleTypeElt, 4> elements;
  for (const auto &param : funcTy->getParams()) {
    Type eltType = param.getParameterType(/*canonicalVararg=*/false, &ctx);
    elements.emplace_back(eltType, param.getLabel());
  }
  return TupleType::get(elements, ctx);
}

void EnumElementDecl::setParameterList(ParameterList *params) {
  Params = params;

  if (params)
    params->setDeclContextOfParamDecls(this);
}

EnumCaseDecl *EnumElementDecl::getParentCase() const {
  for (EnumCaseDecl *EC : getParentEnum()->getAllCases()) {
    ArrayRef<EnumElementDecl *> CaseElements = EC->getElements();
    if (std::find(CaseElements.begin(), CaseElements.end(), this) !=
        CaseElements.end()) {
      return EC;
    }
  }

  llvm_unreachable("enum element not in case of parent enum");
}
      
LiteralExpr *EnumElementDecl::getRawValueExpr() const {
  // The return value of this request is irrelevant - it exists as
  // a cache-warmer.
  (void)evaluateOrDefault(
      getASTContext().evaluator,
      EnumRawValuesRequest{getParentEnum(), TypeResolutionStage::Interface},
      {});
  return RawValueExpr;
}

LiteralExpr *EnumElementDecl::getStructuralRawValueExpr() const {
  // The return value of this request is irrelevant - it exists as
  // a cache-warmer.
  (void)evaluateOrDefault(
      getASTContext().evaluator,
      EnumRawValuesRequest{getParentEnum(), TypeResolutionStage::Structural},
      {});
  return RawValueExpr;
}

void EnumElementDecl::setRawValueExpr(LiteralExpr *e) {
  assert((!RawValueExpr || e == RawValueExpr || e->getType()) &&
         "Illegal mutation of raw value expr");
  RawValueExpr = e;
}

SourceRange ConstructorDecl::getSourceRange() const {
  if (isImplicit())
    return getConstructorLoc();

  SourceLoc End = getOriginalBodySourceRange().End;
  if (End.isInvalid())
    End = getGenericTrailingWhereClauseSourceRange().End;
  if (End.isInvalid())
    End = getThrowsLoc();
  if (End.isInvalid())
    End = getSignatureSourceRange().End;

  return { getConstructorLoc(), End };
}

Type ConstructorDecl::getResultInterfaceType() const {
  Type resultTy;

  auto *dc = getDeclContext();
  if (!dc->isTypeContext())
    resultTy = ErrorType::get(getASTContext());
  else
    resultTy = dc->getSelfInterfaceType();

  // Adjust result type for failability.
  if (isFailable())
    return OptionalType::get(resultTy);

  return resultTy;
}

Type ConstructorDecl::getInitializerInterfaceType() {
  if (InitializerInterfaceType)
    return InitializerInterfaceType;

  // Lazily calculate initializer type.
  auto allocatorTy = getInterfaceType();
  if (!allocatorTy->is<AnyFunctionType>()) {
    InitializerInterfaceType = ErrorType::get(getASTContext());
    return InitializerInterfaceType;
  }

  auto funcTy = allocatorTy->castTo<AnyFunctionType>()->getResult();
  assert(funcTy->is<FunctionType>());

  // Constructors have an initializer type that takes an instance
  // instead of a metatype.
  auto initSelfParam = computeSelfParam(this, /*isInitializingCtor=*/true);

  // FIXME: Verify ExtInfo state is correct, not working by accident.
  Type initFuncTy;
  if (auto sig = getGenericSignature()) {
    GenericFunctionType::ExtInfo info;
    initFuncTy = GenericFunctionType::get(sig, {initSelfParam}, funcTy, info);
  } else {
    FunctionType::ExtInfo info;
    initFuncTy = FunctionType::get({initSelfParam}, funcTy, info);
  }
  InitializerInterfaceType = initFuncTy;

  return InitializerInterfaceType;
}

CtorInitializerKind ConstructorDecl::getInitKind() const {
  return evaluateOrDefault(getASTContext().evaluator,
    InitKindRequest{const_cast<ConstructorDecl *>(this)},
    CtorInitializerKind::Designated);
}

BodyInitKindAndExpr
ConstructorDecl::getDelegatingOrChainedInitKind() const {
  return evaluateOrDefault(getASTContext().evaluator,
    BodyInitKindRequest{const_cast<ConstructorDecl *>(this)},
    BodyInitKindAndExpr());
  assert(hasBody() && "Constructor does not have a definition");
}

void ConstructorDecl::clearCachedDelegatingOrChainedInitKind() {
  getASTContext().evaluator.clearCachedOutput(
    BodyInitKindRequest{const_cast<ConstructorDecl *>(this)});
}

SourceRange DestructorDecl::getSourceRange() const {
  SourceLoc End = getOriginalBodySourceRange().End;
  if (End.isInvalid()) {
    End = getDestructorLoc();
  }

  return { getDestructorLoc(), End };
}

StringRef swift::getAssociativitySpelling(Associativity value) {
  switch (value) {
  case Associativity::None: return "none";
  case Associativity::Left: return "left";
  case Associativity::Right: return "right";
  }
  llvm_unreachable("Unhandled Associativity in switch.");
}

PrecedenceGroupDecl *
PrecedenceGroupDecl::create(DeclContext *dc,
                            SourceLoc precedenceGroupLoc,
                            SourceLoc nameLoc,
                            Identifier name,
                            SourceLoc lbraceLoc,
                            SourceLoc associativityKeywordLoc,
                            SourceLoc associativityValueLoc,
                            Associativity associativity,
                            SourceLoc assignmentKeywordLoc,
                            SourceLoc assignmentValueLoc,
                            bool isAssignment,
                            SourceLoc higherThanLoc,
                            ArrayRef<Relation> higherThan,
                            SourceLoc lowerThanLoc,
                            ArrayRef<Relation> lowerThan,
                            SourceLoc rbraceLoc) {
  void *memory = dc->getASTContext().Allocate(sizeof(PrecedenceGroupDecl) +
                    (higherThan.size() + lowerThan.size()) * sizeof(Relation),
                                              alignof(PrecedenceGroupDecl));
  return new (memory) PrecedenceGroupDecl(dc, precedenceGroupLoc, nameLoc, name,
                                          lbraceLoc, associativityKeywordLoc,
                                          associativityValueLoc, associativity,
                                          assignmentKeywordLoc,
                                          assignmentValueLoc, isAssignment,
                                          higherThanLoc, higherThan,
                                          lowerThanLoc, lowerThan, rbraceLoc);
}

PrecedenceGroupDecl::PrecedenceGroupDecl(DeclContext *dc,
                                         SourceLoc precedenceGroupLoc,
                                         SourceLoc nameLoc,
                                         Identifier name,
                                         SourceLoc lbraceLoc,
                                         SourceLoc associativityKeywordLoc,
                                         SourceLoc associativityValueLoc,
                                         Associativity associativity,
                                         SourceLoc assignmentKeywordLoc,
                                         SourceLoc assignmentValueLoc,
                                         bool isAssignment,
                                         SourceLoc higherThanLoc,
                                         ArrayRef<Relation> higherThan,
                                         SourceLoc lowerThanLoc,
                                         ArrayRef<Relation> lowerThan,
                                         SourceLoc rbraceLoc)
  : Decl(DeclKind::PrecedenceGroup, dc),
    PrecedenceGroupLoc(precedenceGroupLoc), NameLoc(nameLoc),
    LBraceLoc(lbraceLoc), RBraceLoc(rbraceLoc),
    AssociativityKeywordLoc(associativityKeywordLoc),
    AssociativityValueLoc(associativityValueLoc),
    AssignmentKeywordLoc(assignmentKeywordLoc),
    AssignmentValueLoc(assignmentValueLoc),
    HigherThanLoc(higherThanLoc), LowerThanLoc(lowerThanLoc), Name(name),
    NumHigherThan(higherThan.size()), NumLowerThan(lowerThan.size()) {
  Bits.PrecedenceGroupDecl.Associativity = unsigned(associativity);
  Bits.PrecedenceGroupDecl.IsAssignment = isAssignment;
  memcpy(getHigherThanBuffer(), higherThan.data(),
         higherThan.size() * sizeof(Relation));
  memcpy(getLowerThanBuffer(), lowerThan.data(),
         lowerThan.size() * sizeof(Relation));
}

PrecedenceGroupDecl *InfixOperatorDecl::getPrecedenceGroup() const {
  return evaluateOrDefault(
      getASTContext().evaluator,
      OperatorPrecedenceGroupRequest{const_cast<InfixOperatorDecl *>(this)},
      nullptr);
}

bool FuncDecl::isDeferBody() const {
  return getBaseIdentifier() == getASTContext().getIdentifier("$defer");
}

bool FuncDecl::isPotentialIBActionTarget() const {
  return isInstanceMember() &&
    getDeclContext()->getSelfClassDecl() &&
    !isa<AccessorDecl>(this);
}

void FuncDecl::setHasTopLevelLocalContextCaptures(bool hasCaptures) {
  assert(!hasCaptures || isa<SourceFile>(getDeclContext()));
  
  Bits.FuncDecl.HasTopLevelLocalContextCaptures = hasCaptures;
}

Type TypeBase::getSwiftNewtypeUnderlyingType() {
  auto structDecl = getStructOrBoundGenericStruct();
  if (!structDecl)
    return {};

  // Make sure the clang node has swift_newtype attribute
  auto clangNode = structDecl->getClangDecl();
  if (!clangNode || !clangNode->hasAttr<clang::SwiftNewTypeAttr>())
    return {};

  // Underlying type is the type of rawValue
  for (auto member : structDecl->getMembers())
    if (auto varDecl = dyn_cast<VarDecl>(member))
      if (varDecl->getName() == getASTContext().Id_rawValue)
        return varDecl->getType();

  return {};
}

const VarDecl *ClassDecl::getUnownedExecutorProperty() const {
  auto &ctx = getASTContext();

  auto hasUnownedSerialExecutorType = [&](VarDecl *property) {
    if (auto type = property->getInterfaceType())
      if (auto td = type->getAnyNominal())
        if (td == ctx.getUnownedSerialExecutorDecl())
          return true;
    return false;
  };

  VarDecl *candidate = nullptr;
  for (auto member: getMembers()) {
    // Instance properties called unownedExecutor.
    if (auto property = dyn_cast<VarDecl>(member)) {
      if (property->getName() == ctx.Id_unownedExecutor &&
          !property->isStatic()) {
        if (!candidate) {
          candidate = property;
          continue;
        }

        bool oldHasRightType = hasUnownedSerialExecutorType(candidate);
        if (oldHasRightType == hasUnownedSerialExecutorType(property)) {
          // just ignore the new property, we should diagnose this eventually
        } else if (!oldHasRightType) {
          candidate = property;
        }
      }
    }
  }

  return candidate;
}

bool ClassDecl::isRootDefaultActor() const {
  return isRootDefaultActor(getModuleContext(), ResilienceExpansion::Maximal);
}

bool ClassDecl::isRootDefaultActor(ModuleDecl *M,
                                   ResilienceExpansion expansion) const {
  if (!isDefaultActor(M, expansion)) return false;
  auto superclass = getSuperclassDecl();
  return (!superclass || superclass->isNSObject());
}

bool ClassDecl::isNativeNSObjectSubclass() const {
  // @objc actors implicitly inherit from NSObject.
  if (isActor()) {
    if (getAttrs().hasAttribute<ObjCAttr>()) {
      return true;
    }
    ClassDecl *superclass = getSuperclassDecl();
    return superclass && superclass->isNSObject();
  }

  // For now, non-actor classes cannot use the native NSObject subclass.
  // Eventually we should roll this out to more classes that directly
  // inherit NSObject, but we have to do it with ABI compatibility.
  return false;
}

bool ClassDecl::isNSObject() const {
  if (!getName().is("NSObject")) return false;
  ASTContext &ctx = getASTContext();
  return (getModuleContext()->getName() == ctx.Id_Foundation ||
          getModuleContext()->getName() == ctx.Id_ObjectiveC ||
          getModuleContext()->getName().is("SwiftFoundation"));
}

Type ClassDecl::getSuperclass() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    SuperclassTypeRequest{const_cast<ClassDecl *>(this),
                          TypeResolutionStage::Interface},
    Type());
}

ClassDecl *ClassDecl::getSuperclassDecl() const {
  ASTContext &ctx = getASTContext();
  return evaluateOrDefault(ctx.evaluator,
    SuperclassDeclRequest{const_cast<ClassDecl *>(this)}, nullptr);
}

void ClassDecl::setSuperclass(Type superclass) {
  assert((!superclass || !superclass->hasArchetype())
         && "superclass must be interface type");
  LazySemanticInfo.SuperclassType.setPointerAndInt(superclass, true);
  LazySemanticInfo.SuperclassDecl.setPointerAndInt(
    superclass ? superclass->getClassOrBoundGenericClass() : nullptr,
    true);
}

bool VarDecl::isSelfParamCaptureIsolated() const {
  assert(isSelfParamCapture());

  // Find the "self" parameter that we captured and determine whether
  // it is potentially isolated.
  for (auto dc = getDeclContext(); dc; dc = dc->getParent()) {
    if (auto func = dyn_cast<AbstractFunctionDecl>(dc)) {
      if (auto selfDecl = func->getImplicitSelfDecl()) {
        return selfDecl->isIsolated();
      }

      if (auto capture = func->getCaptureInfo().getIsolatedParamCapture())
        return capture->isSelfParameter() || capture->isSelfParamCapture();
    }

    if (auto closure = dyn_cast<AbstractClosureExpr>(dc)) {
      switch (auto isolation = closure->getActorIsolation()) {
      case ClosureActorIsolation::Independent:
      case ClosureActorIsolation::GlobalActor:
        return false;

      case ClosureActorIsolation::ActorInstance:
        auto isolatedVar = isolation.getActorInstance();
        return isolatedVar->isSelfParameter() ||
            isolatedVar-isSelfParamCapture();
      }
    }

    if (dc->isModuleScopeContext() || dc->isTypeContext())
      break;
  }

  return false;
}

ActorIsolation swift::getActorIsolation(ValueDecl *value) {
  auto &ctx = value->getASTContext();
  return evaluateOrDefault(
      ctx.evaluator, ActorIsolationRequest{value},
      ActorIsolation::forUnspecified());
}

ActorIsolation swift::getActorIsolationOfContext(DeclContext *dc) {
  if (auto *vd = dyn_cast_or_null<ValueDecl>(dc->getAsDecl()))
    return getActorIsolation(vd);

  // In the context of the initializing or default-value expression of a
  // stored property, the isolation varies between global and type members:
  //   - For a static stored property, the isolation matches the VarDecl.
  //   - For a field of a nominal type, the expression is not isolated.
  // Without this distinction, a nominal can have non-async initializers
  // with various kinds of isolation, so an impossible constraint can be
  // created. See SE-0327 for details.
  if (auto *var = dc->getNonLocalVarDecl()) {

    // Isolation officially changes, as described above, in Swift 6+
    if (dc->getASTContext().isSwiftVersionAtLeast(6) &&
        var->isInstanceMember() &&
        !var->getAttrs().hasAttribute<LazyAttr>()) {
      return ActorIsolation::forUnspecified();
    }

     return getActorIsolation(var);
  }

  if (auto *closure = dyn_cast<AbstractClosureExpr>(dc)) {
    switch (auto isolation = closure->getActorIsolation()) {
    case ClosureActorIsolation::Independent:
      return ActorIsolation::forIndependent();

    case ClosureActorIsolation::GlobalActor: {
      return ActorIsolation::forGlobalActor(
          isolation.getGlobalActor(), /*unsafe=*/false);
    }

    case ClosureActorIsolation::ActorInstance: {
      auto selfDecl = isolation.getActorInstance();
      auto actorClass = selfDecl->getType()->getReferenceStorageReferent()
          ->getClassOrBoundGenericClass();
      // FIXME: Doesn't work properly with generics
      assert(actorClass && "Bad closure actor isolation?");
      return ActorIsolation::forActorInstance(actorClass);
    }
    }
  }

  return ActorIsolation::forUnspecified();
}

ClangNode Decl::getClangNodeImpl() const {
  assert(Bits.Decl.FromClang);
  void * const *ptr = nullptr;
  switch (getKind()) {
#define DECL(Id, Parent) \
  case DeclKind::Id: \
    ptr = reinterpret_cast<void * const*>(static_cast<const Id##Decl*>(this)); \
    break;
#include "swift/AST/DeclNodes.def"
  }
  return ClangNode::getFromOpaqueValue(*(ptr - 1));
}

void Decl::setClangNode(ClangNode Node) {
  Bits.Decl.FromClang = true;
  // The extra/preface memory is allocated by the importer.
  void **ptr = nullptr;
  switch (getKind()) {
#define DECL(Id, Parent) \
  case DeclKind::Id: \
    ptr = reinterpret_cast<void **>(static_cast<Id##Decl*>(this)); \
    break;
#include "swift/AST/DeclNodes.def"
  }
  *(ptr - 1) = Node.getOpaqueValue();
}

// See swift/Basic/Statistic.h for declaration: this enables tracing Decls, is
// defined here to avoid too much layering violation / circular linkage
// dependency.

struct DeclTraceFormatter : public UnifiedStatsReporter::TraceFormatter {
  void traceName(const void *Entity, raw_ostream &OS) const override {
    if (!Entity)
      return;
    const Decl *D = static_cast<const Decl *>(Entity);
    if (auto const *VD = dyn_cast<const ValueDecl>(D)) {
      VD->getName().print(OS, false);
    } else {
      OS << "<"
         << Decl::getDescriptiveKindName(D->getDescriptiveKind())
         << ">";
    }
  }
  void traceLoc(const void *Entity, SourceManager *SM,
                clang::SourceManager *CSM, raw_ostream &OS) const override {
    if (!Entity)
      return;
    const Decl *D = static_cast<const Decl *>(Entity);
    D->getSourceRange().print(OS, *SM, false);
  }
};

static DeclTraceFormatter TF;

template<>
const UnifiedStatsReporter::TraceFormatter*
FrontendStatsTracer::getTraceFormatter<const Decl *>() {
  return &TF;
}

TypeOrExtensionDecl::TypeOrExtensionDecl(NominalTypeDecl *D) : Decl(D) {}
TypeOrExtensionDecl::TypeOrExtensionDecl(ExtensionDecl *D) : Decl(D) {}

Decl *TypeOrExtensionDecl::getAsDecl() const {
  if (auto NTD = Decl.dyn_cast<NominalTypeDecl *>())
    return NTD;

  return Decl.get<ExtensionDecl *>();
}
DeclContext *TypeOrExtensionDecl::getAsDeclContext() const {
  return getAsDecl()->getInnermostDeclContext();
}
NominalTypeDecl *TypeOrExtensionDecl::getBaseNominal() const {
  return getAsDeclContext()->getSelfNominalTypeDecl();
}
bool TypeOrExtensionDecl::isNull() const { return Decl.isNull(); }

void swift::simple_display(llvm::raw_ostream &out, const Decl *decl) {
  if (!decl) {
    out << "(null)";
    return;
  }

  if (auto value = dyn_cast<ValueDecl>(decl)) {
    simple_display(out, value);
  } else if (auto ext = dyn_cast<ExtensionDecl>(decl)) {
    out << "extension of ";
    if (auto typeRepr = ext->getExtendedTypeRepr())
      typeRepr->print(out);
    else
      ext->getSelfNominalTypeDecl()->dumpRef(out);
  } else {
    out << "(unknown decl)";
  }
}

void swift::simple_display(llvm::raw_ostream &out,
                           OptionSet<NominalTypeDecl::LookupDirectFlags> opts) {
  out << "{ ";
  using LookupFlags = NominalTypeDecl::LookupDirectFlags;
  if (opts.contains(LookupFlags::IncludeAttrImplements))
    out << "IncludeAttrImplements";
  out << " }";
}

void swift::simple_display(llvm::raw_ostream &out, const ValueDecl *decl) {
  if (decl) decl->dumpRef(out);
  else out << "(null)";
}

void swift::simple_display(llvm::raw_ostream &out, const GenericParamList *GPL) {
  if (GPL) GPL->print(out);
  else out << "(null)";
}

StringRef swift::getAccessorLabel(AccessorKind kind) {
  switch (kind) {
  #define SINGLETON_ACCESSOR(ID, KEYWORD) \
    case AccessorKind::ID: return #KEYWORD;
  #define ACCESSOR(ID)
  #include "swift/AST/AccessorKinds.def"
    }
    llvm_unreachable("bad accessor kind");
}

void swift::simple_display(llvm::raw_ostream &out, AccessorKind kind) {
  out << getAccessorLabel(kind);
}

SourceLoc swift::extractNearestSourceLoc(const Decl *decl) {
  auto loc = decl->getLoc(/*SerializedOK=*/false);
  if (loc.isValid())
    return loc;

  return extractNearestSourceLoc(decl->getDeclContext());
}

Optional<BraceStmt *>
ParseAbstractFunctionBodyRequest::getCachedResult() const {
  using BodyKind = AbstractFunctionDecl::BodyKind;
  auto afd = std::get<0>(getStorage());
  switch (afd->getBodyKind()) {
  case BodyKind::Deserialized:
  case BodyKind::SILSynthesize:
  case BodyKind::None:
  case BodyKind::Skipped:
    return nullptr;

  case BodyKind::TypeChecked:
  case BodyKind::Parsed:
    return afd->Body;

  case BodyKind::Synthesize:
  case BodyKind::Unparsed:
    return None;
  }
  llvm_unreachable("Unhandled BodyKing in switch");
}

void ParseAbstractFunctionBodyRequest::cacheResult(BraceStmt *value) const {
  using BodyKind = AbstractFunctionDecl::BodyKind;
  auto afd = std::get<0>(getStorage());
  switch (afd->getBodyKind()) {
  case BodyKind::Deserialized:
  case BodyKind::SILSynthesize:
  case BodyKind::None:
  case BodyKind::Skipped:
    // The body is always empty, so don't cache anything.
    assert(value == nullptr);
    return;

  case BodyKind::Parsed:
  case BodyKind::TypeChecked:
    afd->Body = value;
    return;

  case BodyKind::Synthesize:
  case BodyKind::Unparsed:
    llvm_unreachable("evaluate() did not set the body kind");
    return;
  }

}

void swift::simple_display(llvm::raw_ostream &out, AnyFunctionRef fn) {
  if (auto func = fn.getAbstractFunctionDecl())
    simple_display(out, func);
  else
    out << "closure";
}
