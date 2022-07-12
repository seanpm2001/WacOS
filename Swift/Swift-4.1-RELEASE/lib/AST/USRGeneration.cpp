//===--- USRGeneration.cpp - Routines for USR generation ------------------===//
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

#include "swift/AST/ASTContext.h"
#include "swift/AST/Module.h"
#include "swift/AST/USRGeneration.h"
#include "swift/AST/ASTMangler.h"
#include "swift/AST/SwiftNameTranslation.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"

using namespace swift;
using namespace ide;

static inline StringRef getUSRSpacePrefix() {
  return "s:";
}

bool ide::printTypeUSR(Type Ty, raw_ostream &OS) {
  assert(!Ty->hasArchetype() && "cannot have contextless archetypes mangled.");
  Mangle::ASTMangler Mangler;
  OS << Mangler.mangleTypeForDebugger(Ty->getRValueType(), nullptr, nullptr);
  return false;
}

bool ide::printDeclTypeUSR(const ValueDecl *D, raw_ostream &OS) {
  Mangle::ASTMangler Mangler;
  std::string MangledName = Mangler.mangleDeclType(D);
  OS << MangledName;
  return false;
}

static bool printObjCUSRFragment(const ValueDecl *D, StringRef ObjCName,
                                 const ExtensionDecl *ExtContextD,
                                 raw_ostream &OS) {
  if (!D)
    return true;

  // The Swift module name that the decl originated from. If the decl is
  // originating from ObjC code (ObjC module or the bridging header) then this
  // will be empty.
  StringRef ModuleName;
  if (!D->hasClangNode())
    ModuleName = D->getModuleContext()->getNameStr();

  if (isa<ClassDecl>(D)) {
    StringRef extContextName;
    if (ExtContextD) {
      extContextName = ExtContextD->getModuleContext()->getNameStr();
    }
    clang::index::generateUSRForObjCClass(ObjCName, OS,
                                          ModuleName, extContextName);
  } else if (isa<ProtocolDecl>(D)) {
    clang::index::generateUSRForObjCProtocol(ObjCName, OS, ModuleName);
  } else if (isa<VarDecl>(D)) {
    clang::index::generateUSRForObjCProperty(ObjCName, D->isStatic(), OS);
  } else if (isa<ConstructorDecl>(D)) {
    // init() is a class member in Swift, but an instance method in ObjC.
    clang::index::generateUSRForObjCMethod(ObjCName, /*IsInstanceMethod=*/true,
                                           OS);
  } else if (isa<AbstractFunctionDecl>(D)) {
    clang::index::generateUSRForObjCMethod(ObjCName, D->isInstanceMember(), OS);
  } else if (isa<EnumDecl>(D)) {
    clang::index::generateUSRForGlobalEnum(ObjCName, OS, ModuleName);
  } else if (isa<EnumElementDecl>(D)) {
    clang::index::generateUSRForEnumConstant(ObjCName, OS);
  } else {
    llvm_unreachable("Unexpected value decl");
  }
  return false;
}

static bool printObjCUSRContext(const Decl *D, raw_ostream &OS) {
  OS << clang::index::getUSRSpacePrefix();
  auto *DC = D->getDeclContext();
  if (auto *Parent = DC->getAsNominalTypeOrNominalTypeExtensionContext()) {
    auto *extContextD = dyn_cast<ExtensionDecl>(DC);
    auto ObjCName = objc_translation::getObjCNameForSwiftDecl(Parent);
    if (printObjCUSRFragment(Parent, ObjCName.first.str(), extContextD, OS))
      return true;
  }
  return false;
}

static bool printObjCUSRForAccessor(const AbstractStorageDecl *ASD,
                                    AccessorKind Kind,
                                    raw_ostream &OS) {
  if (printObjCUSRContext(ASD, OS))
    return true;

  ObjCSelector Selector;
  switch (Kind) {
    case swift::AccessorKind::IsGetter:
      Selector = ASD->getObjCGetterSelector();
      break;
    case swift::AccessorKind::IsSetter:
      Selector = ASD->getObjCSetterSelector();
      break;
    default:
      llvm_unreachable("invalid accessor kind");
  }
  assert(Selector);
  llvm::SmallString<128> Buf;
  clang::index::generateUSRForObjCMethod(Selector.getString(Buf),
                                         ASD->isInstanceMember(), OS);
  return false;
}

static bool printObjCUSR(const ValueDecl *D, raw_ostream &OS) {
  if (printObjCUSRContext(D, OS))
    return true;
  auto *extContextD = dyn_cast<ExtensionDecl>(D->getDeclContext());

  auto ObjCName = objc_translation::getObjCNameForSwiftDecl(D);

  if (!ObjCName.first.empty())
    return printObjCUSRFragment(D, ObjCName.first.str(), extContextD, OS);

  assert(ObjCName.second);
  llvm::SmallString<128> Buf;
  return printObjCUSRFragment(D, ObjCName.second.getString(Buf),
                              extContextD, OS);
}

static bool shouldUseObjCUSR(const Decl *D) {
  // Only the subscript getter/setter are visible to ObjC rather than the
  // subscript itself
  if (isa<SubscriptDecl>(D))
    return false;

  auto Parent = D->getDeclContext()->getInnermostDeclarationDeclContext();
  if (Parent && (!shouldUseObjCUSR(Parent) || // parent should be visible too
                 !D->getDeclContext()->isTypeContext() || // no local decls
                 isa<TypeDecl>(D))) // nested types aren't supported
    return false;

  if (const auto *VD = dyn_cast<ValueDecl>(D)) {
    if (isa<EnumElementDecl>(VD))
      return true;
    return objc_translation::isVisibleToObjC(VD, AccessLevel::Internal);
  }

  if (const auto *ED = dyn_cast<ExtensionDecl>(D)) {
    if (auto ExtendedType = ED->getExtendedType()) {
      auto baseClass = ExtendedType->getClassOrBoundGenericClass();
      return baseClass && shouldUseObjCUSR(baseClass) && !baseClass->isForeign();
    }
  }
  return false;
}

bool ide::printDeclUSR(const ValueDecl *D, raw_ostream &OS) {
  if (!D->hasName() && !isa<ParamDecl>(D) &&
      (!isa<FuncDecl>(D) ||
       cast<FuncDecl>(D)->getAccessorKind() == AccessorKind::NotAccessor))
    return true; // Ignore.
  if (D->getModuleContext()->isBuiltinModule())
    return true; // Ignore.
  if (isa<ModuleDecl>(D))
    return true; // Ignore.

  auto interpretAsClangNode = [](const ValueDecl *D)->ClangNode {
    ClangNode ClangN = D->getClangNode();
    if (auto ClangD = ClangN.getAsDecl()) {
      // NSErrorDomain causes the clang enum to be imported like this:
      //
      // struct MyError {
      //     enum Code : Int32 {
      //         case errFirst
      //         case errSecond
      //     }
      //     static var errFirst: MyError.Code { get }
      //     static var errSecond: MyError.Code { get }
      // }
      //
      // The clang enum constants are associated with both the static vars and
      // the enum cases.
      // But we want unique USRs for the above symbols, so use the clang USR
      // for the enum cases, and the Swift USR for the vars.
      //
      if (auto *ClangEnumConst = dyn_cast<clang::EnumConstantDecl>(ClangD)) {
        if (auto *ClangEnum = dyn_cast<clang::EnumDecl>(ClangEnumConst->getDeclContext())) {
          if (ClangEnum->hasAttr<clang::NSErrorDomainAttr>() && isa<VarDecl>(D))
            return ClangNode();
        }
      }
    }
    return ClangN;
  };

  if (ClangNode ClangN = interpretAsClangNode(D)) {
    llvm::SmallString<128> Buf;
    if (auto ClangD = ClangN.getAsDecl()) {
      bool Ignore = clang::index::generateUSRForDecl(ClangD, Buf);
      if (!Ignore)
        OS << Buf.str();
      return Ignore;
    }

    auto &Importer = *D->getASTContext().getClangModuleLoader();

    auto ClangMacroInfo = ClangN.getAsMacro();
    bool Ignore = clang::index::generateUSRForMacro(
        D->getBaseName().getIdentifier().str(),
        ClangMacroInfo->getDefinitionLoc(),
        Importer.getClangASTContext().getSourceManager(), Buf);
    if (!Ignore)
      OS << Buf.str();
    return Ignore;
  }

  if (shouldUseObjCUSR(D)) {
    return printObjCUSR(D, OS);
  }

  if (!D->hasInterfaceType())
    return true;

  // Invalid code.
  if (D->getInterfaceType().findIf([](Type t) -> bool {
        return t->is<ModuleType>();
      }))
    return true;

  Mangle::ASTMangler NewMangler;
  std::string Mangled = NewMangler.mangleDeclAsUSR(D, getUSRSpacePrefix());

  OS << Mangled;

  return false;
}

bool ide::printAccessorUSR(const AbstractStorageDecl *D, AccessorKind AccKind,
                           llvm::raw_ostream &OS) {
  // AccKind should always be either IsGetter or IsSetter here, based
  // on whether a reference is a mutating or non-mutating use.  USRs
  // aren't supposed to reflect implementation differences like stored
  // vs. addressed vs. observing.
  //
  // On the other side, the implementation indexer should be
  // registering the getter/setter USRs independently of how they're
  // actually implemented.  So a stored variable should still have
  // getter/setter USRs (pointing to the variable declaration), and an
  // addressed variable should have its "getter" point at the
  // addressor.

  AbstractStorageDecl *SD = const_cast<AbstractStorageDecl*>(D);
  if (shouldUseObjCUSR(SD)) {
    return printObjCUSRForAccessor(SD, AccKind, OS);
  }

  Mangle::ASTMangler NewMangler;
  std::string Mangled = NewMangler.mangleAccessorEntityAsUSR(AccKind,
                          AddressorKind::NotAddressor, SD, getUSRSpacePrefix());

  OS << Mangled;

  return false;
}

bool ide::printExtensionUSR(const ExtensionDecl *ED, raw_ostream &OS) {
  if (ED->getExtendedType().isNull())
    return true;

  // We make up a unique usr for each extension by combining a prefix
  // and the USR of the first value member of the extension.
  for (auto D : ED->getMembers()) {
    if (auto VD = dyn_cast<ValueDecl>(D)) {
      OS << getUSRSpacePrefix() << "e:";
      return printDeclUSR(VD, OS);
    }
  }
  if (ED->getExtendedType() && ED->getExtendedType()->getAnyNominal()) {
    OS << getUSRSpacePrefix() << "e:";
    printDeclUSR(ED->getExtendedType()->getAnyNominal(), OS);
  } else {
    return true;
  }
  for (auto Inherit : ED->getInherited()) {
    if (auto T = Inherit.getType()) {
      if (T->getAnyNominal())
        return printDeclUSR(T->getAnyNominal(), OS);
    }
  }
  return true;
}

