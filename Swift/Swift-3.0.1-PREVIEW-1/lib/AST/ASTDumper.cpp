//===--- ASTDumper.cpp - Swift Language AST Dumper ------------------------===//
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
//  This file implements dumping for the Swift ASTs.
//
//===----------------------------------------------------------------------===//

#include "swift/Basic/QuotedString.h"
#include "swift/AST/AST.h"
#include "swift/AST/ASTPrinter.h"
#include "swift/AST/ASTVisitor.h"
#include "swift/AST/ForeignErrorConvention.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/TypeVisitor.h"
#include "swift/Basic/STLExtras.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"

using namespace swift;

#define DEF_COLOR(NAME, COLOR)\
static const llvm::raw_ostream::Colors NAME##Color = llvm::raw_ostream::COLOR;

DEF_COLOR(Func, YELLOW)
DEF_COLOR(Extension, MAGENTA)
DEF_COLOR(Pattern, RED)
DEF_COLOR(TypeRepr, GREEN)
DEF_COLOR(Type, BLUE)
DEF_COLOR(TypeField, CYAN)

#undef DEF_COLOR

namespace {
  /// RAII object that prints with the given color, if color is supported on the
  /// given stream.
  class PrintWithColorRAII {
    raw_ostream &OS;
    bool ShowColors;
    
  public:
    PrintWithColorRAII(raw_ostream &os, llvm::raw_ostream::Colors color)
    : OS(os), ShowColors(false)
    {
      if (&os == &llvm::errs() || &os == &llvm::outs())
        ShowColors = llvm::errs().has_colors() && llvm::outs().has_colors();
      
      if (ShowColors) {
        if (auto str = llvm::sys::Process::OutputColor(color, false, false)) {
          OS << str;
        }
      }
    }
    
    ~PrintWithColorRAII() {
      if (ShowColors) {
        OS.resetColor();
      }
    }
    
    template<typename T>
    friend raw_ostream &operator<<(PrintWithColorRAII &&printer,
                                   const T &value){
      printer.OS << value;
      return printer.OS;
    }
  };
} // end anonymous namespace

//===----------------------------------------------------------------------===//
//  Generic param list printing.
//===----------------------------------------------------------------------===//

void RequirementRepr::dump() const {
  print(llvm::errs());
  llvm::errs() << "\n";
}

Optional<std::tuple<StringRef, StringRef, RequirementReprKind>>
RequirementRepr::getAsAnalyzedWrittenString() const {
  if (AsWrittenString.empty())
    return None;
  auto Pair = AsWrittenString.split("==");
  auto Kind = RequirementReprKind::SameType;
  if (Pair.second.empty()) {
    Pair = AsWrittenString.split(":");
    Kind =  RequirementReprKind::TypeConstraint;
  }
  assert(!Pair.second.empty() && "cannot get second type.");
  return std::make_tuple(Pair.first.trim(), Pair.second.trim(), Kind);
}

void RequirementRepr::printImpl(raw_ostream &out, bool AsWritten) const {
  auto printTy = [&](const TypeLoc &TyLoc) {
    if (AsWritten && TyLoc.getTypeRepr()) {
      TyLoc.getTypeRepr()->print(out);
    } else {
      TyLoc.getType().print(out);
    }
  };

  switch (getKind()) {
  case RequirementReprKind::TypeConstraint:
    printTy(getSubjectLoc());
    out << " : ";
    printTy(getConstraintLoc());
    break;

  case RequirementReprKind::SameType:
    printTy(getFirstTypeLoc());
    out << " == ";
    printTy(getSecondTypeLoc());
    break;
  }
}

void RequirementRepr::print(raw_ostream &out) const {
  printImpl(out, /*AsWritten=*/false);
}

void RequirementRepr::printAsWritten(raw_ostream &out) const {
  if (!AsWrittenString.empty()) {
    out << AsWrittenString;
  } else {
    printImpl(out, /*AsWritten=*/true);
  }
}

void GenericParamList::print(llvm::raw_ostream &OS) {
  OS << '<';
  bool First = true;
  for (auto P : *this) {
    if (First) {
      First = false;
    } else {
      OS << ", ";
    }
    OS << P->getName();
    if (!P->getInherited().empty()) {
      OS << " : ";
      P->getInherited()[0].getType().print(OS);
    }
  }

  if (!getRequirements().empty()) {
    OS << " where ";
    interleave(getRequirements(),
               [&](const RequirementRepr &req) {
                 req.print(OS);
               },
               [&] { OS << ", "; });
  }
  OS << '>';
}

void GenericParamList::dump() {
  print(llvm::errs());
  llvm::errs() << '\n';
}

static void printGenericParameters(raw_ostream &OS, GenericParamList *Params) {
  if (!Params)
    return;
  Params->print(OS);
}

//===----------------------------------------------------------------------===//
//  Decl printing.
//===----------------------------------------------------------------------===//

static StringRef
getStorageKindName(AbstractStorageDecl::StorageKindTy storageKind) {
  switch (storageKind) {
  case AbstractStorageDecl::Stored:
    return "stored";
  case AbstractStorageDecl::StoredWithTrivialAccessors:
    return "stored_with_trivial_accessors";
  case AbstractStorageDecl::StoredWithObservers:
    return "stored_with_observers";
  case AbstractStorageDecl::InheritedWithObservers:
    return "inherited_with_observers";
  case AbstractStorageDecl::Addressed:
    return "addressed";
  case AbstractStorageDecl::AddressedWithTrivialAccessors:
    return "addressed_with_trivial_accessors";
  case AbstractStorageDecl::AddressedWithObservers:
    return "addressed_with_observers";
  case AbstractStorageDecl::ComputedWithMutableAddress:
    return "computed_with_mutable_address";
  case AbstractStorageDecl::Computed:
    return "computed";
  }
  llvm_unreachable("bad storage kind");
}

// Print a name.
static void printName(raw_ostream &os, Identifier name) {
  if (name.empty())
    os << "<anonymous>";
  else
    os << name.str();
}

namespace {
  class PrintPattern : public PatternVisitor<PrintPattern> {
  public:
    raw_ostream &OS;
    unsigned Indent;
    bool ShowColors;

    explicit PrintPattern(raw_ostream &os, unsigned indent = 0)
      : OS(os), Indent(indent), ShowColors(false) {
      if (&os == &llvm::errs() || &os == &llvm::outs())
        ShowColors = llvm::errs().has_colors() && llvm::outs().has_colors();
    }

    void printRec(Decl *D) { D->dump(OS, Indent + 2); }
    void printRec(Expr *E) { E->print(OS, Indent + 2); }
    void printRec(Stmt *S) { S->print(OS, Indent + 2); }
    void printRec(TypeRepr *T);
    void printRec(const Pattern *P) {
      PrintPattern(OS, Indent+2).visit(const_cast<Pattern *>(P));
    }

    raw_ostream &printCommon(Pattern *P, const char *Name) {
      OS.indent(Indent) << '(';

      // Support optional color output.
      if (ShowColors) {
        if (const char *CStr =
            llvm::sys::Process::OutputColor(PatternColor, false, false)) {
          OS << CStr;
        }
      }

      OS << Name;

      if (ShowColors)
        OS.resetColor();

      if (P->isImplicit())
        OS << " implicit";

      if (P->hasType()) {
        OS << " type='";
        P->getType().print(OS);
        OS << '\'';
      }
      return OS;
    }

    void visitParenPattern(ParenPattern *P) {
      printCommon(P, "pattern_paren") << '\n';
      printRec(P->getSubPattern());
      OS << ')';
    }
    void visitTuplePattern(TuplePattern *P) {
      printCommon(P, "pattern_tuple");

      OS << " names=";
      interleave(P->getElements(),
                 [&](const TuplePatternElt &elt) {
                   auto name = elt.getLabel();
                   OS << (name.empty() ? "''" : name.str());
                 },
                 [&] { OS << ","; });

      for (auto &elt : P->getElements()) {
        OS << '\n';
        printRec(elt.getPattern());
      }
      OS << ')';
    }
    void visitNamedPattern(NamedPattern *P) {
      printCommon(P, "pattern_named")<< " '" << P->getNameStr() << "')";
    }
    void visitAnyPattern(AnyPattern *P) {
      printCommon(P, "pattern_any") << ')';
    }
    void visitTypedPattern(TypedPattern *P) {
      printCommon(P, "pattern_typed") << '\n';
      printRec(P->getSubPattern());
      if (P->getTypeLoc().getTypeRepr()) {
        OS << '\n';
        printRec(P->getTypeLoc().getTypeRepr());
      }
      OS << ')';
    }
    
    void visitIsPattern(IsPattern *P) {
      printCommon(P, "pattern_is") 
        << ' ' << getCheckedCastKindName(P->getCastKind()) << ' ';
      P->getCastTypeLoc().getType().print(OS);
      if (auto sub = P->getSubPattern()) {
        OS << '\n';
        printRec(sub);
      }
      OS << ')';
    }
    void visitExprPattern(ExprPattern *P) {
      printCommon(P, "pattern_expr");
      OS << '\n';
      if (auto m = P->getMatchExpr())
        printRec(m);
      else
        printRec(P->getSubExpr());
      OS << ')';
    }
    void visitVarPattern(VarPattern *P) {
      printCommon(P, P->isLet() ? "pattern_let" : "pattern_var");
      OS << '\n';
      printRec(P->getSubPattern());
      OS << ')';
    }
    void visitEnumElementPattern(EnumElementPattern *P) {
      printCommon(P, "pattern_enum_element");
      OS << ' ';
      P->getParentType().getType().print(OS);
      OS << '.' << P->getName();
      if (P->hasSubPattern()) {
        OS << '\n';
        printRec(P->getSubPattern());
      }
      OS << ')';
    }
    void visitOptionalSomePattern(OptionalSomePattern *P) {
      printCommon(P, "optional_some_element");
      OS << '\n';
      printRec(P->getSubPattern());
      OS << ')';
    }
    void visitBoolPattern(BoolPattern *P) {
      printCommon(P, "pattern_bool");
      OS << (P->getValue() ? " true)" : " false)");
    }

  };

  /// PrintDecl - Visitor implementation of Decl::print.
  class PrintDecl : public DeclVisitor<PrintDecl> {
  public:
    raw_ostream &OS;
    unsigned Indent;
    bool ShowColors;

    explicit PrintDecl(raw_ostream &os, unsigned indent = 0)
      : OS(os), Indent(indent), ShowColors(false) {
      if (&os == &llvm::errs() || &os == &llvm::outs())
        ShowColors = llvm::errs().has_colors() && llvm::outs().has_colors();
    }
    
    void printRec(Decl *D) { PrintDecl(OS, Indent + 2).visit(D); }
    void printRec(Expr *E) { E->print(OS, Indent+2); }
    void printRec(Stmt *S) { S->print(OS, Indent+2); }
    void printRec(Pattern *P) { PrintPattern(OS, Indent+2).visit(P); }
    void printRec(TypeRepr *T);

    // Print a field with a value.
    template<typename T>
    raw_ostream &printField(StringRef name, const T &value) {
      OS << " ";
      PrintWithColorRAII(OS, TypeFieldColor) << name;
      OS << "=" << value;
      return OS;
    }

    void printCommon(Decl *D, const char *Name,
                     llvm::Optional<llvm::raw_ostream::Colors> Color =
                      llvm::Optional<llvm::raw_ostream::Colors>()) {
      OS.indent(Indent) << '(';

      // Support optional color output.
      if (ShowColors && Color.hasValue()) {
        if (const char *CStr =
            llvm::sys::Process::OutputColor(Color.getValue(), false, false)) {
          OS << CStr;
        }
      }

      OS << Name;

      if (ShowColors)
        OS.resetColor();

      if (D->isImplicit())
        OS << " implicit";
    }

    void printInherited(ArrayRef<TypeLoc> Inherited) {
      if (Inherited.empty())
        return;
      OS << " inherits: ";
      bool First = true;
      for (auto Super : Inherited) {
        if (First)
          First = false;
        else
          OS << ", ";

        Super.getType().print(OS);
      }
    }

    void visitImportDecl(ImportDecl *ID) {
      printCommon(ID, "import_decl");

      if (ID->isExported())
        OS << " exported";

      const char *KindString;
      switch (ID->getImportKind()) {
      case ImportKind::Module:
        KindString = nullptr;
        break;
      case ImportKind::Type:
        KindString = "type";
        break;
      case ImportKind::Struct:
        KindString = "struct";
        break;
      case ImportKind::Class:
        KindString = "class";
        break;
      case ImportKind::Enum:
        KindString = "enum";
        break;
      case ImportKind::Protocol:
        KindString = "protocol";
        break;
      case ImportKind::Var:
        KindString = "var";
        break;
      case ImportKind::Func:
        KindString = "func";
        break;
      }
      if (KindString)
        OS << " kind=" << KindString;

      OS << " '";
      interleave(ID->getFullAccessPath(),
                 [&](const ImportDecl::AccessPathElement &Elem) {
                   OS << Elem.first;
                 },
                 [&] { OS << '.'; });
      OS << "')";
    }

    void visitExtensionDecl(ExtensionDecl *ED) {
      printCommon(ED, "extension_decl", ExtensionColor);
      OS << ' ';
      ED->getExtendedType().print(OS);
      printInherited(ED->getInherited());
      for (Decl *Member : ED->getMembers()) {
        OS << '\n';
        printRec(Member);
      }
      OS << ")";
    }

    void printDeclName(const ValueDecl *D) {
      if (D->getFullName())
        OS << '\"' << D->getFullName() << '\"';
      else
        OS << "'anonname=" << (const void*)D << '\'';
    }

    void visitTypeAliasDecl(TypeAliasDecl *TAD) {
      printCommon(TAD, "typealias");
      OS << " type='";
      if (TAD->hasUnderlyingType())
        OS << TAD->getUnderlyingType().getString();
      else
        OS << "<<<unresolved>>>";
      printInherited(TAD->getInherited());
      OS << "')";
    }

    void printAbstractTypeParamCommon(AbstractTypeParamDecl *decl,
                                      const char *name) {
      printCommon(decl, name);
      if (auto superclassTy = decl->getSuperclass()) {
        OS << " superclass='" << superclassTy->getString() << "'";
      }
    }

    void visitGenericTypeParamDecl(GenericTypeParamDecl *decl) {
      printAbstractTypeParamCommon(decl, "generic_type_param");
      OS << " depth=" << decl->getDepth() << " index=" << decl->getIndex();
      OS << ")";
    }

    void visitAssociatedTypeDecl(AssociatedTypeDecl *decl) {
      printAbstractTypeParamCommon(decl, "associated_type_decl");
      if (auto defaultDef = decl->getDefaultDefinitionType()) {
        OS << " default=";
        defaultDef.print(OS);
      }
      
      if (decl->isRecursive())
        OS << " <<RECURSIVE>>";
      
      OS << ")";
    }

    void visitProtocolDecl(ProtocolDecl *PD) {
      printCommon(PD, "protocol");
      printInherited(PD->getInherited());
      for (auto VD : PD->getMembers()) {
        OS << '\n';
        printRec(VD);
      }
      OS << ")";
    }

    void printCommon(ValueDecl *VD, const char *Name,
                     llvm::Optional<llvm::raw_ostream::Colors> Color =
                      llvm::Optional<llvm::raw_ostream::Colors>()) {
      printCommon((Decl*)VD, Name);

      OS << ' ';
      printDeclName(VD);
      if (AbstractFunctionDecl *AFD = dyn_cast<AbstractFunctionDecl>(VD))
        printGenericParameters(OS, AFD->getGenericParams());
      if (GenericTypeDecl *GTD = dyn_cast<GenericTypeDecl>(VD))
        printGenericParameters(OS, GTD->getGenericParams());

      OS << " type='";
      if (VD->hasType())
        VD->getType().print(OS);
      else
        OS << "<null type>";

      if (VD->hasInterfaceType() &&
          (!VD->hasType() ||
           VD->getInterfaceType().getPointer() != VD->getType().getPointer())) {
        OS << "' interface type='";
        VD->getInterfaceType()->getCanonicalType().print(OS);
      }

      OS << '\'';

      if (VD->hasAccessibility()) {
        OS << " access=";
        switch (VD->getFormalAccess()) {
        case Accessibility::Private:
          OS << "private";
          break;
        case Accessibility::FilePrivate:
          OS << "fileprivate";
          break;
        case Accessibility::Internal:
          OS << "internal";
          break;
        case Accessibility::Public:
          OS << "public";
          break;
        case Accessibility::Open:
          OS << "open";
          break;
        }
      }

      if (auto Overridden = VD->getOverriddenDecl()) {
        OS << " override=";
        Overridden->dumpRef(OS);
      }

      if (VD->isFinal())
        OS << " final";
      if (VD->isObjC())
        OS << " @objc";
    }

    void printCommon(NominalTypeDecl *NTD, const char *Name,
                     llvm::Optional<llvm::raw_ostream::Colors> Color =
                      llvm::Optional<llvm::raw_ostream::Colors>()) {
      printCommon((ValueDecl *)NTD, Name, Color);

      if (NTD->hasType()) {
        if (NTD->hasFixedLayout())
          OS << " @_fixed_layout";
        else
          OS << " @_resilient_layout";
      }
    }

    void visitSourceFile(const SourceFile &SF) {
      OS.indent(Indent) << "(source_file";
      for (Decl *D : SF.Decls) {
        if (D->isImplicit())
          continue;

        OS << '\n';
        printRec(D);
      }
      OS << ')';
    }

    void visitVarDecl(VarDecl *VD) {
      printCommon(VD, "var_decl");
      if (VD->isStatic())
        OS << " type";
      if (VD->isLet())
        OS << " let";
      if (VD->hasNonPatternBindingInit())
        OS << " non_pattern_init";
      OS << " storage_kind=" << getStorageKindName(VD->getStorageKind());
      if (VD->getAttrs().hasAttribute<LazyAttr>())
        OS << " lazy";

      printAccessors(VD);
      OS << ')';
    }

    void printAccessors(AbstractStorageDecl *D) {
      if (FuncDecl *Get = D->getGetter()) {
        OS << "\n";
        printRec(Get);
      }
      if (FuncDecl *Set = D->getSetter()) {
        OS << "\n";
        printRec(Set);
      }
      if (FuncDecl *MaterializeForSet = D->getMaterializeForSetFunc()) {
        OS << "\n";
        printRec(MaterializeForSet);
      }
      if (D->hasObservers()) {
        if (FuncDecl *WillSet = D->getWillSetFunc()) {
          OS << "\n";
          printRec(WillSet);
        }
        if (FuncDecl *DidSet = D->getDidSetFunc()) {
          OS << "\n";
          printRec(DidSet);
        }
      }
      if (D->hasAddressors()) {
        if (FuncDecl *addressor = D->getAddressor()) {
          OS << "\n";
          printRec(addressor);
        }
        if (FuncDecl *mutableAddressor = D->getMutableAddressor()) {
          OS << "\n";
          printRec(mutableAddressor);
        }
      }
    }

    void visitParamDecl(ParamDecl *PD) {
      printParameter(PD);
    }

    void visitEnumCaseDecl(EnumCaseDecl *ECD) {
      printCommon(ECD, "enum_case_decl");
      for (EnumElementDecl *D : ECD->getElements()) {
        OS << '\n';
        printRec(D);
      }
      OS << ')';
    }

    void visitEnumDecl(EnumDecl *ED) {
      printCommon(ED, "enum_decl");
      printInherited(ED->getInherited());
      for (Decl *D : ED->getMembers()) {
        OS << '\n';
        printRec(D);
      }
      OS << ')';
    }

    void visitEnumElementDecl(EnumElementDecl *EED) {
      printCommon(EED, "enum_element_decl");
      OS << ')';
    }

    void visitStructDecl(StructDecl *SD) {
      printCommon(SD, "struct_decl");
      printInherited(SD->getInherited());
      for (Decl *D : SD->getMembers()) {
        OS << '\n';
        printRec(D);
      }
      OS << ")";
    }

    void visitClassDecl(ClassDecl *CD) {
      printCommon(CD, "class_decl");
      printInherited(CD->getInherited());
      for (Decl *D : CD->getMembers()) {
        OS << '\n';
        printRec(D);
      }
      OS << ")";
    }

    void visitPatternBindingDecl(PatternBindingDecl *PBD) {
      printCommon(PBD, "pattern_binding_decl");
      
      for (auto entry : PBD->getPatternList()) {
        OS << '\n';
        printRec(entry.getPattern());
        if (entry.getInit()) {
          OS << '\n';
          printRec(entry.getInit());
        }
      }
      OS << ')';
    }

    void visitSubscriptDecl(SubscriptDecl *SD) {
      printCommon(SD, "subscript_decl");
      OS << " storage_kind=" << getStorageKindName(SD->getStorageKind());
      OS << " element=" << SD->getElementType()->getCanonicalType();
      printAccessors(SD);
      OS << ')';
    }
    
    void printCommonAFD(AbstractFunctionDecl *D, const char *Type) {
      printCommon(D, Type, FuncColor);
      if (!D->getCaptureInfo().isTrivial()) {
        OS << " ";
        D->getCaptureInfo().print(OS);
      }

      if (auto fec = D->getForeignErrorConvention()) {
        OS << " foreign_error=";
        bool wantResultType = false;
        switch (fec->getKind()) {
        case ForeignErrorConvention::ZeroResult:
          OS << "ZeroResult";
          wantResultType = true;
          break;

        case ForeignErrorConvention::NonZeroResult:
          OS << "NonZeroResult";
          wantResultType = true;
          break;

        case ForeignErrorConvention::ZeroPreservedResult:
          OS << "ZeroPreservedResult";
          break;

        case ForeignErrorConvention::NilResult:
          OS << "NilResult";
          break;

        case ForeignErrorConvention::NonNilError:
          OS << "NonNilError";
          break;
        }

        OS << ((fec->isErrorOwned() == ForeignErrorConvention::IsOwned)
                ? ",owned"
                : ",unowned");
        OS << ",param=" << llvm::utostr(fec->getErrorParameterIndex());
        OS << ",paramtype=" << fec->getErrorParameterType().getString();
        if (wantResultType)
          OS << ",resulttype=" << fec->getResultType().getString();
      }
    }
    
    void printParameter(const ParamDecl *P) {
      OS.indent(Indent) << "(parameter ";
      printDeclName(P);
      if (!P->getArgumentName().empty())
        OS << " apiName=" << P->getArgumentName();
      
      OS << " type=";
      if (P->hasType()) {
        OS << '\'';
        P->getType().print(OS);
        OS << '\'';
      } else
        OS << "<null type>";
      
      if (!P->isLet())
        OS << " mutable";
      
      if (P->isVariadic())
        OS << " variadic";

      switch (P->getDefaultArgumentKind()) {
      case DefaultArgumentKind::None: break;
      case DefaultArgumentKind::Column:
        printField("default_arg", "#column");
        break;
      case DefaultArgumentKind::DSOHandle:
        printField("default_arg", "#dsohandle");
        break;
      case DefaultArgumentKind::File:
        printField("default_arg", "#file");
        break;
      case DefaultArgumentKind::Function:
        printField("default_arg", "#function");
        break;
      case DefaultArgumentKind::Inherited:
        printField("default_arg", "inherited");
        break;
      case DefaultArgumentKind::Line:
        printField("default_arg", "#line");
        break;
      case DefaultArgumentKind::Nil:
        printField("default_arg", "nil");
        break;
      case DefaultArgumentKind::EmptyArray:
        printField("default_arg", "[]");
        break;
      case DefaultArgumentKind::EmptyDictionary:
        printField("default_arg", "[:]");
        break;
      case DefaultArgumentKind::Normal:
        printField("default_arg", "normal");
        break;
      }
      
      if (auto init = P->getDefaultValue()) {
        OS << " expression=\n";
        printRec(init->getExpr());
      }
      
      OS << ')';
    }
    

    void printParameterList(const ParameterList *params) {
      OS.indent(Indent) << "(parameter_list";
      Indent += 2;
      for (auto P : *params) {
        OS << '\n';
        printParameter(P);
      }
      OS << ')';
      Indent -= 2;
    }

    void printAbstractFunctionDecl(AbstractFunctionDecl *D) {
      for (auto pl : D->getParameterLists()) {
        OS << '\n';
        Indent += 2;
        printParameterList(pl);
        Indent -= 2;
     }
      if (auto FD = dyn_cast<FuncDecl>(D)) {
        if (FD->getBodyResultTypeLoc().getTypeRepr()) {
          OS << '\n';
          Indent += 2;
          OS.indent(Indent);
          OS << "(result\n";
          printRec(FD->getBodyResultTypeLoc().getTypeRepr());
          OS << ')';
          Indent -= 2;
        }
      }
      if (auto Body = D->getBody(/*canSynthesize=*/false)) {
        OS << '\n';
        printRec(Body);
      }
     }
    
    void visitFuncDecl(FuncDecl *FD) {
      printCommonAFD(FD, "func_decl");
      if (FD->isStatic())
        OS << " type";
      if (auto *ASD = FD->getAccessorStorageDecl()) {
        switch (FD->getAccessorKind()) {
        case AccessorKind::NotAccessor: llvm_unreachable("Isn't an accessor?");
        case AccessorKind::IsGetter: OS << " getter"; break;
        case AccessorKind::IsSetter: OS << " setter"; break;
        case AccessorKind::IsWillSet: OS << " willset"; break;
        case AccessorKind::IsDidSet: OS << " didset"; break;
        case AccessorKind::IsMaterializeForSet: OS << " materializeForSet"; break;
        case AccessorKind::IsAddressor: OS << " addressor"; break;
        case AccessorKind::IsMutableAddressor: OS << " mutableAddressor"; break;
        }

        OS << "_for=" << ASD->getFullName();
      }
      
      for (auto VD: FD->getSatisfiedProtocolRequirements()) {
        OS << '\n';
        OS.indent(Indent+2) << "(conformance ";
        VD->dumpRef(OS);
        OS << ')';
      }

      printAbstractFunctionDecl(FD);

      OS << ')';
     }

    void visitConstructorDecl(ConstructorDecl *CD) {
      printCommonAFD(CD, "constructor_decl");
      if (CD->isRequired())
        OS << " required";
      switch (CD->getInitKind()) {
      case CtorInitializerKind::Designated:
        OS << " designated";
        break;

      case CtorInitializerKind::Convenience:
        OS << " convenience";
        break;

      case CtorInitializerKind::ConvenienceFactory:
        OS << " convenience_factory";
        break;

      case CtorInitializerKind::Factory:
        OS << " factory";
        break;
      }
      
      switch (CD->getFailability()) {
      case OTK_None:
        break;

      case OTK_Optional:
        OS << " failable=Optional";
        break;

      case OTK_ImplicitlyUnwrappedOptional:
        OS << " failable=ImplicitlyUnwrappedOptional";
        break;
      }

      printAbstractFunctionDecl(CD);
      OS << ')';
    }

    void visitDestructorDecl(DestructorDecl *DD) {
      printCommonAFD(DD, "destructor_decl");
      printAbstractFunctionDecl(DD);
      OS << ')';
    }

    void visitTopLevelCodeDecl(TopLevelCodeDecl *TLCD) {
      printCommon(TLCD, "top_level_code_decl");
      if (TLCD->getBody()) {
        OS << "\n";
        printRec(TLCD->getBody());
      }
      OS << ')';
    }
    
    void visitIfConfigDecl(IfConfigDecl *ICD) {
      OS.indent(Indent) << "(#if_decl\n";
      Indent += 2;
      for (auto &Clause : ICD->getClauses()) {
        OS.indent(Indent) << (Clause.Cond ? "(#if:\n" : "\n(#else:\n");
        if (Clause.Cond)
          printRec(Clause.Cond);
        
        for (auto D : Clause.Members) {
          OS << '\n';
          printRec(D);
        }

        OS << ')';
      }
    
      Indent -= 2;
      OS << ')';
    }

    void visitPrecedenceGroupDecl(PrecedenceGroupDecl *PGD) {
      printCommon(PGD, "precedence_group_decl ");
      OS << PGD->getName() << "\n";

      OS.indent(Indent+2);
      OS << "associativity ";
      switch (PGD->getAssociativity()) {
      case Associativity::None: OS << "none\n"; break;
      case Associativity::Left: OS << "left\n"; break;
      case Associativity::Right: OS << "right\n"; break;
      }

      OS.indent(Indent+2);
      OS << "assignment " << (PGD->isAssignment() ? "true" : "false");

      auto printRelations =
          [&](StringRef label, ArrayRef<PrecedenceGroupDecl::Relation> rels) {
        if (rels.empty()) return;
        OS << '\n';
        OS.indent(Indent+2);
        OS << label << ' ' << rels[0].Name;
        for (auto &rel : rels.slice(1))
          OS << ", " << rel.Name;
      };
      printRelations("higherThan", PGD->getHigherThan());
      printRelations("lowerThan", PGD->getLowerThan());

      OS << ')';
    }
    
    void visitInfixOperatorDecl(InfixOperatorDecl *IOD) {
      printCommon(IOD, "infix_operator_decl ");
      OS << IOD->getName() << "\n";
      OS.indent(Indent+2);
      OS << "precedence " << IOD->getPrecedenceGroupName();
      if (!IOD->getPrecedenceGroup()) OS << " <null>";
      OS << ')';
    }
    
    void visitPrefixOperatorDecl(PrefixOperatorDecl *POD) {
      printCommon(POD, "prefix_operator_decl ");
      OS << POD->getName() << ')';
    }

    void visitPostfixOperatorDecl(PostfixOperatorDecl *POD) {
      printCommon(POD, "postfix_operator_decl ");
      OS << POD->getName() << ')';
    }

    void visitModuleDecl(ModuleDecl *MD) {
      printCommon(MD, "module");
      OS << ')';
    }
  };
} // end anonymous namespace.

void ParameterList::dump() const {
  dump(llvm::errs(), 0);
}

void ParameterList::dump(raw_ostream &OS, unsigned Indent) const {
  llvm::Optional<llvm::SaveAndRestore<bool>> X;
  
  // Make sure to print type variables if we can get to ASTContext.
  if (size() != 0 && get(0)) {
    auto &ctx = get(0)->getASTContext();
    X.emplace(llvm::SaveAndRestore<bool>(ctx.LangOpts.DebugConstraintSolver,
                                         true));
  }
  
  PrintDecl(OS, Indent).printParameterList(this);
  llvm::errs() << '\n';
}



void Decl::dump() const {
  dump(llvm::errs(), 0);
}

void Decl::dump(raw_ostream &OS, unsigned Indent) const {
  // Make sure to print type variables.
  llvm::SaveAndRestore<bool> X(getASTContext().LangOpts.DebugConstraintSolver,
                               true);
  PrintDecl(OS, Indent).visit(const_cast<Decl *>(this));
  OS << '\n';
}

/// Print the given declaration context (with its parents).
static void printContext(raw_ostream &os, DeclContext *dc) {
  if (auto parent = dc->getParent()) {
    printContext(os, parent);
    os << '.';
  }

  switch (dc->getContextKind()) {
  case DeclContextKind::Module:
    printName(os, cast<Module>(dc)->getName());
    break;

  case DeclContextKind::FileUnit:
    // FIXME: print the file's basename?
    os << "(file)";
    break;

  case DeclContextKind::SerializedLocal:
    os << "local context";
    break;

  case DeclContextKind::AbstractClosureExpr: {
    auto *ACE = cast<AbstractClosureExpr>(dc);
    if (isa<ClosureExpr>(ACE))
      os << "explicit closure discriminator=";
    if (isa<AutoClosureExpr>(ACE))
      os << "autoclosure discriminator=";
    os << ACE->getDiscriminator();
    break;
  }

  case DeclContextKind::GenericTypeDecl:
    printName(os, cast<GenericTypeDecl>(dc)->getName());
    break;

  case DeclContextKind::ExtensionDecl:
    if (auto extendedTy = cast<ExtensionDecl>(dc)->getExtendedType()) {
      if (auto nominal = extendedTy->getAnyNominal()) {
        printName(os, nominal->getName());
        break;
      }
    }
    os << "extension";
    break;

  case DeclContextKind::Initializer:
    switch (cast<Initializer>(dc)->getInitializerKind()) {
    case InitializerKind::PatternBinding:
      os << "pattern binding initializer";
      break;
    case InitializerKind::DefaultArgument:
      os << "default argument initializer";
      break;
    }
    break;

  case DeclContextKind::TopLevelCodeDecl:
    os << "top-level code";
    break;

  case DeclContextKind::AbstractFunctionDecl: {
    auto *AFD = cast<AbstractFunctionDecl>(dc);
    if (isa<FuncDecl>(AFD))
      os << "func decl";
    if (isa<ConstructorDecl>(AFD))
      os << "init";
    if (isa<DestructorDecl>(AFD))
      os << "deinit";
    break;
  }
  case DeclContextKind::SubscriptDecl:
    os << "subscript decl";
    break;
  }
}

std::string ValueDecl::printRef() const {
  std::string result;
  llvm::raw_string_ostream os(result);
  dumpRef(os);
  return os.str();
}

void ValueDecl::dumpRef(raw_ostream &os) const {
  // Print the context.
  printContext(os, getDeclContext());
  os << ".";

  // Print name.
  getFullName().printPretty(os);

  // Print location.
  auto &srcMgr = getASTContext().SourceMgr;
  if (getLoc().isValid()) {
    os << '@';
    getLoc().print(os, srcMgr);
  }
}

void LLVM_ATTRIBUTE_USED ValueDecl::dumpRef() const {
  dumpRef(llvm::errs());
}

void SourceFile::dump() const {
  dump(llvm::errs());
}

void SourceFile::dump(llvm::raw_ostream &OS) const {
  llvm::SaveAndRestore<bool> X(getASTContext().LangOpts.DebugConstraintSolver,
                               true);
  PrintDecl(OS).visitSourceFile(*this);
  llvm::errs() << '\n';
}

void Pattern::dump() const {
  PrintPattern(llvm::errs()).visit(const_cast<Pattern*>(this));
  llvm::errs() << '\n';
}

//===----------------------------------------------------------------------===//
// Printing for Stmt and all subclasses.
//===----------------------------------------------------------------------===//

namespace {
/// PrintStmt - Visitor implementation of Expr::print.
class PrintStmt : public StmtVisitor<PrintStmt> {
public:
  raw_ostream &OS;
  unsigned Indent;

  PrintStmt(raw_ostream &os, unsigned indent) : OS(os), Indent(indent) {
  }

  void printRec(Stmt *S) {
    Indent += 2;
    if (S)
      visit(S);
    else
      OS.indent(Indent) << "(**NULL STATEMENT**)";
    Indent -= 2;
  }

  void printRec(Decl *D) { D->dump(OS, Indent + 2); }
  void printRec(Expr *E) { E->print(OS, Indent + 2); }
  void printRec(const Pattern *P) {
    PrintPattern(OS, Indent+2).visit(const_cast<Pattern *>(P));
  }
  
  void printRec(StmtConditionElement C) {
    switch (C.getKind()) {
    case StmtConditionElement::CK_Boolean:
      return printRec(C.getBoolean());
    case StmtConditionElement::CK_PatternBinding:
      Indent += 2;
      OS.indent(Indent) << "(pattern\n";
      
      printRec(C.getPattern());
      OS << "\n";
      printRec(C.getInitializer());
      OS << ")";
      Indent -= 2;
      break;
    case StmtConditionElement::CK_Availability:
      Indent += 2;
      OS.indent(Indent) << "(#available\n";
      for (auto *Query : C.getAvailability()->getQueries()) {
        OS << '\n';
        switch (Query->getKind()) {
        case AvailabilitySpecKind::VersionConstraint:
          cast<VersionConstraintAvailabilitySpec>(Query)->print(OS, Indent + 2);
          break;
        case AvailabilitySpecKind::OtherPlatform:
          cast<OtherPlatformAvailabilitySpec>(Query)->print(OS, Indent + 2);
          break;
        }
      }
      OS << ")";
      Indent -= 2;
      break;
    }
  }
  
  void visitBraceStmt(BraceStmt *S) {
    printASTNodes(S->getElements(), "brace_stmt");
  }

  void printASTNodes(const ArrayRef<ASTNode> &Elements, StringRef Name) {
    OS.indent(Indent) << "(" << Name;
    for (auto Elt : Elements) {
      OS << '\n';
      if (Expr *SubExpr = Elt.dyn_cast<Expr*>())
        printRec(SubExpr);
      else if (Stmt *SubStmt = Elt.dyn_cast<Stmt*>())
        printRec(SubStmt);
      else
        printRec(Elt.get<Decl*>());
    }
    OS << ')';
  }

  void visitReturnStmt(ReturnStmt *S) {
    OS.indent(Indent) << "(return_stmt";
    if (S->hasResult()) {
      OS << '\n';
      printRec(S->getResult());
    }
    OS << ')';
  }
  
  void visitDeferStmt(DeferStmt *S) {
    OS.indent(Indent) << "(defer_stmt\n";
    printRec(S->getTempDecl());
    OS << '\n';
    printRec(S->getCallExpr());
    OS << ')';
  }

  void visitIfStmt(IfStmt *S) {
    OS.indent(Indent) << "(if_stmt\n";
    for (auto elt : S->getCond())
      printRec(elt);
    OS << '\n';
    printRec(S->getThenStmt());
    if (S->getElseStmt()) {
      OS << '\n';
      printRec(S->getElseStmt());
    }
    OS << ')';
  }
  
  void visitGuardStmt(GuardStmt *S) {
    OS.indent(Indent) << "(guard_stmt\n";
    for (auto elt : S->getCond())
      printRec(elt);
    OS << '\n';
    printRec(S->getBody());
    OS << ')';
  }

  void visitIfConfigStmt(IfConfigStmt *S) {
    OS.indent(Indent) << "(#if_stmt\n";
    Indent += 2;
    for (auto &Clause : S->getClauses()) {
      OS.indent(Indent) << (Clause.Cond ? "(#if:\n" : "#else");
      if (Clause.Cond)
        printRec(Clause.Cond);

      OS << '\n';
      Indent += 2;
      printASTNodes(Clause.Elements, "elements");
      Indent -= 2;
    }
    
    Indent -= 2;
    OS << ')';
  }

  void visitDoStmt(DoStmt *S) {
    OS.indent(Indent) << "(do_stmt\n";
    printRec(S->getBody());
    OS << ')';
  }

  void visitWhileStmt(WhileStmt *S) {
    OS.indent(Indent) << "(while_stmt\n";
    for (auto elt : S->getCond())
      printRec(elt);
    OS << '\n';
    printRec(S->getBody());
    OS << ')';
  }

  void visitRepeatWhileStmt(RepeatWhileStmt *S) {
    OS.indent(Indent) << "(do_while_stmt\n";
    printRec(S->getBody());
    OS << '\n';
    printRec(S->getCond());
    OS << ')';
  }
  void visitForStmt(ForStmt *S) {
    OS.indent(Indent) << "(for_stmt\n";
    if (!S->getInitializerVarDecls().empty()) {
      for (auto D : S->getInitializerVarDecls()) {
        printRec(D);
        OS << '\n';
      }
    } else if (auto *Initializer = S->getInitializer().getPtrOrNull()) {
      printRec(Initializer);
      OS << '\n';
    } else {
      OS.indent(Indent+2) << "<null initializer>\n";
    }

    if (auto *Cond = S->getCond().getPtrOrNull())
      printRec(Cond);
    else
      OS.indent(Indent+2) << "<null condition>";
    OS << '\n';

    if (auto *Increment = S->getIncrement().getPtrOrNull()) {
      printRec(Increment);
    } else {
      OS.indent(Indent+2) << "<null increment>";
    }
    OS << '\n';
    printRec(S->getBody());
    OS << ')';
  }
  void visitForEachStmt(ForEachStmt *S) {
    OS.indent(Indent) << "(for_each_stmt\n";
    printRec(S->getPattern());
    OS << '\n';
    if (S->getWhere()) {
      Indent += 2;
      OS.indent(Indent) << "(where\n";
      printRec(S->getWhere());
      OS << ")\n";
      Indent -= 2;
    }
    printRec(S->getPattern());
    OS << '\n';
    printRec(S->getSequence());
    OS << '\n';
    if (S->getIterator()) {
      printRec(S->getIterator());
      OS << '\n';
    }
    if (S->getIteratorNext()) {
      printRec(S->getIteratorNext());
      OS << '\n';
    }
    printRec(S->getBody());
    OS << ')';
  }
  void visitBreakStmt(BreakStmt *S) {
    OS.indent(Indent) << "(break_stmt)";
  }
  void visitContinueStmt(ContinueStmt *S) {
    OS.indent(Indent) << "(continue_stmt)";
  }
  void visitFallthroughStmt(FallthroughStmt *S) {
    OS.indent(Indent) << "(fallthrough_stmt)";
  }
  void visitSwitchStmt(SwitchStmt *S) {
    OS.indent(Indent) << "(switch_stmt\n";
    printRec(S->getSubjectExpr());
    for (CaseStmt *C : S->getCases()) {
      OS << '\n';
      printRec(C);
    }
    OS << ')';
  }
  void visitCaseStmt(CaseStmt *S) {
    OS.indent(Indent) << "(case_stmt";
    for (const auto &LabelItem : S->getCaseLabelItems()) {
      OS << '\n';
      OS.indent(Indent + 2) << "(case_label_item";
      if (auto *CasePattern = LabelItem.getPattern()) {
        OS << '\n';
        printRec(CasePattern);
      }
      if (auto *Guard = LabelItem.getGuardExpr()) {
        OS << '\n';
        Guard->print(OS, Indent+4);
      }
      OS << ')';
    }
    OS << '\n';
    printRec(S->getBody());
    OS << ')';
  }
  void visitFailStmt(FailStmt *S) {
    OS.indent(Indent) << "(fail_stmt)";
  }
  
  void visitThrowStmt(ThrowStmt *S) {
    OS.indent(Indent) << "(throw_stmt\n";
    printRec(S->getSubExpr());
    OS << ')';
  }

  void visitDoCatchStmt(DoCatchStmt *S) {
    OS.indent(Indent) << "(do_catch_stmt\n";
    printRec(S->getBody());
    OS << '\n';
    Indent += 2;
    visitCatches(S->getCatches());
    Indent -= 2;
    OS << ')';
  }
  void visitCatches(ArrayRef<CatchStmt*> clauses) {
    for (auto clause : clauses) {
      visitCatchStmt(clause);
    }
  }
  void visitCatchStmt(CatchStmt *clause) {
    OS.indent(Indent) << "(catch\n";
    printRec(clause->getErrorPattern());
    if (auto guard = clause->getGuardExpr()) {
      OS << '\n';
      printRec(guard);
    }
    OS << '\n';
    printRec(clause->getBody());
    OS << ')';
  }
};

} // end anonymous namespace.

void Stmt::dump() const {
  print(llvm::errs());
  llvm::errs() << '\n';
}

void Stmt::print(raw_ostream &OS, unsigned Indent) const {
  PrintStmt(OS, Indent).visit(const_cast<Stmt*>(this));
}

//===----------------------------------------------------------------------===//
// Printing for Expr and all subclasses.
//===----------------------------------------------------------------------===//

static raw_ostream &operator<<(raw_ostream &os, AccessSemantics accessKind) {
  switch (accessKind) {
  case AccessSemantics::Ordinary: return os;
  case AccessSemantics::DirectToStorage: return os << " direct_to_storage";
  case AccessSemantics::DirectToAccessor: return os << " direct_to_accessor";
  case AccessSemantics::BehaviorInitialization: return os << " behavior_init";
  }
  llvm_unreachable("bad access kind");
}

namespace {
/// PrintExpr - Visitor implementation of Expr::print.
class PrintExpr : public ExprVisitor<PrintExpr> {
public:
  raw_ostream &OS;
  unsigned Indent;

  PrintExpr(raw_ostream &os, unsigned indent) : OS(os), Indent(indent) {
  }

  void printRec(Expr *E) {
    Indent += 2;
    if (E)
      visit(E);
    else
      OS.indent(Indent) << "(**NULL EXPRESSION**)";
    Indent -= 2;
  }

  void printRecLabelled(Expr *E, StringRef label) {
    Indent += 2;
    OS.indent(Indent);
    OS << '(' << label << '\n';
    printRec(E);
    OS << ')';
    Indent -= 2;
  }

  /// FIXME: This should use ExprWalker to print children.

  void printRec(Decl *D) { D->dump(OS, Indent + 2); }
  void printRec(Stmt *S) { S->print(OS, Indent + 2); }
  void printRec(const Pattern *P) {
    PrintPattern(OS, Indent+2).visit(const_cast<Pattern *>(P));
  }
  void printRec(TypeRepr *T);
  void printRec(ProtocolConformanceRef conf) {
    conf.dump(OS, Indent + 2);
  }

  static const char *getAccessKindString(AccessKind kind) {
    switch (kind) {
    case AccessKind::Read: return "read";
    case AccessKind::Write: return "write";
    case AccessKind::ReadWrite: return "readwrite";
    }
    llvm_unreachable("bad access kind");
  }

  raw_ostream &printCommon(Expr *E, const char *C) {
    OS.indent(Indent) << '(' << C;
    if (E->isImplicit())
      OS << " implicit";
    OS << " type='" << E->getType() << '\'';

    if (E->hasLValueAccessKind())
      OS << " accessKind=" << getAccessKindString(E->getLValueAccessKind());

    // If we have a source range and an ASTContext, print the source range.
    if (auto Ty = E->getType()) {
      auto &Ctx = Ty->getASTContext();
      auto L = E->getLoc();
      if (L.isValid()) {
        OS << " location=";
        L.print(OS, Ctx.SourceMgr);
      }

      auto R = E->getSourceRange();
      if (R.isValid()) {
        OS << " range=";
        R.print(OS, Ctx.SourceMgr, /*PrintText=*/false);
      }
    }

    return OS;
  }

  void visitErrorExpr(ErrorExpr *E) {
    printCommon(E, "error_expr") << ')';
  }

  void visitCodeCompletionExpr(CodeCompletionExpr *E) {
    printCommon(E, "code_completion_expr") << ')';
  }

  void visitNilLiteralExpr(NilLiteralExpr *E) {
    printCommon(E, "nil_literal_expr") << ')';
  }
  
  void visitIntegerLiteralExpr(IntegerLiteralExpr *E) {
    printCommon(E, "integer_literal_expr");
    if (E->isNegative())
      OS << " negative";
    OS << " value=";
    Type T = E->getType();
    if (T.isNull() || !T->is<BuiltinIntegerType>())
      OS << E->getDigitsText();
    else
      OS << E->getValue();
    OS << ')';
  }
  void visitFloatLiteralExpr(FloatLiteralExpr *E) {
    printCommon(E, "float_literal_expr") << " value="
          << E->getDigitsText() << ')';
  }

  void visitBooleanLiteralExpr(BooleanLiteralExpr *E) {
    printCommon(E, "boolean_literal_expr") 
      << " value=" << (E->getValue() ? "true" : "false")
      << ')';
  }

  void printStringEncoding(StringLiteralExpr::Encoding encoding) {
    switch (encoding) {
    case StringLiteralExpr::UTF8: OS << "utf8"; break;
    case StringLiteralExpr::UTF16: OS << "utf16"; break;
    case StringLiteralExpr::OneUnicodeScalar: OS << "unicodeScalar"; break;
    }
  }

  void visitStringLiteralExpr(StringLiteralExpr *E) {
    printCommon(E, "string_literal_expr")
      << " encoding=";
    printStringEncoding(E->getEncoding());
    OS << " value=" << QuotedString(E->getValue())
       << " builtin_initializer=";
    E->getBuiltinInitializer().dump(OS);
    OS << " initializer=";
    E->getInitializer().dump(OS);
    OS << ')';
  }
  void visitInterpolatedStringLiteralExpr(InterpolatedStringLiteralExpr *E) {
    printCommon(E, "interpolated_string_literal_expr");
    for (auto Segment : E->getSegments()) {
      OS << '\n';
      printRec(Segment);
    }
    OS << ')';
  }
  void visitMagicIdentifierLiteralExpr(MagicIdentifierLiteralExpr *E) {
    printCommon(E, "magic_identifier_literal_expr") << " kind=";
    switch (E->getKind()) {
    case MagicIdentifierLiteralExpr::File:
      OS << "#file encoding=";
      printStringEncoding(E->getStringEncoding());
      break;

    case MagicIdentifierLiteralExpr::Function:
      OS << "#function encoding=";
      printStringEncoding(E->getStringEncoding());
      break;
        
    case MagicIdentifierLiteralExpr::Line:  OS << "#line"; break;
    case MagicIdentifierLiteralExpr::Column:  OS << "#column"; break;
    case MagicIdentifierLiteralExpr::DSOHandle:  OS << "#dsohandle"; break;
    }

    if (E->isString()) {
      OS << " builtin_initializer=";
      E->getBuiltinInitializer().dump(OS);
      OS << " initializer=";
      E->getInitializer().dump(OS);
    }
    OS << ')';
  }

  void visitObjectLiteralExpr(ObjectLiteralExpr *E) {
    printCommon(E, "object_literal") 
      << " kind='" << E->getLiteralKindPlainName() << "'";
    printArgumentLabels(E->getArgumentLabels());
    OS << "\n";
    printRec(E->getArg());
  }

  void visitDiscardAssignmentExpr(DiscardAssignmentExpr *E) {
    printCommon(E, "discard_assignment_expr") << ')';
  }
  
  void visitDeclRefExpr(DeclRefExpr *E) {
    printCommon(E, "declref_expr")
      << " decl=";
    E->getDeclRef().dump(OS);
    OS << E->getAccessSemantics();
    OS << " function_ref=" << getFunctionRefKindStr(E->getFunctionRefKind());
    OS << " specialized=" << (E->isSpecialized()? "yes" : "no");

    for (auto TR : E->getGenericArgs()) {
      OS << '\n';
      printRec(TR);
    }
    OS << ')';
  }
  void visitSuperRefExpr(SuperRefExpr *E) {
    printCommon(E, "super_ref_expr") << ')';
  }

  void visitTypeExpr(TypeExpr *E) {
    printCommon(E, "type_expr");
    OS << " typerepr='";
    if (E->getTypeRepr())
      E->getTypeRepr()->print(OS);
    else
      OS << "<<NULL>>";
    OS << "')";
  }

  void visitOtherConstructorDeclRefExpr(OtherConstructorDeclRefExpr *E) {
    printCommon(E, "other_constructor_ref_expr")
      << " decl=";
    E->getDeclRef().dump(OS);
    OS << ')';
  }
  void visitOverloadedDeclRefExpr(OverloadedDeclRefExpr *E) {
    printCommon(E, "overloaded_decl_ref_expr")
      << " name=" << E->getDecls()[0]->getName()
      << " #decls=" << E->getDecls().size()
      << " specialized=" << (E->isSpecialized()? "yes" : "no")
      << " function_ref=" << getFunctionRefKindStr(E->getFunctionRefKind());

    for (ValueDecl *D : E->getDecls()) {
      OS << '\n';
      OS.indent(Indent);
      D->dumpRef(OS);
    }
    OS << ')';
  }
  void visitUnresolvedDeclRefExpr(UnresolvedDeclRefExpr *E) {
    printCommon(E, "unresolved_decl_ref_expr")
      << " name=" << E->getName()
      << " specialized=" << (E->isSpecialized()? "yes" : "no") << ')'
      << " function_ref=" << getFunctionRefKindStr(E->getFunctionRefKind());
  }
  void visitUnresolvedSpecializeExpr(UnresolvedSpecializeExpr *E) {
    printCommon(E, "unresolved_specialize_expr") << '\n';
    printRec(E->getSubExpr());
    for (TypeLoc T : E->getUnresolvedParams()) {
      OS << '\n';
      printRec(T.getTypeRepr());
    }
    OS << ')';
  }

  void visitMemberRefExpr(MemberRefExpr *E) {
    printCommon(E, "member_ref_expr")
      << " decl=";
    E->getMember().dump(OS);
    
    OS << E->getAccessSemantics();
    if (E->isSuper())
      OS << " super";
            
    OS << '\n';
    printRec(E->getBase());
    OS << ')';
  }
  void visitDynamicMemberRefExpr(DynamicMemberRefExpr *E) {
    printCommon(E, "dynamic_member_ref_expr")
      << " decl=";
    E->getMember().dump(OS);
    OS << '\n';
    printRec(E->getBase());
    OS << ')';
  }
  void visitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
    printCommon(E, "unresolved_member_expr")
      << " name='" << E->getName() << "'";
    printArgumentLabels(E->getArgumentLabels());
    if (E->getArgument()) {
      OS << '\n';
      printRec(E->getArgument());
    }
    OS << "')";
  }
  void visitDotSelfExpr(DotSelfExpr *E) {
    printCommon(E, "dot_self_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitParenExpr(ParenExpr *E) {
    printCommon(E, "paren_expr");
    if (E->hasTrailingClosure())
      OS << " trailing-closure";
    OS << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitTupleExpr(TupleExpr *E) {
    printCommon(E, "tuple_expr");
    if (E->hasTrailingClosure())
      OS << " trailing-closure";

    if (E->hasElementNames()) {
      OS << " names=";

      interleave(E->getElementNames(),
                 [&](Identifier name) { OS << (name.empty()?"''":name.str());},
                 [&] { OS << ","; });
    }

    for (unsigned i = 0, e = E->getNumElements(); i != e; ++i) {
      OS << '\n';
      if (E->getElement(i))
        printRec(E->getElement(i));
      else
        OS.indent(Indent+2) << "<<tuple element default value>>";
    }
    OS << ')';
  }
  void visitArrayExpr(ArrayExpr *E) {
    printCommon(E, "array_expr");
    for (auto elt : E->getElements()) {
      OS << '\n';
      printRec(elt);
    }
    OS << ')';
  }
  void visitDictionaryExpr(DictionaryExpr *E) {
    printCommon(E, "dictionary_expr");
    for (auto elt : E->getElements()) {
      OS << '\n';
      printRec(elt);
    }
    OS << ')';
  }
  void visitSubscriptExpr(SubscriptExpr *E) {
    printCommon(E, "subscript_expr");
    OS << E->getAccessSemantics();
    if (E->isSuper())
      OS << " super";
    if (E->hasDecl()) {
      OS << "  decl=";
      E->getDecl().dump(OS);
    }
    printArgumentLabels(E->getArgumentLabels());
    OS << '\n';
    printRec(E->getBase());
    OS << '\n';
    printRec(E->getIndex());
    OS << ')';
  }
  void visitDynamicSubscriptExpr(DynamicSubscriptExpr *E) {
    printCommon(E, "dynamic_subscript_expr")
      << " decl=";
    E->getMember().dump(OS);
    printArgumentLabels(E->getArgumentLabels());
    OS << '\n';
    printRec(E->getBase());
    OS << '\n';
    printRec(E->getIndex());
    OS << ')';
  }
  void visitUnresolvedDotExpr(UnresolvedDotExpr *E) {
    printCommon(E, "unresolved_dot_expr")
      << " field '" << E->getName() << "'"
      << " function_ref=" << getFunctionRefKindStr(E->getFunctionRefKind());
    if (E->getBase()) {
      OS << '\n';
      printRec(E->getBase());
    }
    OS << ')';
  }
  void visitTupleElementExpr(TupleElementExpr *E) {
    printCommon(E, "tuple_element_expr")
      << " field #" << E->getFieldNumber() << '\n';
    printRec(E->getBase());
    OS << ')';
  }
  void visitTupleShuffleExpr(TupleShuffleExpr *E) {
    printCommon(E, "tuple_shuffle_expr");
    if (E->isSourceScalar()) OS << " sourceIsScalar";
    OS << " elements=[";
    for (unsigned i = 0, e = E->getElementMapping().size(); i != e; ++i) {
      if (i) OS << ", ";
      OS << E->getElementMapping()[i];
    }
    OS << "]";
    OS << " variadic_sources=[";
    interleave(E->getVariadicArgs(),
               [&](unsigned source) {
                 OS << source;
               },
               [&] { OS << ", "; });
    OS << "]";

    OS << "\n";
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitUnresolvedTypeConversionExpr(UnresolvedTypeConversionExpr *E) {
    printCommon(E, "unresolvedtype_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitFunctionConversionExpr(FunctionConversionExpr *E) {
    printCommon(E, "function_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitCovariantFunctionConversionExpr(CovariantFunctionConversionExpr *E){
    printCommon(E, "covariant_function_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitCovariantReturnConversionExpr(CovariantReturnConversionExpr *E){
    printCommon(E, "covariant_return_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitErasureExpr(ErasureExpr *E) {
    printCommon(E, "erasure_expr") << '\n';
    for (auto conf : E->getConformances()) {
      printRec(conf);
      OS << '\n';
    }
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitAnyHashableErasureExpr(AnyHashableErasureExpr *E) {
    printCommon(E, "any_hashable_erasure_expr") << '\n';
    printRec(E->getConformance());
    OS << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitLoadExpr(LoadExpr *E) {
    printCommon(E, "load_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitMetatypeConversionExpr(MetatypeConversionExpr *E) {
    printCommon(E, "metatype_conversion_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitCollectionUpcastConversionExpr(CollectionUpcastConversionExpr *E) {
    printCommon(E, "collection_upcast_expr");
    if (E->bridgesToObjC())
      OS << " bridges_to_objc";
    OS << '\n';
    printRec(E->getSubExpr());
    if (auto keyConversion = E->getKeyConversion()) {
      OS << '\n';
      printRecLabelled(keyConversion.Conversion, "key_conversion");
    }
    if (auto valueConversion = E->getValueConversion()) {
      OS << '\n';
      printRecLabelled(valueConversion.Conversion, "value_conversion");
    }
    OS << ')';
  }
  void visitDerivedToBaseExpr(DerivedToBaseExpr *E) {
    printCommon(E, "derived_to_base_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitArchetypeToSuperExpr(ArchetypeToSuperExpr *E) {
    printCommon(E, "archetype_to_super_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitLValueToPointerExpr(LValueToPointerExpr *E) {
    printCommon(E, "lvalue_to_pointer") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitInjectIntoOptionalExpr(InjectIntoOptionalExpr *E) {
    printCommon(E, "inject_into_optional") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitClassMetatypeToObjectExpr(ClassMetatypeToObjectExpr *E) {
    printCommon(E, "class_metatype_to_object") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitExistentialMetatypeToObjectExpr(ExistentialMetatypeToObjectExpr *E) {
    printCommon(E, "existential_metatype_to_object") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitProtocolMetatypeToObjectExpr(ProtocolMetatypeToObjectExpr *E) {
    printCommon(E, "protocol_metatype_to_object") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitInOutToPointerExpr(InOutToPointerExpr *E) {
    printCommon(E, "inout_to_pointer") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitArrayToPointerExpr(ArrayToPointerExpr *E) {
    printCommon(E, "array_to_pointer") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitStringToPointerExpr(StringToPointerExpr *E) {
    printCommon(E, "string_to_pointer") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitPointerToPointerExpr(PointerToPointerExpr *E) {
    printCommon(E, "pointer_to_pointer") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitForeignObjectConversionExpr(ForeignObjectConversionExpr *E) {
    printCommon(E, "foreign_object_conversion") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitUnevaluatedInstanceExpr(UnevaluatedInstanceExpr *E) {
    printCommon(E, "unevaluated_instance") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }

  void visitInOutExpr(InOutExpr *E) {
    printCommon(E, "inout_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }

  void visitForceTryExpr(ForceTryExpr *E) {
    printCommon(E, "force_try_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }

  void visitOptionalTryExpr(OptionalTryExpr *E) {
    printCommon(E, "optional_try_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }

  void visitTryExpr(TryExpr *E) {
    printCommon(E, "try_expr");
    OS << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }

  void visitSequenceExpr(SequenceExpr *E) {
    printCommon(E, "sequence_expr");
    for (unsigned i = 0, e = E->getNumElements(); i != e; ++i) {
      OS << '\n';
      printRec(E->getElement(i));
    }
    OS << ')';
  }

  void visitCaptureListExpr(CaptureListExpr *E) {
    printCommon(E, "capture_list");
    for (auto capture : E->getCaptureList()) {
      OS << '\n';
      Indent += 2;
      printRec(capture.Var);
      printRec(capture.Init);
      Indent -= 2;
    }
    printRec(E->getClosureBody());
    OS << ')';
  }

  llvm::raw_ostream &printClosure(AbstractClosureExpr *E, char const *name) {
    printCommon(E, name);
    OS << " discriminator=" << E->getDiscriminator();
    if (!E->getCaptureInfo().isTrivial()) {
      OS << " ";
      E->getCaptureInfo().print(OS);
    }
    
    return OS;
  }

  void visitClosureExpr(ClosureExpr *E) {
    printClosure(E, "closure_expr");
    if (E->hasSingleExpressionBody())
      OS << " single-expression";
    if (E->isVoidConversionClosure())
      OS << " void-conversion";
    
    if (E->getParameters()) {
      OS << '\n';
      PrintDecl(OS, Indent+2).printParameterList(E->getParameters());
    }
    
    OS << '\n';
    if (E->hasSingleExpressionBody()) {
      printRec(E->getSingleExpressionBody());
    } else {
      printRec(E->getBody());
    }
    OS << ')';
  }
  void visitAutoClosureExpr(AutoClosureExpr *E) {
    printClosure(E, "autoclosure_expr") << '\n';
    
    if (E->getParameters()) {
      OS << '\n';
      PrintDecl(OS, Indent+2).printParameterList(E->getParameters());
    }

    OS << '\n';
    printRec(E->getSingleExpressionBody());
    OS << ')';
  }

  void visitDynamicTypeExpr(DynamicTypeExpr *E) {
    printCommon(E, "metatype_expr");
    OS << '\n';
    printRec(E->getBase());
    OS << ')';
  }

  void visitOpaqueValueExpr(OpaqueValueExpr *E) {
    printCommon(E, "opaque_value_expr") << " @ " << (void*)E;
    OS << ')';
  }

  void printArgumentLabels(ArrayRef<Identifier> argLabels) {
    OS << "  arg_labels=";
    for (auto label : argLabels) 
      OS << (label.empty() ? "_" : label.str()) << ":";
  }

  void printApplyExpr(ApplyExpr *E, const char *NodeName) {
    printCommon(E, NodeName);
    if (E->isSuper())
      OS << " super";
    if (E->isThrowsSet())
      OS << (E->throws() ? " throws" : " nothrow");
    if (auto call = dyn_cast<CallExpr>(E))
      printArgumentLabels(call->getArgumentLabels());

    OS << '\n';
    printRec(E->getFn());
    OS << '\n';
    printRec(E->getArg());
    OS << ')';
  }

  void visitCallExpr(CallExpr *E) {
    printApplyExpr(E, "call_expr");
  }
  void visitPrefixUnaryExpr(PrefixUnaryExpr *E) {
    printApplyExpr(E, "prefix_unary_expr");
  }
  void visitPostfixUnaryExpr(PostfixUnaryExpr *E) {
    printApplyExpr(E, "postfix_unary_expr");
  }
  void visitBinaryExpr(BinaryExpr *E) {
    printApplyExpr(E, "binary_expr");
  }
  void visitDotSyntaxCallExpr(DotSyntaxCallExpr *E) {
    printApplyExpr(E, "dot_syntax_call_expr");
  }
  void visitConstructorRefCallExpr(ConstructorRefCallExpr *E) {
    printApplyExpr(E, "constructor_ref_call_expr");
  }
  void visitDotSyntaxBaseIgnoredExpr(DotSyntaxBaseIgnoredExpr *E) {
    printCommon(E, "dot_syntax_base_ignored") << '\n';
    printRec(E->getLHS());
    OS << '\n';
    printRec(E->getRHS());
    OS << ')';
  }

  void printExplicitCastExpr(ExplicitCastExpr *E, const char *name) {
    printCommon(E, name) << ' ';
    if (auto checkedCast = dyn_cast<CheckedCastExpr>(E))
      OS << getCheckedCastKindName(checkedCast->getCastKind()) << ' ';
    OS << "writtenType='";
    E->getCastTypeLoc().getType().print(OS);
    OS << "'\n";
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitForcedCheckedCastExpr(ForcedCheckedCastExpr *E) {
    printExplicitCastExpr(E, "forced_checked_cast_expr");
  }
  void visitConditionalCheckedCastExpr(ConditionalCheckedCastExpr *E) {
    printExplicitCastExpr(E, "conditional_checked_cast_expr");
  }
  void visitIsExpr(IsExpr *E) {
    printExplicitCastExpr(E, "is_subtype_expr");
  }
  void visitCoerceExpr(CoerceExpr *E) {
    printExplicitCastExpr(E, "coerce_expr");
  }
  void visitArrowExpr(ArrowExpr *E) {
    printCommon(E, "arrow") << '\n';
    printRec(E->getArgsExpr());
    OS << '\n';
    printRec(E->getResultExpr());
    OS << ')';
  }
  void visitRebindSelfInConstructorExpr(RebindSelfInConstructorExpr *E) {
    printCommon(E, "rebind_self_in_constructor_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitIfExpr(IfExpr *E) {
    printCommon(E, "if_expr") << '\n';
    printRec(E->getCondExpr());
    OS << '\n';
    printRec(E->getThenExpr());
    OS << '\n';
    printRec(E->getElseExpr());
    OS << ')';
  }
  void visitDefaultValueExpr(DefaultValueExpr *E) {
    printCommon(E, "default_value_expr") << ' ';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitAssignExpr(AssignExpr *E) {
    OS.indent(Indent) << "(assign_expr\n";
    printRec(E->getDest());
    OS << '\n';
    printRec(E->getSrc());
    OS << ')';
  }
  void visitEnumIsCaseExpr(EnumIsCaseExpr *E) {
    printCommon(E, "enum_is_case_expr") << ' ' <<
      E->getEnumElement()->getName() << "\n";
    printRec(E->getSubExpr());
    
  }
  void visitUnresolvedPatternExpr(UnresolvedPatternExpr *E) {
    printCommon(E, "unresolved_pattern_expr") << '\n';
    printRec(E->getSubPattern());
    OS << ')';
  }
  void visitBindOptionalExpr(BindOptionalExpr *E) {
    printCommon(E, "bind_optional_expr")
      << " depth=" << E->getDepth() << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitOptionalEvaluationExpr(OptionalEvaluationExpr *E) {
    printCommon(E, "optional_evaluation_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitForceValueExpr(ForceValueExpr *E) {
    printCommon(E, "force_value_expr") << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitOpenExistentialExpr(OpenExistentialExpr *E) {
    printCommon(E, "open_existential_expr") << '\n';
    printRec(E->getOpaqueValue());
    OS << '\n';
    printRec(E->getExistentialValue());
    OS << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }
  void visitEditorPlaceholderExpr(EditorPlaceholderExpr *E) {
    printCommon(E, "editor_placeholder_expr") << '\n';
    auto *TyR = E->getTypeLoc().getTypeRepr();
    auto *ExpTyR = E->getTypeForExpansion();
    if (TyR)
      printRec(TyR);
    if (ExpTyR && ExpTyR != TyR) {
      OS << '\n';
      printRec(ExpTyR);
    }
    OS << ')';
  }
  void visitObjCSelectorExpr(ObjCSelectorExpr *E) {
    printCommon(E, "objc_selector_expr");
    OS << " kind=";
    switch (E->getSelectorKind()) {
      case ObjCSelectorExpr::Method:
        OS << "method";
        break;
      case ObjCSelectorExpr::Getter:
        OS << "getter";
        break;
      case ObjCSelectorExpr::Setter:
        OS << "setter";
        break;
    }
    OS << " decl=";
    if (auto method = E->getMethod()) {
      method->dumpRef(OS);
    } else {
      OS << "<unresolved>";
    }
    OS << '\n';
    printRec(E->getSubExpr());
    OS << ')';
  }

  void visitObjCKeyPathExpr(ObjCKeyPathExpr *E) {
    printCommon(E, "keypath_expr");
    for (unsigned i = 0, n = E->getNumComponents(); i != n; ++i) {
      OS << "\n";
      OS.indent(Indent + 2);
      OS << "component=";
      if (auto decl = E->getComponentDecl(i))
        decl->dumpRef(OS);
      else
        OS << E->getComponentName(i);
    }
    if (auto semanticE = E->getSemanticExpr()) {
      OS << '\n';
      printRec(semanticE);
    }
    OS << ")";
  }
};

} // end anonymous namespace.


void Expr::dump(raw_ostream &OS) const {
  if (auto ty = getType()) {
    llvm::SaveAndRestore<bool> X(ty->getASTContext().LangOpts.
                                 DebugConstraintSolver, true);
  
    print(OS);
  } else {
    print(OS);
  }
  OS << '\n';
}

void Expr::dump() const {
  dump(llvm::errs());
}

void Expr::print(raw_ostream &OS, unsigned Indent) const {
  PrintExpr(OS, Indent).visit(const_cast<Expr*>(this));
}

void Expr::print(ASTPrinter &Printer, const PrintOptions &Opts) const {
  // FIXME: Fully use the ASTPrinter.
  llvm::SmallString<128> Str;
  llvm::raw_svector_ostream OS(Str);
  print(OS);
  Printer << OS.str();
}

//===----------------------------------------------------------------------===//
// Printing for TypeRepr and all subclasses.
//===----------------------------------------------------------------------===//

namespace {
class PrintTypeRepr : public TypeReprVisitor<PrintTypeRepr> {
public:
  raw_ostream &OS;
  unsigned Indent;
  bool ShowColors;

  PrintTypeRepr(raw_ostream &os, unsigned indent)
    : OS(os), Indent(indent), ShowColors(false) {
    if (&os == &llvm::errs() || &os == &llvm::outs())
      ShowColors = llvm::errs().has_colors() && llvm::outs().has_colors();
  }

  void printRec(Decl *D) { D->dump(OS, Indent + 2); }
  void printRec(Expr *E) { E->print(OS, Indent + 2); }
  void printRec(TypeRepr *T) { PrintTypeRepr(OS, Indent + 2).visit(T); }

  raw_ostream &printCommon(TypeRepr *T, const char *Name) {
    OS.indent(Indent) << '(';

    // Support optional color output.
    if (ShowColors) {
      if (const char *CStr =
          llvm::sys::Process::OutputColor(TypeReprColor, false, false)) {
        OS << CStr;
      }
    }

    OS << Name;

    if (ShowColors)
      OS.resetColor();
    return OS;
  }

  void visitErrorTypeRepr(ErrorTypeRepr *T) {
    printCommon(T, "type_error");
  }

  void visitAttributedTypeRepr(AttributedTypeRepr *T) {
    printCommon(T, "type_attributed") << " attrs=";
    T->printAttrs(OS);
    OS << '\n';
    printRec(T->getTypeRepr());
  }

  void visitIdentTypeRepr(IdentTypeRepr *T) {
    printCommon(T, "type_ident");
    Indent += 2;
    for (auto comp : T->getComponentRange()) {
      OS << '\n';
      printCommon(nullptr, "component");
      OS << " id='" << comp->getIdentifier() << '\'';
      OS << " bind=";
      if (comp->isBound())
        comp->getBoundDecl()->dumpRef(OS);
      else OS << "none";
      OS << ')';
      if (auto GenIdT = dyn_cast<GenericIdentTypeRepr>(comp)) {
        for (auto genArg : GenIdT->getGenericArgs()) {
          OS << '\n';
          printRec(genArg);
        }
      }
    }
    OS << ')';
    Indent -= 2;
  }

  void visitFunctionTypeRepr(FunctionTypeRepr *T) {
    printCommon(T, "type_function");
    OS << '\n'; printRec(T->getArgsTypeRepr());
    if (T->throws())
      OS << " throws ";
    OS << '\n'; printRec(T->getResultTypeRepr());
    OS << ')';
  }

  void visitArrayTypeRepr(ArrayTypeRepr *T) {
    printCommon(T, "type_array") << '\n';
    printRec(T->getBase());
    OS << ')';
  }

  void visitDictionaryTypeRepr(DictionaryTypeRepr *T) {
    printCommon(T, "type_dictionary") << '\n';
    printRec(T->getKey());
    OS << '\n';
    printRec(T->getValue());
    OS << ')';
  }

  void visitTupleTypeRepr(TupleTypeRepr *T) {
    printCommon(T, "type_tuple");
    for (auto elem : T->getElements()) {
      OS << '\n';
      printRec(elem);
    }
    OS << ')';
  }

  void visitNamedTypeRepr(NamedTypeRepr *T) {
    printCommon(T, "type_named");
    if (T->hasName())
      OS << " id=" << T->getName();
    if (T->getTypeRepr()) {
      OS << '\n';
      printRec(T->getTypeRepr());
    }
    OS << ')';
  }

  void visitProtocolCompositionTypeRepr(ProtocolCompositionTypeRepr *T) {
    printCommon(T, "type_composite");
    for (auto elem : T->getProtocols()) {
      OS << '\n';
      printRec(elem);
    }
    OS << ')';
  }

  void visitMetatypeTypeRepr(MetatypeTypeRepr *T) {
    printCommon(T, "type_metatype") << '\n';
    printRec(T->getBase());
    OS << ')';
  }
  
  void visitInOutTypeRepr(InOutTypeRepr *T) {
    printCommon(T, "type_inout") << '\n';
    printRec(T->getBase());
    OS << ')';
  }
};

} // end anonymous namespace.

void PrintDecl::printRec(TypeRepr *T) {
  PrintTypeRepr(OS, Indent+2).visit(T);
}

void PrintExpr::printRec(TypeRepr *T) {
  PrintTypeRepr(OS, Indent+2).visit(T);
}

void PrintPattern::printRec(TypeRepr *T) {
  PrintTypeRepr(OS, Indent+2).visit(T);
}

void TypeRepr::dump() const {
  PrintTypeRepr(llvm::errs(), 0).visit(const_cast<TypeRepr*>(this));
  llvm::errs() << '\n';
}

void Substitution::dump() const {
  dump(llvm::errs());
}

void Substitution::dump(llvm::raw_ostream &out, unsigned indent) const {
  out.indent(indent);
  print(out);
  out << '\n';

  for (auto &c : Conformance) {
    c.dump(out, indent + 2);
  }
}

void ProtocolConformanceRef::dump() const {
  dump(llvm::errs());
}

void ProtocolConformanceRef::dump(llvm::raw_ostream &out,
                                  unsigned indent) const {
  if (isConcrete()) {
    getConcrete()->dump(out, indent);
  } else {
    out.indent(indent) << "(abstract_conformance protocol="
                       << getAbstract()->getName() << ')';
  }
}

void swift::dump(const ArrayRef<Substitution> &subs) {
  unsigned i = 0;
  for (const auto &s : subs) {
    llvm::errs() << i++ << ": ";
    s.dump();
  }
}

void ProtocolConformance::dump() const {
  auto &out = llvm::errs();
  dump(out);
  out << '\n';
}

void ProtocolConformance::dump(llvm::raw_ostream &out, unsigned indent) const {
  auto printCommon = [&](StringRef kind) {
    out.indent(indent) << '(' << kind << "_conformance type=" << getType()
                       << " protocol=" << getProtocol()->getName();
  };

  switch (getKind()) {
  case ProtocolConformanceKind::Normal:
    printCommon("normal");
    // Maybe print information about the conforming context?
    break;

  case ProtocolConformanceKind::Inherited: {
    auto conf = cast<InheritedProtocolConformance>(this);
    printCommon("inherited");
    out << '\n';
    conf->getInheritedConformance()->dump(out, indent + 2);
    break;
  }

  case ProtocolConformanceKind::Specialized: {
    auto conf = cast<SpecializedProtocolConformance>(this);
    printCommon("specialized");
    out << '\n';
    for (auto sub : conf->getGenericSubstitutions()) {
      sub.dump(out, indent + 2);
      out << '\n';
    }
    conf->getGenericConformance()->dump(out, indent + 2);
    break;
  }
  }

  out << ')';
}

//===----------------------------------------------------------------------===//
// Dumping for Types.
//===----------------------------------------------------------------------===//

namespace {
  class PrintType : public TypeVisitor<PrintType, void, StringRef> {
    raw_ostream &OS;
    unsigned Indent;

    raw_ostream &printCommon(const TypeBase *T, StringRef label,
                             StringRef name) {
      OS.indent(Indent) << '(';
      if (!label.empty()) {
        PrintWithColorRAII(OS, TypeFieldColor) << label;
        OS << "=";
      }

      PrintWithColorRAII(OS, TypeColor) << name;
      return OS;
    }

    // Print a single flag.
    raw_ostream &printFlag(StringRef name) {
      PrintWithColorRAII(OS, TypeFieldColor) << " " << name;
      return OS;
    }

    // Print a single flag if it is set.
    raw_ostream &printFlag(bool isSet, StringRef name) {
      if (isSet)
        printFlag(name);

      return OS;
    }

    // Print a field with a value.
    template<typename T>
    raw_ostream &printField(StringRef name, const T &value) {
      OS << " ";
      PrintWithColorRAII(OS, TypeFieldColor) << name;
      OS << "=" << value;
      return OS;
    }

  public:
    PrintType(raw_ostream &os, unsigned indent) : OS(os), Indent(indent) { }

    void printRec(Type type) {
      printRec("", type);
    }

    void printRec(StringRef label, Type type) {
      OS << "\n";

      if (type.isNull())
        OS << "<<null>>";
      else {
        Indent += 2;
        visit(type, label);
        Indent -=2;
      }
    }

#define TRIVIAL_TYPE_PRINTER(Class,Name)                        \
    void visit##Class##Type(Class##Type *T, StringRef label) {  \
      printCommon(T, label, #Name "_type") << ")";              \
    }

    TRIVIAL_TYPE_PRINTER(Error, error)
    TRIVIAL_TYPE_PRINTER(Unresolved, unresolved)

    void visitBuiltinIntegerType(BuiltinIntegerType *T, StringRef label) {
      printCommon(T, label, "builtin_integer_type");
      if (T->isFixedWidth())
        printField("bit_width", T->getFixedWidth());
      else
        printFlag("word_sized");
      OS << ")";
    }

    void visitBuiltinFloatType(BuiltinFloatType *T, StringRef label) {
      printCommon(T, label, "builtin_float_type");
      printField("bit_width", T->getBitWidth());
      OS << ")";
    }

    TRIVIAL_TYPE_PRINTER(BuiltinRawPointer, builtin_raw_pointer)
    TRIVIAL_TYPE_PRINTER(BuiltinNativeObject, builtin_native_object)
    TRIVIAL_TYPE_PRINTER(BuiltinBridgeObject, builtin_bridge_object)
    TRIVIAL_TYPE_PRINTER(BuiltinUnknownObject, builtin_unknown_object)
    TRIVIAL_TYPE_PRINTER(BuiltinUnsafeValueBuffer, builtin_unsafe_value_buffer)

    void visitBuiltinVectorType(BuiltinVectorType *T, StringRef label) {
      printCommon(T, label, "builtin_vector_type");
      printField("num_elements", T->getNumElements());
      printRec(T->getElementType());
      OS << ")";
    }

    void visitNameAliasType(NameAliasType *T, StringRef label) {
      printCommon(T, label, "name_alias_type");
      printField("decl", T->getDecl()->printRef());
      OS << ")";
    }

    void visitParenType(ParenType *T, StringRef label) {
      printCommon(T, label, "paren_type");
      printRec(T->getUnderlyingType());
      OS << ")";
    }

    void visitTupleType(TupleType *T, StringRef label) {
      printCommon(T, label, "tuple_type");
      printField("num_elements", T->getNumElements());
      Indent += 2;
      for (const auto &elt : T->getElements()) {
        OS << "\n";
        OS.indent(Indent) << "(";
        PrintWithColorRAII(OS, TypeFieldColor) << "tuple_type_elt";
        if (elt.hasName())
          printField("name", elt.getName().str());
        if (elt.isVararg())
          printFlag("vararg");

        printRec(elt.getType());
        OS << ")";
      }
      Indent -= 2;
      OS << ")";
    }

    void visitUnownedStorageType(UnownedStorageType *T, StringRef label) {
      printCommon(T, label, "unowned_storage_type");
      printRec(T->getReferentType());
      OS << ")";
    }

    void visitUnmanagedStorageType(UnmanagedStorageType *T, StringRef label) {
      printCommon(T, label, "unmanaged_storage_type");
      printRec(T->getReferentType());
      OS << ")";
    }

    void visitWeakStorageType(WeakStorageType *T, StringRef label) {
      printCommon(T, label, "weak_storage_type");
      printRec(T->getReferentType());
      OS << ")";
    }

    void visitEnumType(EnumType *T, StringRef label) {
      printCommon(T, label, "enum_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      OS << ")";
    }

    void visitStructType(StructType *T, StringRef label) {
      printCommon(T, label, "struct_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      OS << ")";
    }

    void visitClassType(ClassType *T, StringRef label) {
      printCommon(T, label, "class_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      OS << ")";
    }

    void visitProtocolType(ProtocolType *T, StringRef label) {
      printCommon(T, label, "protocol_type");
      printField("decl", T->getDecl()->printRef());
      OS << ")";
    }

    void visitMetatypeType(MetatypeType *T, StringRef label) {
      printCommon(T, label, "metatype_type");
      if (T->hasRepresentation()) {
        OS << " ";
        switch (T->getRepresentation()) {
        case MetatypeRepresentation::Thin:
          OS << "@thin";
          break;
        case MetatypeRepresentation::Thick:
          OS << "@thick";
          break;
        case MetatypeRepresentation::ObjC:
          OS << "@objc";
          break;
        }
      }
      printRec(T->getInstanceType());
      OS << ")";
    }

    void visitExistentialMetatypeType(ExistentialMetatypeType *T,
                                      StringRef label) {
      printCommon(T, label, "existential_metatype_type");
      printRec(T->getInstanceType());
      OS << ")";
    }

    void visitModuleType(ModuleType *T, StringRef label) {
      printCommon(T, label, "module_type");
      printField("module", T->getModule()->getName());
      OS << ")";
    }

    void visitDynamicSelfType(DynamicSelfType *T, StringRef label) {
      printCommon(T, label, "dynamic_self_type");
      printRec(T->getSelfType());
      OS << ")";
    }

    void visitArchetypeType(ArchetypeType *T, StringRef label) {
      printCommon(T, label, "archetype_type");
      if (T->getOpenedExistentialType())
        printField("opened_existential_id", T->getOpenedExistentialID());
      else
        printField("name", T->getFullName());
      printField("address", static_cast<void *>(T));
      printFlag(T->requiresClass(), "class");
      for (auto proto : T->getConformsTo())
        printField("conforms_to", proto->printRef());
      if (auto parent = T->getParent())
        printField("parent", static_cast<void *>(parent));
      if (auto assocType = T->getAssocType())
        printField("assoc_type", assocType->printRef());
      if (auto selfProto = T->getSelfProtocol())
        printField("self_proto", selfProto->printRef());

      // FIXME: This is ugly.
      OS << "\n";
      T->getASTContext().dumpArchetypeContext(T, OS, Indent + 2);

      if (auto superclass = T->getSuperclass())
        printRec("superclass", superclass);
      if (auto openedExistential = T->getOpenedExistentialType())
        printRec("opened_existential", openedExistential);

      Indent += 2;
      for (auto nestedType : T->getNestedTypes(/*resolveTypes=*/false)) {
        OS << "\n";
        OS.indent(Indent) << "(";
        PrintWithColorRAII(OS, TypeFieldColor) << "nested_type";
        OS << "=";
        OS << nestedType.first.str() << " ";
        if (!nestedType.second) {
          PrintWithColorRAII(OS, TypeColor) << "unresolved";          
        } else if (auto concrete = nestedType.second.getAsConcreteType()) {
          PrintWithColorRAII(OS, TypeColor) << "concrete";
          OS << "=" << concrete.getString();
        } else {
          PrintWithColorRAII(OS, TypeColor) << "archetype";
          OS << "=" << static_cast<void *>(nestedType.second.getAsArchetype());
        }
        OS << ")";
      }
      Indent -= 2;

      OS << ")";
    }

    void visitGenericTypeParamType(GenericTypeParamType *T, StringRef label) {
      printCommon(T, label, "generic_type_param_type");
      printField("depth", T->getDepth());
      printField("index", T->getIndex());
      if (auto decl = T->getDecl())
        printField("decl", decl->printRef());
      OS << ")";
    }

    void visitAssociatedTypeType(AssociatedTypeType *T, StringRef label) {
      printCommon(T, label, "associated_type_type");
      printField("decl", T->getDecl()->printRef());
      OS << ")";
    }

    void visitSubstitutedType(SubstitutedType *T, StringRef label) {
      printCommon(T, label, "substituted_type");
      printRec("original", T->getOriginal());
      printRec("replacement", T->getReplacementType());
      OS << ")";
    }

    void visitDependentMemberType(DependentMemberType *T, StringRef label) {
      printCommon(T, label, "dependent_member_type");
      if (auto assocType = T->getAssocType()) {
        printField("assoc_type", assocType->printRef());
      } else {
        printField("name", T->getName().str());
      }
      printRec("base", T->getBase());
      OS << ")";
    }

    void printAnyFunctionTypeCommon(AnyFunctionType *T, StringRef label,
                                    StringRef name) {
      printCommon(T, label, name);

      switch (T->getExtInfo().getSILRepresentation()) {
      case SILFunctionType::Representation::Thick:
        break;

      case SILFunctionType::Representation::Block:
        printField("representation", "block");
        break;

      case SILFunctionType::Representation::CFunctionPointer:
        printField("representation", "c");
        break;

      case SILFunctionType::Representation::Thin:
        printField("representation", "thin");
        break;

      case SILFunctionType::Representation::Method:
        printField("representation", "method");
        break;
        
      case SILFunctionType::Representation::ObjCMethod:
        printField("representation", "objc_method");
        break;
        
      case SILFunctionType::Representation::WitnessMethod:
        printField("representation", "witness_method");
        break;
      }

      printFlag(T->isAutoClosure(), "autoclosure");

      // Dump out either @noescape or @escaping
      printFlag(!T->isNoEscape(), "@escaping");

      printFlag(T->throws(), "throws");

      printRec("input", T->getInput());
      printRec("output", T->getResult());
    }

    void visitFunctionType(FunctionType *T, StringRef label) {
      printAnyFunctionTypeCommon(T, label, "function_type");
      OS << ")";
    }

    void visitPolymorphicFunctionType(PolymorphicFunctionType *T,
                                      StringRef label) {
      printAnyFunctionTypeCommon(T, label, "polymorphic_function_type");
      // FIXME: generic parameters
      OS << ")";
    }

    void visitGenericFunctionType(GenericFunctionType *T, StringRef label) {
      printAnyFunctionTypeCommon(T, label, "generic_function_type");
      // FIXME: generic signature dumping needs improvement
      OS << "\n";
      OS.indent(Indent + 2) << "(";
      printField("generic_sig", T->getGenericSignature()->getAsString());
      OS << ")";
      OS << ")";
    }

    void visitSILFunctionType(SILFunctionType *T, StringRef label) {
      printCommon(T, label, "sil_function_type");
      // FIXME: Make this useful.
      printField("type", T->getString());
      OS << ")";
    }

    void visitSILBlockStorageType(SILBlockStorageType *T, StringRef label) {
      printCommon(T, label, "sil_block_storage_type");
      printRec(T->getCaptureType());
      OS << ")";
    }

    void visitSILBoxType(SILBoxType *T, StringRef label) {
      printCommon(T, label, "sil_box_type");
      printRec(T->getBoxedType());
      OS << ")";
    }

    void visitArraySliceType(ArraySliceType *T, StringRef label) {
      printCommon(T, label, "array_slice_type");
      printRec(T->getBaseType());
      OS << ")";
    }

    void visitOptionalType(OptionalType *T, StringRef label) {
      printCommon(T, label, "optional_type");
      printRec(T->getBaseType());
      OS << ")";
    }

    void visitImplicitlyUnwrappedOptionalType(
           ImplicitlyUnwrappedOptionalType *T, StringRef label) {
      printCommon(T, label, "implicitly_unwrapped_optional_type");
      printRec(T->getBaseType());
      OS << ")";
    }

    void visitDictionaryType(DictionaryType *T, StringRef label) {
      printCommon(T, label, "dictionary_type");
      printRec("key", T->getKeyType());
      printRec("value", T->getValueType());
      OS << ")";
    }

    void visitProtocolCompositionType(ProtocolCompositionType *T,
                                      StringRef label) {
      printCommon(T, label, "protocol_composition_type");
      for (auto proto : T->getProtocols()) {
        printRec(proto);
      }
      OS << ")";
    }

    void visitLValueType(LValueType *T, StringRef label) {
      printCommon(T, label, "lvalue_type");
      printRec(T->getObjectType());
      OS << ")";
    }

    void visitInOutType(InOutType *T, StringRef label) {
      printCommon(T, label, "inout_type");
      printRec(T->getObjectType());
      OS << ")";
    }

    void visitUnboundGenericType(UnboundGenericType *T, StringRef label) {
      printCommon(T, label, "unbound_generic_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      OS << ")";
    }

    void visitBoundGenericClassType(BoundGenericClassType *T, StringRef label) {
      printCommon(T, label, "bound_generic_class_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      OS << ")";
    }

    void visitBoundGenericStructType(BoundGenericStructType *T,
                                     StringRef label) {
      printCommon(T, label, "bound_generic_struct_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      OS << ")";
    }

    void visitBoundGenericEnumType(BoundGenericEnumType *T, StringRef label) {
      printCommon(T, label, "bound_generic_enum_type");
      printField("decl", T->getDecl()->printRef());
      if (T->getParent())
        printRec("parent", T->getParent());
      for (auto arg : T->getGenericArgs())
        printRec(arg);
      OS << ")";
    }

    void visitTypeVariableType(TypeVariableType *T, StringRef label) {
      printCommon(T, label, "type_variable_type");
      printField("id", T->getID());
      OS << ")";
    }

#undef TRIVIAL_TYPE_PRINTER
  };
}

void Type::dump() const {
  // Make sure to print type variables.
  dump(llvm::errs());
}

void Type::dump(raw_ostream &os, unsigned indent) const {
  // Make sure to print type variables.
  llvm::SaveAndRestore<bool> X(getPointer()->getASTContext().LangOpts.
                               DebugConstraintSolver, true);
  PrintType(os, indent).visit(*this, "");
  os << "\n";
}

void TypeBase::dump() const {
  // Make sure to print type variables.
  Type(const_cast<TypeBase *>(this)).dump();
}

void TypeBase::dump(raw_ostream &os, unsigned indent) const {
  auto &ctx = const_cast<TypeBase*>(this)->getASTContext();
  
  // Make sure to print type variables.
  llvm::SaveAndRestore<bool> X(ctx.LangOpts.DebugConstraintSolver, true);
  Type(const_cast<TypeBase *>(this)).dump(os, indent);
}
