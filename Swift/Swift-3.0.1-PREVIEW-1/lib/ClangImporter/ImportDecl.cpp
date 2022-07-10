//===--- ImportDecl.cpp - Import Clang Declarations -----------------------===//
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
// This file implements support for importing Clang declarations into Swift.
//
//===----------------------------------------------------------------------===//

#include "CFTypeInfo.h"
#include "ImporterImpl.h"
#include "swift/Strings.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Attr.h"
#include "swift/AST/Builtins.h"
#include "swift/AST/Decl.h"
#include "swift/AST/DiagnosticsClangImporter.h"
#include "swift/AST/Expr.h"
#include "swift/AST/Module.h"
#include "swift/AST/NameLookup.h"
#include "swift/AST/ParameterList.h"
#include "swift/AST/Pattern.h"
#include "swift/AST/Stmt.h"
#include "swift/AST/Types.h"
#include "swift/Basic/Fallthrough.h"
#include "swift/ClangImporter/ClangModule.h"
#include "swift/Parse/Lexer.h"
#include "swift/Config.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Lookup.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Path.h"

#include <algorithm>

#define DEBUG_TYPE "Clang module importer"

STATISTIC(NumTotalImportedEntities, "# of imported clang entities");
STATISTIC(NumFactoryMethodsAsInitializers,
          "# of factory methods mapped to initializers");

using namespace swift;
using namespace importer;

namespace swift {
namespace inferred_attributes {
  enum {
    requires_stored_property_inits = 0x01
  };
}
}

namespace {
enum class MakeStructRawValuedFlags {
  /// whether to also create an unlabeled init
  MakeUnlabeledValueInit = 0x01,

  /// whether the raw value should be a let
  IsLet = 0x02,

  /// whether to mark the rawValue as implicit
  IsImplicit = 0x04,
};
using MakeStructRawValuedOptions = OptionSet<MakeStructRawValuedFlags>;
}

static MakeStructRawValuedOptions
getDefaultMakeStructRawValuedOptions() {
  MakeStructRawValuedOptions opts;
  opts -= MakeStructRawValuedFlags::MakeUnlabeledValueInit; // default off
  opts |= MakeStructRawValuedFlags::IsLet;                  // default on
  opts |= MakeStructRawValuedFlags::IsImplicit;             // default on
  return opts;
}

static bool isInSystemModule(DeclContext *D) {
  if (cast<ClangModuleUnit>(D->getModuleScopeContext())->isSystemModule())
    return true;
  return false;
}

static Accessibility getOverridableAccessibility(DeclContext *dc) {
  return (dc->getAsProtocolOrProtocolExtensionContext()
            ? Accessibility::Public : Accessibility::Open);
}

/// Create a typedpattern(namedpattern(decl))
static Pattern *createTypedNamedPattern(VarDecl *decl) {
  ASTContext &Ctx = decl->getASTContext();
  Type ty = decl->getType();

  Pattern *P = new (Ctx) NamedPattern(decl);
  P->setType(ty);
  P->setImplicit();
  P = new (Ctx) TypedPattern(P, TypeLoc::withoutLoc(ty));
  P->setType(ty);
  P->setImplicit();
  return P;
}

/// Create a var member for this struct, along with its pattern binding, and add
/// it as a member
static std::pair<VarDecl *, PatternBindingDecl *>
createVarWithPattern(ASTContext &cxt, DeclContext *dc, Identifier name, Type ty,
                     bool isLet, bool isImplicit,
                     Accessibility setterAccessibility) {
  // Create a variable to store the underlying value.
  auto var = new (cxt) VarDecl(
      /*static*/ false,
      /*IsLet*/ isLet, SourceLoc(), name, ty, dc);
  if (isImplicit)
    var->setImplicit();
  var->setAccessibility(Accessibility::Public);
  var->setSetterAccessibility(setterAccessibility);

  // Create a pattern binding to describe the variable.
  Pattern *varPattern = createTypedNamedPattern(var);
  auto patternBinding =
      PatternBindingDecl::create(cxt, SourceLoc(), StaticSpellingKind::None,
                                 SourceLoc(), varPattern, nullptr, dc);

  return {var, patternBinding};
}

#ifndef NDEBUG
static bool verifyNameMapping(MappedTypeNameKind NameMapping,
                              StringRef left, StringRef right) {
  return NameMapping == MappedTypeNameKind::DoNothing || left != right;
}
#endif

/// \brief Map a well-known C type to a swift type from the standard library.
///
/// \param IsError set to true when we know the corresponding swift type name,
/// but we could not find it.  (For example, the type was not defined in the
/// standard library or the required standard library module was not imported.)
/// This should be a hard error, we don't want to map the type only sometimes.
///
/// \returns A pair of a swift type and its name that corresponds to a given
/// C type.
static std::pair<Type, StringRef>
getSwiftStdlibType(const clang::TypedefNameDecl *D,
                   Identifier Name,
                   ClangImporter::Implementation &Impl,
                   bool *IsError, MappedTypeNameKind &NameMapping) {
  *IsError = false;

  MappedCTypeKind CTypeKind;
  unsigned Bitwidth;
  StringRef SwiftModuleName;
  bool IsSwiftModule; // True if SwiftModuleName == STDLIB_NAME.
  StringRef SwiftTypeName;
  bool CanBeMissing;

  do {
#define MAP_TYPE(C_TYPE_NAME, C_TYPE_KIND, C_TYPE_BITWIDTH,        \
                 SWIFT_MODULE_NAME, SWIFT_TYPE_NAME,               \
                 CAN_BE_MISSING, C_NAME_MAPPING)                   \
    if (Name.str() == C_TYPE_NAME) {                               \
      CTypeKind = MappedCTypeKind::C_TYPE_KIND;                    \
      Bitwidth = C_TYPE_BITWIDTH;                                  \
      if (StringRef(SWIFT_MODULE_NAME) == StringRef(STDLIB_NAME))  \
        IsSwiftModule = true;                                      \
      else {                                                       \
        IsSwiftModule = false;                                     \
        SwiftModuleName = SWIFT_MODULE_NAME;                       \
      }                                                            \
      SwiftTypeName = SWIFT_TYPE_NAME;                             \
      CanBeMissing = CAN_BE_MISSING;                               \
      NameMapping = MappedTypeNameKind::C_NAME_MAPPING;            \
      assert(verifyNameMapping(MappedTypeNameKind::C_NAME_MAPPING, \
                               C_TYPE_NAME, SWIFT_TYPE_NAME) &&    \
             "MappedTypes.def: Identical names must use DoNothing"); \
      break;                                                       \
    }
#include "MappedTypes.def"

    // We did not find this type, thus it is not mapped.
    return std::make_pair(Type(), "");
  } while (0);

  clang::ASTContext &ClangCtx = Impl.getClangASTContext();

  auto ClangType = D->getUnderlyingType();

  // If the C type does not have the expected size, don't import it as a stdlib
  // type.
  unsigned ClangTypeSize = ClangCtx.getTypeSize(ClangType);
  if (Bitwidth != 0 && Bitwidth != ClangTypeSize)
    return std::make_pair(Type(), "");

  // Check other expected properties of the C type.
  switch(CTypeKind) {
  case MappedCTypeKind::UnsignedInt:
    if (!ClangType->isUnsignedIntegerType())
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::SignedInt:
    if (!ClangType->isSignedIntegerType())
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::UnsignedWord:
    if (ClangTypeSize != 64 && ClangTypeSize != 32)
      return std::make_pair(Type(), "");
    if (!ClangType->isUnsignedIntegerType())
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::SignedWord:
    if (ClangTypeSize != 64 && ClangTypeSize != 32)
      return std::make_pair(Type(), "");
    if (!ClangType->isSignedIntegerType())
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::FloatIEEEsingle:
  case MappedCTypeKind::FloatIEEEdouble:
  case MappedCTypeKind::FloatX87DoubleExtended: {
    if (!ClangType->isFloatingType())
      return std::make_pair(Type(), "");

    const llvm::fltSemantics &Sem = ClangCtx.getFloatTypeSemantics(ClangType);
    switch(CTypeKind) {
    case MappedCTypeKind::FloatIEEEsingle:
      assert(Bitwidth == 32 && "FloatIEEEsingle should be 32 bits wide");
      if (&Sem != &APFloat::IEEEsingle)
        return std::make_pair(Type(), "");
      break;

    case MappedCTypeKind::FloatIEEEdouble:
      assert(Bitwidth == 64 && "FloatIEEEdouble should be 64 bits wide");
      if (&Sem != &APFloat::IEEEdouble)
        return std::make_pair(Type(), "");
      break;

    case MappedCTypeKind::FloatX87DoubleExtended:
      assert(Bitwidth == 80 && "FloatX87DoubleExtended should be 80 bits wide");
      if (&Sem != &APFloat::x87DoubleExtended)
        return std::make_pair(Type(), "");
      break;

    default:
      llvm_unreachable("should see only floating point types here");
    }
    }
    break;

  case MappedCTypeKind::VaList:
    if (ClangTypeSize != ClangCtx.getTypeSize(ClangCtx.VoidPtrTy))
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::ObjCBool:
    if (!ClangCtx.hasSameType(ClangType, ClangCtx.ObjCBuiltinBoolTy) &&
        !(ClangCtx.getBOOLDecl() &&
          ClangCtx.hasSameType(ClangType, ClangCtx.getBOOLType())))
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::ObjCSel:
    if (!ClangCtx.hasSameType(ClangType, ClangCtx.getObjCSelType()) &&
        !ClangCtx.hasSameType(ClangType,
                              ClangCtx.getObjCSelRedefinitionType()))
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::ObjCId:
    if (!ClangCtx.hasSameType(ClangType, ClangCtx.getObjCIdType()) &&
        !ClangCtx.hasSameType(ClangType,
                              ClangCtx.getObjCIdRedefinitionType()))
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::ObjCClass:
    if (!ClangCtx.hasSameType(ClangType, ClangCtx.getObjCClassType()) &&
        !ClangCtx.hasSameType(ClangType,
                              ClangCtx.getObjCClassRedefinitionType()))
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::CGFloat:
    if (!ClangType->isFloatingType())
      return std::make_pair(Type(), "");
    break;

  case MappedCTypeKind::Block:
    if (!ClangType->isBlockPointerType())
      return std::make_pair(Type(), "");
    break;
  }

  Module *M;
  if (IsSwiftModule)
    M = Impl.getStdlibModule();
  else
    M = Impl.getNamedModule(SwiftModuleName);
  if (!M) {
    // User did not import the library module that contains the type we want to
    // substitute.
    *IsError = true;
    return std::make_pair(Type(), "");
  }

  Type SwiftType = Impl.getNamedSwiftType(M, SwiftTypeName);
  if (!SwiftType && !CanBeMissing) {
    // The required type is not defined in the standard library.
    *IsError = true;
    return std::make_pair(Type(), "");
  }
  return std::make_pair(SwiftType, SwiftTypeName);
}

static bool isNSDictionaryMethod(const clang::ObjCMethodDecl *MD,
                                 clang::Selector cmd) {
  if (MD->getSelector() != cmd)
    return false;
  if (isa<clang::ObjCProtocolDecl>(MD->getDeclContext()))
    return false;
  if (MD->getClassInterface()->getName() != "NSDictionary")
    return false;
  return true;
}

// Build the init(rawValue:) initializer for an imported NS_ENUM.
//   enum NSSomeEnum: RawType {
//     init?(rawValue: RawType) {
//       self = Builtin.reinterpretCast(rawValue)
//     }
//   }
// Unlike a standard init(rawValue:) enum initializer, this does a reinterpret
// cast in order to preserve unknown or future cases from C.
static ConstructorDecl *
makeEnumRawValueConstructor(ClangImporter::Implementation &Impl,
                            EnumDecl *enumDecl) {
  ASTContext &C = Impl.SwiftContext;
  auto enumTy = enumDecl->getDeclaredTypeInContext();
  auto metaTy = MetatypeType::get(enumTy);
  
  auto selfDecl = ParamDecl::createSelf(SourceLoc(), enumDecl,
                                        /*static*/false, /*inout*/true);

  auto param = new (C) ParamDecl(/*let*/ true, SourceLoc(),
                                 SourceLoc(), C.Id_rawValue,
                                 SourceLoc(), C.Id_rawValue,
                                 enumDecl->getRawType(),
                                 enumDecl);
  auto paramPL = ParameterList::createWithoutLoc(param);
  
  DeclName name(C, C.Id_init, paramPL);
  auto *ctorDecl =
    new (C) ConstructorDecl(name, enumDecl->getLoc(),
                            OTK_Optional, /*FailabilityLoc=*/SourceLoc(),
                            /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                            selfDecl, paramPL,
                            /*GenericParams=*/nullptr, enumDecl);
  ctorDecl->setImplicit();
  ctorDecl->setAccessibility(Accessibility::Public);

  auto optEnumTy = OptionalType::get(enumTy);

  auto fnTy = FunctionType::get(paramPL->getType(C), optEnumTy);
  auto allocFnTy = FunctionType::get(metaTy, fnTy);
  auto initFnTy = FunctionType::get(enumTy, fnTy);
  ctorDecl->setType(allocFnTy);
  ctorDecl->setInterfaceType(allocFnTy);
  ctorDecl->setInitializerInterfaceType(initFnTy);

  // Don't bother synthesizing the body if we've already finished type-checking.
  if (Impl.hasFinishedTypeChecking())
    return ctorDecl;
  
  auto selfRef = new (C) DeclRefExpr(selfDecl, DeclNameLoc(), /*implicit*/true);
  auto paramRef = new (C) DeclRefExpr(param, DeclNameLoc(),
                                      /*implicit*/ true);
  auto reinterpretCast
    = cast<FuncDecl>(getBuiltinValueDecl(C,C.getIdentifier("reinterpretCast")));
  auto reinterpretCastRef
    = new (C) DeclRefExpr(reinterpretCast, DeclNameLoc(), /*implicit*/ true);
  auto reinterpreted = CallExpr::createImplicit(C, reinterpretCastRef,
                                                { paramRef }, { Identifier() });
  auto assign = new (C) AssignExpr(selfRef, SourceLoc(), reinterpreted,
                                   /*implicit*/ true);
  auto body = BraceStmt::create(C, SourceLoc(), ASTNode(assign), SourceLoc(),
                                /*implicit*/ true);
  
  ctorDecl->setBody(body);
  
  C.addExternalDecl(ctorDecl);
  
  return ctorDecl;
}

// Build the rawValue getter for an imported NS_ENUM.
//   enum NSSomeEnum: RawType {
//     var rawValue: RawType {
//       return Builtin.reinterpretCast(self)
//     }
//   }
// Unlike a standard init(rawValue:) enum initializer, this does a reinterpret
// cast in order to preserve unknown or future cases from C.
static FuncDecl *makeEnumRawValueGetter(ClangImporter::Implementation &Impl,
                                        EnumDecl *enumDecl,
                                        VarDecl *rawValueDecl) {
  ASTContext &C = Impl.SwiftContext;
  
  auto selfDecl = ParamDecl::createSelf(SourceLoc(), enumDecl);
  
  ParameterList *params[] = {
    ParameterList::createWithoutLoc(selfDecl),
    ParameterList::createEmpty(C)
  };

  auto getterDecl =
    FuncDecl::create(C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
                     /*FuncLoc=*/SourceLoc(), DeclName(), /*NameLoc=*/SourceLoc(),
                     /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                     /*AccessorKeywordLoc=*/SourceLoc(),
                     /*GenericParams=*/nullptr, params, Type(),
                     TypeLoc::withoutLoc(enumDecl->getRawType()), enumDecl);
  getterDecl->setImplicit();

  auto type = ParameterList::getFullInterfaceType(enumDecl->getRawType(),
                                                  params, enumDecl);

  getterDecl->setType(type);
  getterDecl->setInterfaceType(type);

  getterDecl->setBodyResultType(enumDecl->getRawType());
  getterDecl->setAccessibility(Accessibility::Public);

  rawValueDecl->makeComputed(SourceLoc(), getterDecl, nullptr, nullptr,
                             SourceLoc());

  // Don't bother synthesizing the body if we've already finished type-checking.
  if (Impl.hasFinishedTypeChecking())
    return getterDecl;
  
  auto selfRef = new (C) DeclRefExpr(selfDecl, DeclNameLoc(), /*implicit*/true);
  auto reinterpretCast
    = cast<FuncDecl>(getBuiltinValueDecl(C, C.getIdentifier("reinterpretCast")));
  auto reinterpretCastRef
    = new (C) DeclRefExpr(reinterpretCast, DeclNameLoc(), /*implicit*/ true);
  auto reinterpreted = CallExpr::createImplicit(C, reinterpretCastRef,
                                                { selfRef }, { Identifier() });
  auto ret = new (C) ReturnStmt(SourceLoc(), reinterpreted);
  auto body = BraceStmt::create(C, SourceLoc(), ASTNode(ret), SourceLoc(),
                                /*implicit*/ true);
  
  getterDecl->setBody(body);
  C.addExternalDecl(getterDecl);
  return getterDecl;
}

// Build the rawValue getter for a bridged, swift_newtype'd type.
//   struct SomeType: RawRepresentable {
//     var _rawValue: ObjCType
//     var rawValue: SwiftType {
//       return _rawValue as SwiftType
//     }
//   }
static FuncDecl *makeNewtypeBridgedRawValueGetter(
                   ClangImporter::Implementation &Impl,
                   StructDecl *structDecl,
                   VarDecl *computedVar,
                   VarDecl *storedVar) {
  ASTContext &C = Impl.SwiftContext;
  
  auto selfDecl = ParamDecl::createSelf(SourceLoc(), structDecl);
  
  ParameterList *params[] = {
    ParameterList::createWithoutLoc(selfDecl),
    ParameterList::createEmpty(C)
  };

  auto computedType = computedVar->getType();

  auto getterDecl =
    FuncDecl::create(C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
                     /*FuncLoc=*/SourceLoc(), DeclName(), /*NameLoc=*/SourceLoc(),
                     /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                     /*AccessorKeywordLoc=*/SourceLoc(),
                     /*GenericParams=*/nullptr,
                     params, Type(),
                     TypeLoc::withoutLoc(computedType), structDecl);
  getterDecl->setImplicit();

  auto type = ParameterList::getFullInterfaceType(computedType, params,
                                                  structDecl);

  getterDecl->setType(type);
  getterDecl->setInterfaceType(type);

  getterDecl->setBodyResultType(computedType);
  getterDecl->setAccessibility(Accessibility::Public);

  computedVar->makeComputed(SourceLoc(), getterDecl, nullptr, nullptr,
                            SourceLoc());

  // Don't bother synthesizing the body if we've already finished type-checking.
  if (Impl.hasFinishedTypeChecking())
    return getterDecl;

  auto selfRef = new (C) DeclRefExpr(selfDecl, DeclNameLoc(), /*implicit*/true);
  auto storedRef = new (C) MemberRefExpr(selfRef, SourceLoc(), storedVar,
                                         DeclNameLoc(), /*Implicit=*/true);
  auto coerce = new (C) CoerceExpr(storedRef, {}, {nullptr, computedType});
  auto ret = new (C) ReturnStmt(SourceLoc(), coerce);
  auto body = BraceStmt::create(C, SourceLoc(), ASTNode(ret), SourceLoc(),
                                /*implicit*/ true);
  
  getterDecl->setBody(body);
  C.addExternalDecl(getterDecl);
  return getterDecl;
}

static FuncDecl *makeFieldGetterDecl(ClangImporter::Implementation &Impl,
                                     StructDecl *importedDecl,
                                     VarDecl *importedFieldDecl,
                                     ClangNode clangNode = ClangNode()) {
  auto &C = Impl.SwiftContext;
  auto selfDecl = ParamDecl::createSelf(SourceLoc(), importedDecl);

  ParameterList *params[] = {
    ParameterList::createWithoutLoc(selfDecl),
    ParameterList::createEmpty(C)
  };
  
  auto getterType = importedFieldDecl->getType();
  auto getterDecl =
    FuncDecl::create(C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
                     /*FuncLoc=*/importedFieldDecl->getLoc(),
                     DeclName(), /*NameLoc=*/SourceLoc(),
                     /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                     /*AccessorKeywordLoc=*/SourceLoc(),
                     /*GenericParams=*/nullptr, params, Type(),
                     TypeLoc::withoutLoc(getterType), importedDecl, clangNode);
  getterDecl->setAccessibility(Accessibility::Public);

  auto type = ParameterList::getFullInterfaceType(getterType, params,
                                                  importedDecl);
  getterDecl->setType(type);
  getterDecl->setInterfaceType(type);

  getterDecl->setBodyResultType(getterType);

  return getterDecl;
}

static FuncDecl *makeFieldSetterDecl(ClangImporter::Implementation &Impl,
                                     StructDecl *importedDecl,
                                     VarDecl *importedFieldDecl,
                                     ClangNode clangNode = ClangNode()) {
  auto &C = Impl.SwiftContext;
  auto selfDecl = ParamDecl::createSelf(SourceLoc(), importedDecl,
                                        /*isStatic*/false, /*isInOut*/true);
  auto newValueDecl = new (C) ParamDecl(/*isLet */ true,SourceLoc(),SourceLoc(),
                                        Identifier(), SourceLoc(), C.Id_value,
                                        importedFieldDecl->getType(),
                                        importedDecl);

  ParameterList *params[] = {
    ParameterList::createWithoutLoc(selfDecl),
    ParameterList::createWithoutLoc(newValueDecl),
  };

  auto voidTy = TupleType::getEmpty(C);

  auto setterDecl =
    FuncDecl::create(C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
                     /*FuncLoc=*/SourceLoc(), DeclName(), /*NameLoc=*/SourceLoc(),
                     /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                     /*AccessorKeywordLoc=*/SourceLoc(),
                     /*GenericParams=*/nullptr, params, Type(),
                     TypeLoc::withoutLoc(voidTy), importedDecl, clangNode);

  auto type = ParameterList::getFullInterfaceType(voidTy, params,
                                                  importedDecl);
  setterDecl->setType(type);
  setterDecl->setInterfaceType(type);

  setterDecl->setBodyResultType(voidTy);
  setterDecl->setAccessibility(Accessibility::Public);
  setterDecl->setMutating();

  return setterDecl;
}

/// Build the union field getter and setter.
///
/// \code
/// struct SomeImportedUnion {
///   var myField: Int {
///     get {
///       return Builtin.reinterpretCast(self)
///     }
///     set(newValue) {
///       Builtin.initialize(Builtin.addressof(self), newValue))
///     }
///   }
/// }
/// \endcode
///
/// \returns a pair of the getter and setter function decls.
static std::pair<FuncDecl *, FuncDecl *>
makeUnionFieldAccessors(ClangImporter::Implementation &Impl,
                        StructDecl *importedUnionDecl,
                        VarDecl *importedFieldDecl) {
  auto &C = Impl.SwiftContext;

  auto getterDecl = makeFieldGetterDecl(Impl,
                                        importedUnionDecl,
                                        importedFieldDecl);

  auto setterDecl = makeFieldSetterDecl(Impl,
                                        importedUnionDecl,
                                        importedFieldDecl);

  importedFieldDecl->makeComputed(SourceLoc(), getterDecl, setterDecl, nullptr,
                                  SourceLoc());

  // Don't bother synthesizing the body if we've already finished type-checking.
  if (Impl.hasFinishedTypeChecking())
    return { getterDecl, setterDecl };

  // Synthesize the getter body
  {
    auto selfDecl = getterDecl->getImplicitSelfDecl();

    auto selfRef = new (C) DeclRefExpr(selfDecl, DeclNameLoc(),
                                       /*implicit*/ true);
    auto reinterpretCast = cast<FuncDecl>(getBuiltinValueDecl(
        C, C.getIdentifier("reinterpretCast")));
    auto reinterpretCastRef
      = new (C) DeclRefExpr(reinterpretCast, DeclNameLoc(), /*implicit*/ true);
    auto reinterpreted = CallExpr::createImplicit(C, reinterpretCastRef,
                                                  { selfRef },
                                                  { Identifier() });
    auto ret = new (C) ReturnStmt(SourceLoc(), reinterpreted);
    auto body = BraceStmt::create(C, SourceLoc(), ASTNode(ret), SourceLoc(),
                                  /*implicit*/ true);
    getterDecl->setBody(body);
    getterDecl->getAttrs().add(new (C) TransparentAttr(/*implicit*/ true));
    C.addExternalDecl(getterDecl);
  }

  // Synthesize the setter body
  {
    auto inoutSelfDecl = setterDecl->getImplicitSelfDecl();

    auto inoutSelfRef = new (C) DeclRefExpr(inoutSelfDecl, DeclNameLoc(),
                                            /*implicit*/ true);
    auto inoutSelf = new (C) InOutExpr(SourceLoc(), inoutSelfRef,
      InOutType::get(importedUnionDecl->getType()), /*implicit*/ true);

    auto newValueDecl = setterDecl->getParameterList(1)->get(0);

    auto newValueRef = new (C) DeclRefExpr(newValueDecl, DeclNameLoc(),
                                           /*implicit*/ true);
    auto addressofFn = cast<FuncDecl>(getBuiltinValueDecl(
      C, C.getIdentifier("addressof")));
    auto addressofFnRef
      = new (C) DeclRefExpr(addressofFn, DeclNameLoc(), /*implicit*/ true);
    auto selfPointer = CallExpr::createImplicit(C, addressofFnRef,
                                                { inoutSelf },
                                                { Identifier() });
    auto initializeFn = cast<FuncDecl>(getBuiltinValueDecl(
      C, C.getIdentifier("initialize")));
    auto initializeFnRef
      = new (C) DeclRefExpr(initializeFn, DeclNameLoc(), /*implicit*/ true);
    auto initialize = CallExpr::createImplicit(C, initializeFnRef,
                                               { newValueRef, selfPointer },
                                               { Identifier(), Identifier() });
    auto body = BraceStmt::create(C, SourceLoc(), { initialize }, SourceLoc(),
                                  /*implicit*/ true);
    setterDecl->setBody(body);
    setterDecl->getAttrs().add(new (C) TransparentAttr(/*implicit*/ true));
    C.addExternalDecl(setterDecl);
  }

  return { getterDecl, setterDecl };
}

static clang::DeclarationName
getAccessorDeclarationName(clang::ASTContext &Ctx,
                           StructDecl *structDecl,
                           VarDecl *fieldDecl,
                           const char *suffix) {
  std::string id;
  llvm::raw_string_ostream IdStream(id);
  IdStream << "$" << structDecl->getName()
           << "$" << fieldDecl->getName()
           << "$" << suffix;

  return clang::DeclarationName(&Ctx.Idents.get(IdStream.str()));
}

/// Build the bitfield getter and setter using Clang.
///
/// \code
/// static inline int get(RecordType self) {
///   return self.field;
/// }
/// static inline void set(int newValue, RecordType *self) {
///   self->field = newValue;
/// }
/// \endcode
///
/// \returns a pair of the getter and setter function decls.
static std::pair<FuncDecl *, FuncDecl *>
makeBitFieldAccessors(ClangImporter::Implementation &Impl,
                      clang::RecordDecl *structDecl,
                      StructDecl *importedStructDecl,
                      clang::FieldDecl *fieldDecl,
                      VarDecl *importedFieldDecl) {
  clang::ASTContext &Ctx = Impl.getClangASTContext();

  // Getter: static inline FieldType get(RecordType self);
  auto recordType = Ctx.getRecordType(structDecl);
  auto recordPointerType = Ctx.getPointerType(recordType);
  auto fieldType = fieldDecl->getType();
  auto fieldNameInfo = clang::DeclarationNameInfo(fieldDecl->getDeclName(),
                                                  clang::SourceLocation());

  auto cGetterName = getAccessorDeclarationName(Ctx, importedStructDecl,
                                                importedFieldDecl, "getter");
  auto cGetterType = Ctx.getFunctionType(fieldDecl->getType(),
                                         recordType,
                                         clang::FunctionProtoType::ExtProtoInfo());
  auto cGetterTypeInfo = Ctx.getTrivialTypeSourceInfo(cGetterType);
  auto cGetterDecl = clang::FunctionDecl::Create(Ctx,
                                                 structDecl->getDeclContext(),
                                                 clang::SourceLocation(),
                                                 clang::SourceLocation(),
                                                 cGetterName,
                                                 cGetterType,
                                                 cGetterTypeInfo,
                                                 clang::SC_Static);
  cGetterDecl->setImplicitlyInline();
  assert(!cGetterDecl->isExternallyVisible());

  auto getterDecl = makeFieldGetterDecl(Impl,
                                        importedStructDecl,
                                        importedFieldDecl,
                                        cGetterDecl);

  // Setter: static inline void set(FieldType newValue, RecordType *self);
  SmallVector<clang::QualType, 8> cSetterParamTypes;
  cSetterParamTypes.push_back(fieldType);
  cSetterParamTypes.push_back(recordPointerType);

  auto cSetterName = getAccessorDeclarationName(Ctx, importedStructDecl,
                                                importedFieldDecl, "setter");
  auto cSetterType = Ctx.getFunctionType(Ctx.VoidTy,
                                         cSetterParamTypes,
                                         clang::FunctionProtoType::ExtProtoInfo());
  auto cSetterTypeInfo = Ctx.getTrivialTypeSourceInfo(cSetterType);
  
  auto cSetterDecl = clang::FunctionDecl::Create(Ctx,
                                                 structDecl->getDeclContext(),
                                                 clang::SourceLocation(),
                                                 clang::SourceLocation(),
                                                 cSetterName,
                                                 cSetterType,
                                                 cSetterTypeInfo,
                                                 clang::SC_Static);
  cSetterDecl->setImplicitlyInline();
  assert(!cSetterDecl->isExternallyVisible());

  auto setterDecl = makeFieldSetterDecl(Impl,
                                        importedStructDecl,
                                        importedFieldDecl,
                                        cSetterDecl);

  importedFieldDecl->makeComputed(SourceLoc(),
                                  getterDecl,
                                  setterDecl,
                                  nullptr,
                                  SourceLoc());

  // Don't bother synthesizing the body if we've already finished type-checking.
  if (Impl.hasFinishedTypeChecking())
    return { getterDecl, setterDecl };
  
  // Synthesize the getter body
  {
    auto cGetterSelfId = nullptr;
    auto recordTypeInfo = Ctx.getTrivialTypeSourceInfo(recordType);
    auto cGetterSelf = clang::ParmVarDecl::Create(Ctx, cGetterDecl,
                                                  clang::SourceLocation(),
                                                  clang::SourceLocation(),
                                                  cGetterSelfId,
                                                  recordType,
                                                  recordTypeInfo,
                                                  clang::SC_None,
                                                  nullptr);
    cGetterDecl->setParams(cGetterSelf);
    
    auto cGetterSelfExpr = new (Ctx) clang::DeclRefExpr(cGetterSelf, false,
                                                        recordType,
                                                        clang::VK_RValue,
                                                        clang::SourceLocation());
    auto cGetterExpr = new (Ctx) clang::MemberExpr(cGetterSelfExpr,
                                                   /*isarrow=*/ false,
                                                   clang::SourceLocation(),
                                                   fieldDecl,
                                                   fieldNameInfo,
                                                   fieldType,
                                                   clang::VK_RValue,
                                                   clang::OK_BitField);
    
    auto cGetterBody = new (Ctx) clang::ReturnStmt(clang::SourceLocation(),
                                                   cGetterExpr,
                                                   nullptr);
    cGetterDecl->setBody(cGetterBody);

    Impl.registerExternalDecl(getterDecl);
  }

  // Synthesize the setter body
  {
    SmallVector<clang::ParmVarDecl *, 2> cSetterParams;
    auto fieldTypeInfo = Ctx.getTrivialTypeSourceInfo(fieldType);
    auto cSetterValue = clang::ParmVarDecl::Create(Ctx, cSetterDecl,
                                                   clang::SourceLocation(),
                                                   clang::SourceLocation(),
                                                   /* nameID? */ nullptr,
                                                   fieldType,
                                                   fieldTypeInfo,
                                                   clang::SC_None,
                                                   nullptr);
    cSetterParams.push_back(cSetterValue);
    auto recordPointerTypeInfo = Ctx.getTrivialTypeSourceInfo(recordPointerType);
    auto cSetterSelf = clang::ParmVarDecl::Create(Ctx, cSetterDecl,
                                                  clang::SourceLocation(),
                                                  clang::SourceLocation(),
                                                  /* nameID? */ nullptr,
                                                  recordPointerType,
                                                  recordPointerTypeInfo,
                                                  clang::SC_None,
                                                  nullptr);
    cSetterParams.push_back(cSetterSelf);
    cSetterDecl->setParams(cSetterParams);
    
    auto cSetterSelfExpr = new (Ctx) clang::DeclRefExpr(cSetterSelf, false,
                                                        recordPointerType,
                                                        clang::VK_RValue,
                                                        clang::SourceLocation());
    
    auto cSetterMemberExpr = new (Ctx) clang::MemberExpr(cSetterSelfExpr,
                                                         /*isarrow=*/ true,
                                                         clang::SourceLocation(),
                                                         fieldDecl,
                                                         fieldNameInfo,
                                                         fieldType,
                                                         clang::VK_LValue,
                                                         clang::OK_BitField);
    
    auto cSetterValueExpr = new (Ctx) clang::DeclRefExpr(cSetterValue, false,
                                                         fieldType,
                                                         clang::VK_RValue,
                                                         clang::SourceLocation());
    
    auto cSetterExpr = new (Ctx) clang::BinaryOperator(cSetterMemberExpr,
                                                       cSetterValueExpr,
                                                       clang::BO_Assign,
                                                       fieldType,
                                                       clang::VK_RValue,
                                                       clang::OK_Ordinary,
                                                       clang::SourceLocation(),
                                                       /*fpContractable=*/ false);
    
    cSetterDecl->setBody(cSetterExpr);

    Impl.registerExternalDecl(setterDecl);
  }

  return { getterDecl, setterDecl };
}

namespace {
  using ImportedName = ClangImporter::Implementation::ImportedName;
  using ImportNameFlags = ClangImporter::Implementation::ImportNameFlags;
  using ImportNameOptions = ClangImporter::Implementation::ImportNameOptions;
}

/// Add an AvailableAttr to the declaration for the given
/// version range.
static void applyAvailableAttribute(Decl *decl, AvailabilityContext &info,
                                    ASTContext &C) {
  // If the range is "all", this is the same as not having an available
  // attribute.
  if (info.isAlwaysAvailable())
    return;

  clang::VersionTuple noVersion;
  auto AvAttr = new (C) AvailableAttr(SourceLoc(), SourceRange(),
                                      targetPlatform(C.LangOpts),
                                      /*message=*/StringRef(),
                                      /*rename=*/StringRef(),
                                      info.getOSVersion().getLowerEndpoint(),
                                      /*deprecated=*/noVersion,
                                      /*obsoleted=*/noVersion,
                                      UnconditionalAvailabilityKind::None,
                                      /*implicit=*/false);

  decl->getAttrs().add(AvAttr);
}

/// Synthesize availability attributes for protocol requirements
/// based on availability of the types mentioned in the requirements.
static void inferProtocolMemberAvailability(ClangImporter::Implementation &impl,
                                            DeclContext *dc, Decl *member) {
  // Don't synthesize attributes if there is already an
  // availability annotation.
  if (member->getAttrs().hasAttribute<AvailableAttr>())
    return;

  auto *valueDecl = dyn_cast<ValueDecl>(member);
  if (!valueDecl)
    return;

  AvailabilityContext requiredRange =
      AvailabilityInference::inferForType(valueDecl->getType());

  ASTContext &C = impl.SwiftContext;

  const Decl *innermostDecl = dc->getInnermostDeclarationDeclContext();
  AvailabilityContext containingDeclRange =
      AvailabilityInference::availableRange(innermostDecl, C);

  requiredRange.intersectWith(containingDeclRange);

  applyAvailableAttribute(valueDecl, requiredRange, C);
}

/// Add a domain error member, as required by conformance to
/// _BridgedStoredNSError.
/// \returns true on success, false on failure
static bool addErrorDomain(NominalTypeDecl *swiftDecl,
                           clang::NamedDecl *errorDomainDecl,
                           ClangImporter::Implementation &importer) {
  auto &C = importer.SwiftContext;
  auto swiftValueDecl =
      dyn_cast_or_null<ValueDecl>(importer.importDecl(errorDomainDecl,
                                                      false));
  auto stringTy = C.getStringDecl()->getDeclaredType();
  assert(stringTy && "no string type available");
  if (!swiftValueDecl || !swiftValueDecl->getType()->isEqual(stringTy)) {
    // Couldn't actually import it as an error enum, fall back to enum
    return false;
  }

  bool isStatic = true;
  bool isImplicit = true;

  DeclRefExpr *domainDeclRef = new (C)
      DeclRefExpr(ConcreteDeclRef(swiftValueDecl), {}, isImplicit);
  ParameterList *params[] = {
      ParameterList::createWithoutLoc(
          ParamDecl::createSelf(SourceLoc(), swiftDecl, isStatic)),
      ParameterList::createEmpty(C)};
  auto toStringTy = ParameterList::getFullInterfaceType(stringTy, params,
                                                        swiftDecl);

  FuncDecl *getterDecl =
    FuncDecl::create(C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
                     /*FuncLoc=*/SourceLoc(), DeclName(), /*NameLoc=*/SourceLoc(),
                     /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                     /*AccessorKeywordLoc=*/SourceLoc(),
                     /*GenericParams=*/nullptr, params, toStringTy,
                     TypeLoc::withoutLoc(stringTy), swiftDecl);
  getterDecl->setInterfaceType(toStringTy);

  // Make the property decl
  auto errorDomainPropertyDecl = new (C) VarDecl(
      isStatic,
      /*isLet=*/false, SourceLoc(), C.Id_nsErrorDomain, stringTy, swiftDecl);
  errorDomainPropertyDecl->setAccessibility(Accessibility::Public);

  swiftDecl->addMember(errorDomainPropertyDecl);
  swiftDecl->addMember(getterDecl);
  errorDomainPropertyDecl->makeComputed(SourceLoc(), getterDecl,
                                        /*Set=*/nullptr,
                                        /*MaterializeForSet=*/nullptr,
                                        SourceLoc());

  getterDecl->setImplicit();
  getterDecl->setStatic(isStatic);
  getterDecl->setBodyResultType(stringTy);
  getterDecl->setAccessibility(Accessibility::Public);

  auto ret = new (C) ReturnStmt(SourceLoc(), domainDeclRef);
  getterDecl->setBody(
      BraceStmt::create(C, SourceLoc(), {ret}, SourceLoc(), isImplicit));
  importer.registerExternalDecl(getterDecl);
  return true;
}

/// As addErrorDomain above, but performs a lookup
static bool addErrorDomain(NominalTypeDecl *swiftDecl,
                           StringRef errorDomainName,
                           ClangImporter::Implementation &importer) {
  auto &clangSema = importer.getClangSema();
  clang::IdentifierInfo *errorDomainDeclName =
    &clangSema.getASTContext().Idents.get(errorDomainName);
  clang::LookupResult lookupResult(
      clangSema, clang::DeclarationName(errorDomainDeclName),
      clang::SourceLocation(), clang::Sema::LookupNameKind::LookupOrdinaryName);

  if (!clangSema.LookupName(lookupResult, clangSema.TUScope)) {
    // Couldn't actually import it as an error enum, fall back to enum
    return false;
  }

  auto clangNamedDecl = lookupResult.getAsSingle<clang::NamedDecl>();
  if (!clangNamedDecl) {
    // Couldn't actually import it as an error enum, fall back to enum
    return false;
  }

  return addErrorDomain(swiftDecl, clangNamedDecl, importer);
}

namespace {
  /// \brief Convert Clang declarations into the corresponding Swift
  /// declarations.
  class SwiftDeclConverter
    : public clang::ConstDeclVisitor<SwiftDeclConverter, Decl *>
  {
    ClangImporter::Implementation &Impl;
    bool useSwift2Name;
    bool forwardDeclaration = false;

    /// Import the name of the given entity.
    ///
    /// This version of importFullName introduces any context-specific
    /// name importing options (e.g., if we're importing the Swift 2 version).
    ///
    /// Note: Use this rather than calling Impl.importFullName directly!
    ImportedName importFullName(const clang::NamedDecl *D,
                                Optional<ImportedName> &swift3Name,
                                ImportNameOptions options = None) {
      // Special handling when we import using the Swift 2 name.
      if (useSwift2Name) {
        // First, import based on the Swift 3 name. If that fails, we won't
        // do anything.
        swift3Name = Impl.importFullName(D, options);
        if (!*swift3Name) return *swift3Name;

        // Import using the Swift 2 name. If that fails, or if it's identical
        // to the Swift name, we won't introduce a Swift 2 stub declaration.
        auto swift2Name =
          Impl.importFullName(D, options | ImportNameFlags::Swift2Name);
        if (!swift2Name || swift2Name.Imported == swift3Name->Imported)
          return ImportedName();

        // Okay, return the Swift 2 name.
        return swift2Name;
      }

      // Just import the Swift 2 name.
      swift3Name = None;
      return Impl.importFullName(D, options);
    }

    /// \brief Create a declaration name for anonymous enums, unions and
    /// structs.
    ///
    /// Since Swift does not natively support these features, we fake them by
    /// importing them as declarations with generated names. The generated name
    /// is derived from the name of the field in the outer type. Since the
    /// anonymous type is imported as a nested type of the outer type, this
    /// generated name will most likely be unique.
    ImportedName getClangDeclName(const clang::TagDecl *decl,
                                  Optional<ImportedName> &swift3Name) {
      // If we have a name for this declaration, use it.
      if (auto name = importFullName(decl, swift3Name)) return name;

      // If that didn't succeed, check whether this is an anonymous tag declaration
      // with a corresponding typedef-name declaration.
      if (decl->getDeclName().isEmpty()) {
        if (auto *typedefForAnon = decl->getTypedefNameForAnonDecl())
          return importFullName(typedefForAnon, swift3Name);
      }

      if (!decl->isRecord())
        return ImportedName();

      // If the type has no name and no structure name, but is not anonymous,
      // generate a name for it. Specifically this is for cases like:
      //   struct a {
      //     struct {} z;
      //   }
      // Where the member z is an unnamed struct, but does have a member-name
      // and is accessible as a member of struct a.
      swift3Name = None;
      if (auto recordDecl = dyn_cast<clang::RecordDecl>(
                              decl->getLexicalDeclContext())) {
        for (auto field : recordDecl->fields()) {
          if (field->getType()->getAsTagDecl() == decl) {
            // We found the field. The field should not be anonymous, since we are
            // using its name to derive the generated declaration name.
            assert(!field->isAnonymousStructOrUnion());

            // Create a name for the declaration from the field name.
            std::string Id;
            llvm::raw_string_ostream IdStream(Id);

            const char *kind;
            if (decl->isStruct())
              kind = "struct";
            else if (decl->isUnion())
              kind = "union";
            else
              llvm_unreachable("unknown decl kind");

            IdStream << "__Unnamed_" << kind
            << "_" << field->getName();
            ImportedName Result;
            Result.Imported = Impl.SwiftContext.getIdentifier(IdStream.str());
            Result.EffectiveContext = decl->getDeclContext();
            return Result;
          }
        }
      }
      
      return ImportedName();
    }

  public:
    explicit SwiftDeclConverter(ClangImporter::Implementation &impl,
                                bool useSwift2Name)
      : Impl(impl), useSwift2Name(useSwift2Name) { }

    bool hadForwardDeclaration() const {
      return forwardDeclaration;
    }

    Decl *VisitDecl(const clang::Decl *decl) {
      return nullptr;
    }

    Decl *VisitTranslationUnitDecl(const clang::TranslationUnitDecl *decl) {
      // Note: translation units are handled specially by importDeclContext.
      return nullptr;
    }

    Decl *VisitNamespaceDecl(const clang::NamespaceDecl *decl) {
      // FIXME: Implement once Swift has namespaces.
      return nullptr;
    }

    Decl *VisitUsingDirectiveDecl(const clang::UsingDirectiveDecl *decl) {
      // Never imported.
      return nullptr;
    }

    Decl *VisitNamespaceAliasDecl(const clang::NamespaceAliasDecl *decl) {
      // FIXME: Implement once Swift has namespaces.
      return nullptr;
    }

    Decl *VisitLabelDecl(const clang::LabelDecl *decl) {
      // Labels are function-local, and therefore never imported.
      return nullptr;
    }

    /// Try to strip "Mutable" out of a type name.
    clang::IdentifierInfo *
    getImmutableCFSuperclassName(const clang::TypedefNameDecl *decl) {
      StringRef name = decl->getName();

      // Split at the first occurrence of "Mutable".
      StringRef _mutable = "Mutable";
      auto mutableIndex = camel_case::findWord(name, _mutable);
      if (mutableIndex == StringRef::npos)
        return nullptr;

      StringRef namePrefix = name.substr(0, mutableIndex);
      StringRef nameSuffix = name.substr(mutableIndex + _mutable.size());

      // Abort if "Mutable" appears twice.
      if (camel_case::findWord(nameSuffix, _mutable) != StringRef::npos)
        return nullptr;

      llvm::SmallString<128> buffer;
      buffer += namePrefix;
      buffer += nameSuffix;
      return &Impl.getClangASTContext().Idents.get(buffer.str());
    }

    /// Check whether this CF typedef is a Mutable type, and if so,
    /// look for a non-Mutable typedef.
    ///
    /// If the "subclass" is:
    ///   typedef struct __foo *XXXMutableYYY;
    /// then we look for a "superclass" that matches:
    ///   typedef const struct __foo *XXXYYY;
    Type findImmutableCFSuperclass(const clang::TypedefNameDecl *decl,
                                   CFPointeeInfo subclassInfo) {
      // If this type is already immutable, it has no immutable
      // superclass.
      if (subclassInfo.isConst()) return Type();

      // If this typedef name does not contain "Mutable", it has no
      // immutable superclass.
      auto superclassName = getImmutableCFSuperclassName(decl);
      if (!superclassName) return Type();

      // Look for a typedef that successfully classifies as a CF
      // typedef with the same underlying record.
      auto superclassTypedef = Impl.lookupTypedef(superclassName);
      if (!superclassTypedef) return Type();
      auto superclassInfo = CFPointeeInfo::classifyTypedef(superclassTypedef);
      if (!superclassInfo || !superclassInfo.isRecord() ||
          !declaresSameEntity(superclassInfo.getRecord(),
                              subclassInfo.getRecord()))
        return Type();

      // Try to import the superclass.
      Decl *importedSuperclassDecl = Impl.importDeclReal(superclassTypedef,
                                                         false);
      if (!importedSuperclassDecl) return Type();

      auto importedSuperclass =
        cast<TypeDecl>(importedSuperclassDecl)->getDeclaredType();
      assert(importedSuperclass->is<ClassType>() && "must have class type");
      return importedSuperclass;
    }

    /// Attempt to find a superclass for the given CF typedef.
    Type findCFSuperclass(const clang::TypedefNameDecl *decl,
                          CFPointeeInfo info) {
      if (Type immutable = findImmutableCFSuperclass(decl, info))
        return immutable;

      // TODO: use NSObject if it exists?
      return Type();
    }

    ClassDecl *importCFClassType(const clang::TypedefNameDecl *decl,
                                 Identifier className, CFPointeeInfo info,
                                 EffectiveClangContext effectiveContext) {
      auto dc = Impl.importDeclContextOf(decl, effectiveContext);
      if (!dc) return nullptr;

      Type superclass = findCFSuperclass(decl, info);

      // TODO: maybe use NSObject as the superclass if we can find it?
      // TODO: try to find a non-mutable type to use as the superclass.

      auto theClass =
        Impl.createDeclWithClangNode<ClassDecl>(decl, Accessibility::Public,
                                                SourceLoc(), className,
                                                SourceLoc(), None,
                                                nullptr, dc);
      theClass->computeType();
      theClass->setCircularityCheck(CircularityCheck::Checked);
      theClass->setSuperclass(superclass);
      theClass->setCheckedInheritanceClause();
      theClass->setAddedImplicitInitializers(); // suppress all initializers
      theClass->setForeignClassKind(ClassDecl::ForeignKind::CFType);
      addObjCAttribute(theClass, None);
      Impl.registerExternalDecl(theClass);

      SmallVector<ProtocolDecl *, 4> protocols;
      theClass->getImplicitProtocols(protocols);
      addObjCProtocolConformances(theClass, protocols);

      // Look for bridging attributes on the clang record.  We can
      // just check the most recent redeclaration, which will inherit
      // any attributes from earlier declarations.
      auto record = info.getRecord()->getMostRecentDecl();
      if (info.isConst()) {
        if (auto attr = record->getAttr<clang::ObjCBridgeAttr>()) {
          // Record the Objective-C class to which this CF type is toll-free
          // bridged.
          if (ClassDecl *objcClass = dyn_cast_or_null<ClassDecl>(
                                       Impl.importDeclByName(
                                         attr->getBridgedType()->getName()))) {
            theClass->getAttrs().add(
              new (Impl.SwiftContext) ObjCBridgedAttr(objcClass));
          }
        }
      } else {
        if (auto attr = record->getAttr<clang::ObjCBridgeMutableAttr>()) {
          // Record the Objective-C class to which this CF type is toll-free
          // bridged.
          if (ClassDecl *objcClass = dyn_cast_or_null<ClassDecl>(
                                       Impl.importDeclByName(
                                         attr->getBridgedType()->getName()))) {
            theClass->getAttrs().add(
              new (Impl.SwiftContext) ObjCBridgedAttr(objcClass));
          }
        }
      }

      return theClass;
    }

    /// Mark the given declaration as the Swift 2 variant of a Swift 3
    /// declaration with the given name.
    void markAsSwift2Variant(Decl *decl, ImportedName swift3Name,
                             DeclContext *newDC = nullptr) {
      ASTContext &ctx = decl->getASTContext();

      llvm::SmallString<64> renamed;
      {
        // Render a swift_name string.
        llvm::raw_svector_ostream os(renamed);

        // If we're importing a global as a member, we need to provide the
        // effective context.
        Impl.printSwiftName(swift3Name,
                            /*fullyQualified=*/swift3Name.ImportAsMember,
                            os);
      }

      auto attr = AvailableAttr::createUnconditional(
                    ctx, StringRef(), ctx.AllocateCopy(renamed.str()),
                    UnconditionalAvailabilityKind::UnavailableInCurrentSwift);
      decl->getAttrs().add(attr);
      decl->setImplicit();
    }

    /// Create a typealias for the Swift 2 name of a Clang type declaration.
    Decl *importSwift2TypeAlias(const clang::NamedDecl *decl,
                                ImportedName swift2Name,
                                ImportedName swift3Name) {
      // Import the referenced declaration. If it doesn't come in as a type,
      // we don't care.
      auto importedDecl = Impl.importDecl(decl, /*useSwift2Name=*/false);
      auto typeDecl = dyn_cast_or_null<TypeDecl>(importedDecl);
      if (!typeDecl) return nullptr;

      // Handle generic types.
      GenericParamList *genericParams = nullptr;
      GenericSignature *genericSig = nullptr;
      auto underlyingType = typeDecl->getDeclaredInterfaceType();

      if (auto generic = dyn_cast<GenericTypeDecl>(typeDecl)) {
        if (generic->getGenericSignature() && !isa<ProtocolDecl>(typeDecl)) {
          genericParams = generic->getGenericParams();
          genericSig = generic->getGenericSignature();

          underlyingType = ArchetypeBuilder::mapTypeIntoContext(
              generic, underlyingType);
        }
      }

      // Import the declaration context where this name will go. Note that
      // this is the "natural" context for the declaration, without
      // import-as-member inference or swift_name tricks.
      EffectiveClangContext effectiveContext(
                              decl->getDeclContext()->getRedeclContext());
      auto dc = Impl.importDeclContextOf(decl, effectiveContext);
      if (!dc) return nullptr;

      // Create the type alias.
      auto alias = Impl.createDeclWithClangNode<TypeAliasDecl>(
                     decl, Accessibility::Public,
                     Impl.importSourceLoc(decl->getLocStart()),
                     swift2Name.Imported.getBaseName(),
                     Impl.importSourceLoc(decl->getLocation()),
                     TypeLoc::withoutLoc(underlyingType),
                     genericParams, dc);
      alias->computeType();
      alias->setGenericSignature(genericSig);

      // Record that this is the Swift 2 version of this declaration.
      Impl.ImportedDecls[{decl->getCanonicalDecl(), true}] = alias;

      // Mark it as the Swift 2 variant.
      markAsSwift2Variant(alias, swift3Name);
      return alias;
    }

    /// Create a swift_newtype struct corresponding to a typedef. Returns
    /// nullptr if unable.
    Decl *importSwiftNewtype(const clang::TypedefNameDecl *decl,
                             clang::SwiftNewtypeAttr *newtypeAttr,
                             DeclContext *dc, Identifier name) {
      // The only (current) difference between swift_newtype(struct) and
      // swift_newtype(enum), until we can get real enum support, is that enums
      // have no un-labeled inits(). This is because enums are to be considered
      // closed, and if constructed from a rawValue, should be very explicit.
      bool unlabeledCtor = false;

      switch (newtypeAttr->getNewtypeKind()) {
      case clang::SwiftNewtypeAttr::NK_Enum:
        unlabeledCtor = false;
        // TODO: import as enum instead
        break;

      case clang::SwiftNewtypeAttr::NK_Struct:
        unlabeledCtor = true;
        break;
      // No other cases yet
      }

      auto &cxt = Impl.SwiftContext;
      auto Loc = Impl.importSourceLoc(decl->getLocation());

      auto structDecl = Impl.createDeclWithClangNode<StructDecl>(
          decl, Accessibility::Public, Loc, name, Loc, None, nullptr, dc);
      structDecl->computeType();

      // Import the type of the underlying storage
      auto storedUnderlyingType = Impl.importType(
          decl->getUnderlyingType(), ImportTypeKind::Value,
          isInSystemModule(dc), decl->getUnderlyingType()->isBlockPointerType(),
          OTK_None);
      if (auto objTy = storedUnderlyingType->getAnyOptionalObjectType())
        storedUnderlyingType = objTy;

      // If the type is Unmanaged, that is it is not CF ARC audited,
      // we will store the underlying type and leave it up to the use site
      // to determine whether to use this new_type, or an Unmanaged<CF...> type.
      if (auto genericType = storedUnderlyingType->getAs<BoundGenericType>()) {
        if (genericType->getDecl() == Impl.SwiftContext.getUnmanagedDecl()) {
          assert(genericType->getGenericArgs().size() == 1 && "other args?");
          storedUnderlyingType = genericType->getGenericArgs()[0];
        }
      }

      // Find a bridged type, which may be different
      auto computedPropertyUnderlyingType = Impl.importType(
          decl->getUnderlyingType(), ImportTypeKind::Property,
          isInSystemModule(dc), decl->getUnderlyingType()->isBlockPointerType(),
          OTK_None);
      if (auto objTy =
            computedPropertyUnderlyingType->getAnyOptionalObjectType())
        computedPropertyUnderlyingType = objTy;

      bool isBridged =
          !storedUnderlyingType->isEqual(computedPropertyUnderlyingType);

      // Determine the set of protocols to which the synthesized
      // type will conform.
      SmallVector<ProtocolDecl *, 4> protocols;
      SmallVector<KnownProtocolKind, 4> synthesizedProtocols;

      // Local function to add a known protocol.
      auto addKnown = [&](KnownProtocolKind kind) {
        if (auto proto = cxt.getProtocol(kind)) {
          protocols.push_back(proto);
          synthesizedProtocols.push_back(kind);
        }
      };

      // Add conformances that are always available.
      addKnown(KnownProtocolKind::RawRepresentable);
      addKnown(KnownProtocolKind::SwiftNewtypeWrapper);

      // Local function to add a known protocol only when the
      // underlying type conforms to it.
      auto computedNominal = computedPropertyUnderlyingType->getAnyNominal();
      auto transferKnown = [&](KnownProtocolKind kind) {
        if (!computedNominal)
          return;

        auto proto = cxt.getProtocol(kind);
        if (!proto)
          return;

        SmallVector<ProtocolConformance *, 1> conformances;
        if (computedNominal->lookupConformance(
                computedNominal->getParentModule(), proto, conformances)) {
          protocols.push_back(proto);
          synthesizedProtocols.push_back(kind);
        }
      };

      // Transfer conformances. Each of these needs a forwarding
      // implementation in the standard library.
      transferKnown(KnownProtocolKind::Equatable);
      transferKnown(KnownProtocolKind::Hashable);
      transferKnown(KnownProtocolKind::Comparable);
      transferKnown(KnownProtocolKind::ObjectiveCBridgeable);

      if (!isBridged) {
        // Simple, our stored type is equivalent to our computed
        // type.
        auto options = getDefaultMakeStructRawValuedOptions();
        if (unlabeledCtor)
          options |= MakeStructRawValuedFlags::MakeUnlabeledValueInit;

        makeStructRawValued(structDecl, storedUnderlyingType,
                            synthesizedProtocols, protocols, options);
      } else {
        // We need to make a stored rawValue or storage type, and a
        // computed one of bridged type.
        makeStructRawValuedWithBridge(structDecl, storedUnderlyingType,
                                      computedPropertyUnderlyingType,
                                      synthesizedProtocols, protocols,
                                      /*makeUnlabeledValueInit=*/unlabeledCtor);
      }

      Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] =
          structDecl;
      Impl.registerExternalDecl(structDecl);
      return structDecl;
    }

    Decl *VisitTypedefNameDecl(const clang::TypedefNameDecl *Decl) {
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(Decl, swift3Name);
      auto Name = importedName.Imported.getBaseName();
      if (Name.empty())
        return nullptr;

      // If we've been asked to produce a Swift 2 stub, handle it via a
      // typealias.
      if (swift3Name)
        return importSwift2TypeAlias(Decl, importedName, *swift3Name);

      Type SwiftType;
      if (Decl->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
        // Ignore the 'id' typedef. We want to bridge the underlying
        // ObjCId type.
        //
        // When we remove the EnableIdAsAny staging flag, the 'id' entry
        // should be removed from MappedTypes.def, and this conditional should
        // become unnecessary.
        if (Name.str() == "id" && Impl.SwiftContext.LangOpts.EnableIdAsAny) {
          Impl.SpecialTypedefNames[Decl->getCanonicalDecl()] =
              MappedTypeNameKind::DoNothing;

          auto DC = Impl.importDeclContextOf(Decl, importedName.EffectiveContext);
          if (!DC) return nullptr;

          auto loc = Impl.importSourceLoc(Decl->getLocStart());
          
          // Leave behind a typealias for 'id' marked unavailable, to instruct
          // people to use Any instead.
          auto unavailableAlias = Impl.createDeclWithClangNode<TypeAliasDecl>(
                          Decl, Accessibility::Public, loc, Name, loc,
                          TypeLoc::withoutLoc(Impl.SwiftContext.TheAnyType),
                          /*genericparams*/nullptr, DC);

          auto attr = AvailableAttr::createUnconditional(
                        Impl.SwiftContext, "'id' is not available in Swift; use 'Any'",
                        "", UnconditionalAvailabilityKind::UnavailableInSwift);

          unavailableAlias->getAttrs().add(attr);
          return nullptr;
        }
      
        bool IsError;
        StringRef StdlibTypeName;
        MappedTypeNameKind NameMapping;
        std::tie(SwiftType, StdlibTypeName) =
            getSwiftStdlibType(Decl, Name, Impl, &IsError, NameMapping);

        if (IsError)
          return nullptr;

        // Import 'typedef struct __Blah *BlahRef;' and
        // 'typedef const void *FooRef;' as CF types if they have the
        // right attributes or match our name whitelist.
        if (!SwiftType) {
          auto DC = Impl.importDeclContextOf(Decl,
                                             importedName.EffectiveContext);
          if (!DC)
            return nullptr;

          if (auto pointee = CFPointeeInfo::classifyTypedef(Decl)) {
            // If the pointee is a record, consider creating a class type.
            if (pointee.isRecord()) {
              auto swiftClass =
                  importCFClassType(Decl, Name, pointee,
                                    importedName.EffectiveContext);
              if (!swiftClass) return nullptr;

              Impl.SpecialTypedefNames[Decl->getCanonicalDecl()] =
                MappedTypeNameKind::DefineAndUse;
              return swiftClass;
            }

            // If the pointee is another CF typedef, create an extra typealias
            // for the name without "Ref", but not a separate type.
            if (pointee.isTypedef()) {
              auto underlying =
                cast_or_null<TypeDecl>(
                  Impl.importDecl(pointee.getTypedef(),
                                  /*useSwift2Name=*/false));
              if (!underlying)
                return nullptr;

              // Check for a newtype
              if (auto newtypeAttr =
                      Impl.getSwiftNewtypeAttr(Decl, useSwift2Name))
                if (auto newtype =
                        importSwiftNewtype(Decl, newtypeAttr, DC, Name))
                  return newtype;

              // Create a typealias for this CF typedef.
              TypeAliasDecl *typealias = nullptr;
              typealias = Impl.createDeclWithClangNode<TypeAliasDecl>(
                            Decl, Accessibility::Public,
                            Impl.importSourceLoc(Decl->getLocStart()),
                            Name,
                            Impl.importSourceLoc(Decl->getLocation()),
                            TypeLoc::withoutLoc(
                              underlying->getDeclaredInterfaceType()),
                            /*genericparams*/nullptr, DC);
              typealias->computeType();

              Impl.SpecialTypedefNames[Decl->getCanonicalDecl()] =
                MappedTypeNameKind::DefineAndUse;
              return typealias;
            }

            // If the pointee is 'void', 'CFTypeRef', bring it
            // in specifically as AnyObject.
            if (pointee.isVoid()) {
              auto proto = Impl.SwiftContext.getProtocol(
                                               KnownProtocolKind::AnyObject);
              if (!proto)
                return nullptr;

              // Create a typealias for this CF typedef.
              TypeAliasDecl *typealias = nullptr;
              typealias = Impl.createDeclWithClangNode<TypeAliasDecl>(
                            Decl, Accessibility::Public,
                            Impl.importSourceLoc(Decl->getLocStart()),
                            Name,
                            Impl.importSourceLoc(Decl->getLocation()),
                            TypeLoc::withoutLoc(
                              proto->getDeclaredInterfaceType()),
                            /*genericparams*/nullptr, DC);
              typealias->computeType();

              Impl.SpecialTypedefNames[Decl->getCanonicalDecl()] =
                MappedTypeNameKind::DefineAndUse;
              return typealias;
            }
          }
        }

        if (SwiftType) {
          // Note that this typedef-name is special.
          Impl.SpecialTypedefNames[Decl->getCanonicalDecl()] = NameMapping;

          if (NameMapping == MappedTypeNameKind::DoNothing) {
            // Record the remapping using the name of the Clang declaration.
            // This will be useful for type checker diagnostics when
            // a user tries to use the Objective-C/C type instead of the
            // Swift type.
            Impl.SwiftContext.RemappedTypes[Decl->getNameAsString()]
              = SwiftType;

            // Don't create an extra typealias in the imported module because
            // doing so will cause confusion (or even lookup ambiguity) between
            // the name in the imported module and the same name in the
            // standard library.
            if (auto *NAT = dyn_cast<NameAliasType>(SwiftType.getPointer()))
              return NAT->getDecl();

            auto *NTD = SwiftType->getAnyNominal();
            assert(NTD);
            return NTD;
          }
        }
      }

      auto DC = Impl.importDeclContextOf(Decl, importedName.EffectiveContext);
      if (!DC)
        return nullptr;

      // Check for swift_newtype
      if (!SwiftType)
        if (auto newtypeAttr = Impl.getSwiftNewtypeAttr(Decl, useSwift2Name))
          if (auto newtype = importSwiftNewtype(Decl, newtypeAttr, DC, Name))
            return newtype;

      if (!SwiftType) {
        // Import typedefs of blocks as their fully-bridged equivalent Swift
        // type. That matches how we want to use them in most cases. All other
        // types should be imported in a non-bridged way.
        clang::QualType ClangType = Decl->getUnderlyingType();
        SwiftType = Impl.importType(ClangType,
                                    ImportTypeKind::Typedef,
                                    isInSystemModule(DC),
                                    ClangType->isBlockPointerType(),
                                    OTK_Optional);
      }

      if (!SwiftType)
        return nullptr;

      auto Loc = Impl.importSourceLoc(Decl->getLocation());
      auto Result = Impl.createDeclWithClangNode<TypeAliasDecl>(Decl,
                                      Accessibility::Public,
                                      Impl.importSourceLoc(Decl->getLocStart()),
                                      Name,
                                      Loc,
                                      TypeLoc::withoutLoc(SwiftType),
                                      /*genericparams*/nullptr, DC);
      Result->computeType();
      return Result;
    }

    Decl *
    VisitUnresolvedUsingTypenameDecl(const
                                     clang::UnresolvedUsingTypenameDecl *decl) {
      // Note: only occurs in templates.
      return nullptr;
    }
    
    /// \brief Create a default constructor that initializes a struct to zero.
    ConstructorDecl *createDefaultConstructor(StructDecl *structDecl) {
      auto &context = Impl.SwiftContext;
      
      // Create the 'self' declaration.
      auto selfDecl = ParamDecl::createSelf(SourceLoc(), structDecl,
                                            /*static*/false, /*inout*/true);
      
      // self & param.
      auto emptyPL = ParameterList::createEmpty(context);

      // Create the constructor.
      DeclName name(context, context.Id_init, emptyPL);
      auto constructor =
        new (context) ConstructorDecl(name, structDecl->getLoc(),
                                      OTK_None, /*FailabilityLoc=*/SourceLoc(),
                                      /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                                      selfDecl, emptyPL,
                                      /*GenericParams=*/nullptr, structDecl);
      
      // Set the constructor's type.
      auto selfType = structDecl->getDeclaredTypeInContext();
      auto selfMetatype = MetatypeType::get(selfType);
      auto emptyTy = TupleType::getEmpty(context);
      auto fnTy = FunctionType::get(emptyTy, selfType);
      auto allocFnTy = FunctionType::get(selfMetatype, fnTy);
      auto initFnTy = FunctionType::get(selfType, fnTy);
      constructor->setType(allocFnTy);
      constructor->setInterfaceType(allocFnTy);
      constructor->setInitializerInterfaceType(initFnTy);
      
      constructor->setAccessibility(Accessibility::Public);

      // Mark the constructor transparent so that we inline it away completely.
      constructor->getAttrs().add(
                              new (context) TransparentAttr(/*implicit*/ true));

      // Use a builtin to produce a zero initializer, and assign it to self.
      constructor->setBodySynthesizer([](AbstractFunctionDecl *constructor) {
        ASTContext &context = constructor->getASTContext();

        // Construct the left-hand reference to self.
        Expr *lhs =
            new (context) DeclRefExpr(constructor->getImplicitSelfDecl(),
                                      DeclNameLoc(), /*implicit=*/true);

        // Construct the right-hand call to Builtin.zeroInitializer.
        Identifier zeroInitID = context.getIdentifier("zeroInitializer");
        auto zeroInitializerFunc =
            cast<FuncDecl>(getBuiltinValueDecl(context, zeroInitID));
        auto zeroInitializerRef = new (context) DeclRefExpr(zeroInitializerFunc,
                                                            DeclNameLoc(),
                                                            /*implicit*/ true);
        auto call = CallExpr::createImplicit(context, zeroInitializerRef,
                                             { }, { });

        auto assign = new (context) AssignExpr(lhs, SourceLoc(), call,
                                               /*implicit*/ true);

        // Create the function body.
        auto body = BraceStmt::create(context, SourceLoc(), { assign },
                                      SourceLoc());
        constructor->setBody(body);
      });

      // Add this as an external definition.
      Impl.registerExternalDecl(constructor);

      // We're done.
      return constructor;
    }

    /// Make a struct declaration into a raw-value-backed struct
    ///
    /// \param structDecl the struct to make a raw value for
    /// \param underlyingType the type of the raw value
    /// \param synthesizedProtocolAttrs synthesized protocol attributes to add
    /// \param protocols the protocols to make this struct conform to
    /// \param setterAccessibility the accessibility of the raw value's setter
    ///
    /// This will perform most of the work involved in making a new Swift struct
    /// be backed by a raw value. This will populated derived protocols and
    /// synthesized protocols, add the new variable and pattern bindings, and
    /// create the inits parameterized over a raw value
    ///
    void makeStructRawValued(
        StructDecl *structDecl, Type underlyingType,
        ArrayRef<KnownProtocolKind> synthesizedProtocolAttrs,
        ArrayRef<ProtocolDecl *> protocols,
        MakeStructRawValuedOptions options =
            getDefaultMakeStructRawValuedOptions(),
        Accessibility setterAccessibility = Accessibility::Private) {
      auto &cxt = Impl.SwiftContext;
      addProtocolsToStruct(structDecl, synthesizedProtocolAttrs, protocols);

      // Create a variable to store the underlying value.
      VarDecl *var;
      PatternBindingDecl *patternBinding;
      std::tie(var, patternBinding) = createVarWithPattern(
          cxt, structDecl, cxt.Id_rawValue, underlyingType,
          options.contains(MakeStructRawValuedFlags::IsLet),
          options.contains(MakeStructRawValuedFlags::IsImplicit),
          setterAccessibility);

      structDecl->setHasDelayedMembers();

      // Create constructors to initialize that value from a value of the
      // underlying type.
      if (options.contains(
              MakeStructRawValuedFlags::MakeUnlabeledValueInit))
        structDecl->addMember(createValueConstructor(
            structDecl, var,
            /*wantCtorParamNames=*/false,
            /*wantBody=*/!Impl.hasFinishedTypeChecking()));
      structDecl->addMember(
          createValueConstructor(structDecl, var,
                                 /*wantCtorParamNames=*/true,
                                 /*wantBody=*/!Impl.hasFinishedTypeChecking()));
      structDecl->addMember(patternBinding);
      structDecl->addMember(var);
    }

    /// Make a struct declaration into a raw-value-backed struct, with
    /// bridged computed rawValue property which differs from stored backing
    ///
    /// \param structDecl the struct to make a raw value for
    /// \param storedUnderlyingType the type of the stored raw value
    /// \param bridgedType the type of the 'rawValue' computed property bridge
    /// \param synthesizedProtocolAttrs synthesized protocol attributes to add
    /// \param protocols the protocols to make this struct conform to
    ///
    /// This will perform most of the work involved in making a new Swift struct
    /// be backed by a stored raw value and computed raw value of bridged type.
    /// This will populated derived protocols and synthesized protocols, add the
    /// new variable and pattern bindings, and create the inits parameterized
    /// over a bridged type that will cast to the stored type, as appropriate.
    ///
    void makeStructRawValuedWithBridge(
        StructDecl *structDecl, Type storedUnderlyingType, Type bridgedType,
        ArrayRef<KnownProtocolKind> synthesizedProtocolAttrs,
        ArrayRef<ProtocolDecl *> protocols,
        bool makeUnlabeledValueInit = false) {
      auto &cxt = Impl.SwiftContext;
      addProtocolsToStruct(structDecl, synthesizedProtocolAttrs, protocols);

      auto storedVarName = cxt.getIdentifier("_rawValue");
      auto computedVarName = cxt.Id_rawValue;

      // Create a variable to store the underlying value.
      VarDecl *storedVar;
      PatternBindingDecl *storedPatternBinding;
      std::tie(storedVar, storedPatternBinding) = createVarWithPattern(
          cxt, structDecl, storedVarName, storedUnderlyingType, /*isLet=*/false,
          /*isImplicit=*/true, Accessibility::Private);

      //
      // Create a computed value variable
      auto computedVar = new (cxt) VarDecl(/*static*/ false,
                                           /*IsLet*/ false, SourceLoc(),
                                           computedVarName, bridgedType,
                                           structDecl);
      computedVar->setImplicit();
      computedVar->setAccessibility(Accessibility::Public);
      computedVar->setSetterAccessibility(Accessibility::Private);

      // Create the getter for the computed value variable.
      auto computedVarGetter = makeNewtypeBridgedRawValueGetter(
          Impl, structDecl, computedVar, storedVar);

      // Create a pattern binding to describe the variable.
      Pattern *computedVarPattern = createTypedNamedPattern(computedVar);
      auto computedPatternBinding = PatternBindingDecl::create(
          cxt, SourceLoc(), StaticSpellingKind::None, SourceLoc(),
          computedVarPattern, nullptr, structDecl);

      // Don't bother synthesizing the body if we've already finished
      // type-checking.
      bool wantBody = !Impl.hasFinishedTypeChecking();

      auto init = createRawValueBridgingConstructor(
          structDecl, computedVar, storedVar,
          /*wantLabel*/ true, wantBody);

      ConstructorDecl *unlabeledCtor = nullptr;
      if (makeUnlabeledValueInit)
        unlabeledCtor = createRawValueBridgingConstructor(
            structDecl, computedVar, storedVar,
            /*wantLabel*/ false, wantBody);

      structDecl->setHasDelayedMembers();
      if (unlabeledCtor)
        structDecl->addMember(unlabeledCtor);
      structDecl->addMember(init);
      structDecl->addMember(storedPatternBinding);
      structDecl->addMember(storedVar);
      structDecl->addMember(computedPatternBinding);
      structDecl->addMember(computedVar);
      structDecl->addMember(computedVarGetter);
    }

    /// Create a rawValue-ed constructor that bridges to its underlying storage.
    ConstructorDecl *createRawValueBridgingConstructor(
        StructDecl *structDecl, VarDecl *computedRawValue,
        VarDecl *storedRawValue, bool wantLabel, bool wantBody) {
      auto &cxt = Impl.SwiftContext;
      auto init = createValueConstructor(structDecl, computedRawValue,
                                         /*wantCtorParamNames=*/wantLabel,
                                         /*wantBody=*/false);
      // Insert our custom init body
      if (wantBody) {
        auto selfDecl = init->getParameterList(0)->get(0);

        // Construct left-hand side.
        Expr *lhs = new (cxt) DeclRefExpr(selfDecl, DeclNameLoc(),
                                          /*Implicit=*/true);
        lhs = new (cxt) MemberRefExpr(lhs, SourceLoc(), storedRawValue,
                                      DeclNameLoc(), /*Implicit=*/true);

        // Construct right-hand side.
        // FIXME: get the parameter from the init, and plug it in here.
        auto rhs = new (cxt)
            CoerceExpr(new (cxt) DeclRefExpr(init->getParameterList(1)->get(0),
                                             DeclNameLoc(),
                                             /*Implicit=*/true),
                       {}, {nullptr, storedRawValue->getType()});

        // Add assignment.
        auto assign = new (cxt) AssignExpr(lhs, SourceLoc(), rhs,
                                           /*Implicit=*/true);
        auto body = BraceStmt::create(cxt, SourceLoc(), {assign}, SourceLoc());
        init->setBody(body);
      }

      return init;
    }

    /// \brief Create a constructor that initializes a struct from its members.
    ConstructorDecl *createValueConstructor(StructDecl *structDecl,
                                            ArrayRef<VarDecl *> members,
                                            bool wantCtorParamNames,
                                            bool wantBody) {
      auto &context = Impl.SwiftContext;

      // Create the 'self' declaration.
      auto selfDecl = ParamDecl::createSelf(SourceLoc(), structDecl,
                                            /*static*/false, /*inout*/true);

      // Construct the set of parameters from the list of members.
      SmallVector<ParamDecl*, 8> valueParameters;
      for (auto var : members) {
        Identifier argName = wantCtorParamNames ? var->getName()
                                                : Identifier();
        auto param = new (context) ParamDecl(/*IsLet*/ true, SourceLoc(),
                                             SourceLoc(), argName,
                                             SourceLoc(), var->getName(),
                                             var->getType(), structDecl);
        valueParameters.push_back(param);
      }

      // self & param.
      ParameterList *paramLists[] = {
        ParameterList::createWithoutLoc(selfDecl),
        ParameterList::create(context, valueParameters)
      };
      
      // Create the constructor
      DeclName name(context, context.Id_init, paramLists[1]);
      auto constructor =
        new (context) ConstructorDecl(name, structDecl->getLoc(),
                                      OTK_None, /*FailabilityLoc=*/SourceLoc(),
                                      /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                                      selfDecl, paramLists[1],
                                      /*GenericParams=*/nullptr, structDecl);

      // Set the constructor's type.
      auto paramTy = paramLists[1]->getType(context);
      auto selfType = structDecl->getDeclaredTypeInContext();
      auto selfMetatype = MetatypeType::get(selfType);
      auto fnTy = FunctionType::get(paramTy, selfType);
      auto allocFnTy = FunctionType::get(selfMetatype, fnTy);
      auto initFnTy = FunctionType::get(selfType, fnTy);
      constructor->setType(allocFnTy);
      constructor->setInterfaceType(allocFnTy);
      constructor->setInitializerInterfaceType(initFnTy);
      
      constructor->setAccessibility(Accessibility::Public);

      // Make the constructor transparent so we inline it away completely.
      constructor->getAttrs().add(
                              new (context) TransparentAttr(/*implicit*/ true));

      if (wantBody) {
        // Assign all of the member variables appropriately.
        SmallVector<ASTNode, 4> stmts;

        // To keep DI happy, initialize stored properties before computed.
        for (unsigned pass = 0; pass < 2; pass++) {
          for (unsigned i = 0, e = members.size(); i < e; i++) {
            auto var = members[i];
            if (var->hasStorage() == (pass != 0))
              continue;

            // Construct left-hand side.
            Expr *lhs = new (context) DeclRefExpr(selfDecl, DeclNameLoc(),
                                                  /*Implicit=*/true);
            lhs = new (context) MemberRefExpr(lhs, SourceLoc(), var,
                                              DeclNameLoc(), /*Implicit=*/true);

            // Construct right-hand side.
            auto rhs = new (context) DeclRefExpr(valueParameters[i],
                                                 DeclNameLoc(),
                                                 /*Implicit=*/true);

            // Add assignment.
            stmts.push_back(new (context) AssignExpr(lhs, SourceLoc(), rhs,
                                                     /*Implicit=*/true));
          }
        }

        // Create the function body.
        auto body = BraceStmt::create(context, SourceLoc(), stmts, SourceLoc());
        constructor->setBody(body);
      }

      // Add this as an external definition.
      Impl.registerExternalDecl(constructor);

      // We're done.
      return constructor;
    }
    
    /// Import an NS_ENUM constant as a case of a Swift enum.
    Decl *importEnumCase(const clang::EnumConstantDecl *decl,
                         const clang::EnumDecl *clangEnum,
                         EnumDecl *theEnum,
                         Decl *swift3Decl = nullptr) {
      auto &context = Impl.SwiftContext;
      Optional<ImportedName> swift3Name;
      auto name = importFullName(decl, swift3Name).Imported.getBaseName();
      if (name.empty())
        return nullptr;

      if (swift3Name) {
        // We're creating a Swift 2 stub. Treat it as an enum case alias.
        if (!swift3Decl) return nullptr;

        // If the Swift 3 declaration was unavailable, don't map to it.
        // FIXME: This eliminates spurious errors, but affects QoI.
        if (swift3Decl->getAttrs().isUnavailable(Impl.SwiftContext))
          return nullptr;

        auto swift3Case = dyn_cast<EnumElementDecl>(swift3Decl);
        if (!swift3Case) return nullptr;

        auto swift2Case = importEnumCaseAlias(name, decl, swift3Case,
                                              clangEnum, theEnum);
        if (swift2Case) markAsSwift2Variant(swift2Case, *swift3Name);

        return swift2Case;
      }

      // Use the constant's underlying value as its raw value in Swift.
      bool negative = false;
      llvm::APSInt rawValue = decl->getInitVal();
      
      // Did we already import an enum constant for this enum with the
      // same value? If so, import it as a standalone constant.
      auto insertResult =
        Impl.EnumConstantValues.insert({{clangEnum, rawValue}, nullptr});
      if (!insertResult.second)
        return importEnumCaseAlias(name, decl, insertResult.first->second,
                                   clangEnum, theEnum);

      if (clangEnum->getIntegerType()->isSignedIntegerOrEnumerationType()
          && rawValue.slt(0)) {
        rawValue = -rawValue;
        negative = true;
      }
      llvm::SmallString<12> rawValueText;
      rawValue.toString(rawValueText, 10, /*signed*/ false);
      StringRef rawValueTextC
        = context.AllocateCopy(StringRef(rawValueText));
      auto rawValueExpr = new (context) IntegerLiteralExpr(rawValueTextC,
                                                       SourceLoc(),
                                                       /*implicit*/ false);
      if (negative)
        rawValueExpr->setNegative(SourceLoc());
      
      auto element
        = Impl.createDeclWithClangNode<EnumElementDecl>(decl,
                                        Accessibility::Public, SourceLoc(),
                                        name, TypeLoc(),
                                        SourceLoc(), rawValueExpr,
                                        theEnum);
      insertResult.first->second = element;

      // Give the enum element the appropriate type.
      element->computeType();

      Impl.importAttributes(decl, element);

      return element;
    }
    
    /// Import an NS_OPTIONS constant as a static property of a Swift struct.
    ///
    /// This is also used to import enum case aliases.
    Decl *importOptionConstant(const clang::EnumConstantDecl *decl,
                               const clang::EnumDecl *clangEnum,
                               NominalTypeDecl *theStruct) {
      Optional<ImportedName> swift3Name;
      ImportedName nameInfo = importFullName(decl, swift3Name);
      Identifier name = nameInfo.Imported.getBaseName();
      if (name.empty())
        return nullptr;

      // Create the constant.
      auto convertKind = ConstantConvertKind::Construction;
      if (isa<EnumDecl>(theStruct))
        convertKind = ConstantConvertKind::ConstructionWithUnwrap;
      Decl *CD = Impl.createConstant(name, theStruct,
                                     theStruct->getDeclaredTypeInContext(),
                                     clang::APValue(decl->getInitVal()),
                                     convertKind, /*isStatic*/ true, decl);
      Impl.importAttributes(decl, CD);

      // NS_OPTIONS members that have a value of 0 (typically named "None") do
      // not operate as a set-like member.  Mark them unavailable with a message
      // that says that they should be used as [].
      if (decl->getInitVal() == 0 && !nameInfo.HasCustomName &&
          !CD->getAttrs().isUnavailable(Impl.SwiftContext)) {
        /// Create an AvailableAttr that indicates specific availability
        /// for all platforms.
        auto attr =
          AvailableAttr::createUnconditional(Impl.SwiftContext,
                           "use [] to construct an empty option set");
        CD->getAttrs().add(attr);
      }

      // If this is a Swift 2 stub, mark it as such.
      if (swift3Name)
        markAsSwift2Variant(CD, *swift3Name);

      return CD;
    }

    /// Import \p alias as an alias for the imported constant \p original.
    ///
    /// This builds the getter in a way that's compatible with switch
    /// statements. Changing the body here may require changing
    /// TypeCheckPattern.cpp as well.
    Decl *importEnumCaseAlias(Identifier name,
                              const clang::EnumConstantDecl *alias,
                              ValueDecl *original,
                              const clang::EnumDecl *clangEnum,
                              NominalTypeDecl *importedEnum,
                              DeclContext *importIntoDC = nullptr) {
      if (name.empty())
        return nullptr;

      // Default the DeclContext to the enum type.
      if (!importIntoDC)
        importIntoDC = importedEnum;

      // Construct the original constant. Enum constants without payloads look
      // like simple values, but actually have type 'MyEnum.Type -> MyEnum'.
      auto constantRef = new (Impl.SwiftContext) DeclRefExpr(original,
                                                             DeclNameLoc(),
                                                             /*implicit*/true);
      Type importedEnumTy = importedEnum->getDeclaredTypeInContext();
      auto typeRef = TypeExpr::createImplicit(importedEnumTy,
                                              Impl.SwiftContext);
      auto instantiate = new (Impl.SwiftContext) DotSyntaxCallExpr(constantRef,
                                                                   SourceLoc(),
                                                                   typeRef);
      instantiate->setType(importedEnumTy);

      Decl *CD = Impl.createConstant(name, importIntoDC, importedEnumTy,
                                     instantiate, ConstantConvertKind::None,
                                     /*isStatic*/ true, alias);
      Impl.importAttributes(alias, CD);
      return CD;
    }

    void populateInheritedTypes(NominalTypeDecl *nominal,
                                ArrayRef<ProtocolDecl *> protocols) {
      SmallVector<TypeLoc, 4> inheritedTypes;
      inheritedTypes.resize(protocols.size());
      for_each(MutableArrayRef<TypeLoc>(inheritedTypes),
               ArrayRef<ProtocolDecl *>(protocols),
               [](TypeLoc &tl, ProtocolDecl *proto) {
                 tl = TypeLoc::withoutLoc(proto->getDeclaredType());
               });
      nominal->setInherited(Impl.SwiftContext.AllocateCopy(inheritedTypes));
      nominal->setCheckedInheritanceClause();
    }

    /// Add protocol conformances and synthesized protocol attributes
    void
    addProtocolsToStruct(StructDecl *structDecl,
                         ArrayRef<KnownProtocolKind> synthesizedProtocolAttrs,
                         ArrayRef<ProtocolDecl *> protocols) {
      populateInheritedTypes(structDecl, protocols);

      // Note synthesized protocols
      for (auto kind : synthesizedProtocolAttrs)
        structDecl->getAttrs().add(new (Impl.SwiftContext)
                                       SynthesizedProtocolAttr(kind));
    }

    NominalTypeDecl *importAsOptionSetType(DeclContext *dc,
                                           Identifier name,
                                           const clang::EnumDecl *decl) {
      ASTContext &cxt = Impl.SwiftContext;
      
      // Compute the underlying type.
      auto underlyingType = Impl.importType(decl->getIntegerType(),
                                            ImportTypeKind::Enum,
                                            isInSystemModule(dc),
                                            /*isFullyBridgeable*/false);
      if (!underlyingType)
        return nullptr;

      auto Loc = Impl.importSourceLoc(decl->getLocation());

      // Create a struct with the underlying type as a field.
      auto structDecl = Impl.createDeclWithClangNode<StructDecl>(decl,
        Accessibility::Public, Loc, name, Loc, None, nullptr, dc);
      structDecl->computeType();

      ProtocolDecl *protocols[]
        = {cxt.getProtocol(KnownProtocolKind::OptionSet)};
      makeStructRawValued(structDecl, underlyingType,
                          {KnownProtocolKind::OptionSet}, protocols);
      return structDecl;
    }
    

    Decl *VisitEnumDecl(const clang::EnumDecl *decl) {
      decl = decl->getDefinition();
      if (!decl) {
        forwardDeclaration = true;
        return nullptr;
      }

      Optional<ImportedName> swift3Name;
      auto importedName = getClangDeclName(decl, swift3Name);
      if (!importedName)
        return nullptr;

      // If we've been asked to produce a Swift 2 stub, handle it via a
      // typealias.
      if (swift3Name)
        return importSwift2TypeAlias(decl, importedName, *swift3Name);

      auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
      if (!dc)
        return nullptr;
      
      ASTContext &cxt = Impl.SwiftContext;
      auto name = importedName.Imported.getBaseName();

      // Create the enum declaration and record it.
      StructDecl *errorWrapper = nullptr;
      NominalTypeDecl *result;
      NominalTypeDecl *enumeratorContext;
      auto enumInfo = Impl.getEnumInfo(decl);
      auto enumKind = enumInfo.getKind();
      switch (enumKind) {
      case EnumKind::Constants: {
        // There is no declaration. Rather, the type is mapped to the
        // underlying type.
        return nullptr;
      }

      case EnumKind::Unknown: {
        // Compute the underlying type of the enumeration.
        auto underlyingType = Impl.importType(decl->getIntegerType(),
                                              ImportTypeKind::Enum,
                                              isInSystemModule(dc),
                                              /*isFullyBridgeable*/false);
        if (!underlyingType)
          return nullptr;

        auto Loc = Impl.importSourceLoc(decl->getLocation());
        auto structDecl = Impl.createDeclWithClangNode<StructDecl>(decl,
          Accessibility::Public, Loc, name, Loc, None, nullptr, dc);
        structDecl->computeType();

        ProtocolDecl *protocols[]
          = {cxt.getProtocol(KnownProtocolKind::RawRepresentable),
             cxt.getProtocol(KnownProtocolKind::Equatable)};

        auto options = getDefaultMakeStructRawValuedOptions();
        options |= MakeStructRawValuedFlags::MakeUnlabeledValueInit;
        options -= MakeStructRawValuedFlags::IsLet;
        options -= MakeStructRawValuedFlags::IsImplicit;

        makeStructRawValued(structDecl, underlyingType,
                            {KnownProtocolKind::RawRepresentable}, protocols,
                            options,
                            /*setterAccessibility=*/Accessibility::Public);

        result = structDecl;
        enumeratorContext = structDecl;
        break;
      }

      case EnumKind::Enum: {
        auto &C = Impl.SwiftContext;
        EnumDecl *nativeDecl;
        bool declaredNative = hasNativeSwiftDecl(decl, name, dc, nativeDecl);
        if (declaredNative && nativeDecl)
          return nativeDecl;

        // Compute the underlying type.
        auto underlyingType = Impl.importType(
            decl->getIntegerType(), ImportTypeKind::Enum, isInSystemModule(dc),
            /*isFullyBridgeable*/ false);
        if (!underlyingType)
          return nullptr;

        /// Basic information about the enum type we're building.
        Identifier enumName = name;
        DeclContext *enumDC = dc;
        SourceLoc loc = Impl.importSourceLoc(decl->getLocStart());

        // If this is an error enum, form the error wrapper type,
        // which is a struct containing an NSError instance.
        ProtocolDecl *bridgedNSError = nullptr;
        ClassDecl *nsErrorDecl = nullptr;
        ProtocolDecl *errorCodeProto = nullptr;
        if (enumInfo.isErrorEnum() && 
            (bridgedNSError =
               C.getProtocol(KnownProtocolKind::BridgedStoredNSError)) &&
            (nsErrorDecl = C.getNSErrorDecl()) &&
            (errorCodeProto =
               C.getProtocol(KnownProtocolKind::ErrorCodeProtocol))) {
          // Create the wrapper struct.
          errorWrapper = Impl.createDeclWithClangNode<StructDecl>(
                           decl, Accessibility::Public, loc, name, loc,
                           None, nullptr, dc);
          errorWrapper->computeType();

          // Add inheritance clause.
          TypeLoc inheritedTypes[1] = {
            TypeLoc::withoutLoc(bridgedNSError->getDeclaredType())
          };
          errorWrapper->setInherited(C.AllocateCopy(inheritedTypes));
          errorWrapper->setCheckedInheritanceClause();

          // Set up error conformance to be lazily expanded
          errorWrapper->getAttrs().add(new (C) SynthesizedProtocolAttr(
              KnownProtocolKind::BridgedStoredNSError));

          // Create the _nsError member.
          //   public let _nsError: NSError
          auto nsErrorType = nsErrorDecl->getDeclaredInterfaceType();
          auto nsErrorProp = new (C) VarDecl(/*static*/ false, /*IsLet*/ true,
                                             loc, C.Id_nsError, nsErrorType,
                                             errorWrapper);
          nsErrorProp->setImplicit();
          nsErrorProp->setAccessibility(Accessibility::Public);

          // Create a pattern binding to describe the variable.
          Pattern *nsErrorPattern = createTypedNamedPattern(nsErrorProp);

          auto nsErrorBinding = PatternBindingDecl::create(
                                  C, loc, StaticSpellingKind::None, loc,
                                  nsErrorPattern, nullptr, errorWrapper);
          errorWrapper->addMember(nsErrorProp);
          errorWrapper->addMember(nsErrorBinding);

          // Create the _nsError initializer.
          //   public init(_nsError error: NSError)
          VarDecl *members[1] = { nsErrorProp };
          auto nsErrorInit = createValueConstructor(errorWrapper, members,
                                                    /*wantCtorParamNames=*/true,
                                                    /*wantBody=*/true);
          errorWrapper->addMember(nsErrorInit);

          // Add the domain error member.
          //   public static var _nsErrorDomain: String { return error-domain }
          addErrorDomain(errorWrapper, enumInfo.getErrorDomain(), Impl);

          // Note: the Code will be added after it's created.

          // The enum itself will be nested within the error wrapper,
          // and be named Code.
          enumDC = errorWrapper;
          enumName = C.Id_Code;
        }

        // Create the enumeration.
        auto enumDecl = Impl.createDeclWithClangNode<EnumDecl>(
            decl, Accessibility::Public, loc, enumName,
            Impl.importSourceLoc(decl->getLocation()), None, nullptr, enumDC);
        enumDecl->computeType();

        // Set up the C underlying type as its Swift raw type.
        enumDecl->setRawType(underlyingType);

        // Add the C name.
        addObjCAttribute(enumDecl,
                         Impl.importIdentifier(decl->getIdentifier()));

        // Add protocol declarations to the enum declaration.
        SmallVector<TypeLoc, 2> inheritedTypes;
        inheritedTypes.push_back(TypeLoc::withoutLoc(underlyingType));
        if (errorWrapper) {
          inheritedTypes.push_back(
            TypeLoc::withoutLoc(errorCodeProto->getDeclaredType()));
        }
        enumDecl->setInherited(C.AllocateCopy(inheritedTypes));
        enumDecl->setCheckedInheritanceClause();

        // Provide custom implementations of the init(rawValue:) and rawValue
        // conversions that just do a bitcast. We can't reliably filter a
        // C enum without additional knowledge that the type has no
        // undeclared values, and won't ever add cases.
        auto rawValueConstructor = makeEnumRawValueConstructor(Impl, enumDecl);

        auto varName = C.Id_rawValue;
        auto rawValue = new (C) VarDecl(/*static*/ false,
                                               /*IsLet*/ false,
                                               SourceLoc(), varName,
                                               underlyingType, enumDecl);
        rawValue->setImplicit();
        rawValue->setAccessibility(Accessibility::Public);
        rawValue->setSetterAccessibility(Accessibility::Private);

        // Create a pattern binding to describe the variable.
        Pattern *varPattern = createTypedNamedPattern(rawValue);

        auto rawValueBinding = PatternBindingDecl::create(
            C, SourceLoc(), StaticSpellingKind::None, SourceLoc(),
            varPattern, nullptr, enumDecl);

        auto rawValueGetter = makeEnumRawValueGetter(Impl, enumDecl, rawValue);

        enumDecl->addMember(rawValueConstructor);
        enumDecl->addMember(rawValueGetter);
        enumDecl->addMember(rawValue);
        enumDecl->addMember(rawValueBinding);

        // If we have an error wrapper, finish it up now that its
        // nested enum has been constructed.
        if (errorWrapper) {
          // Add the ErrorType alias:
          //   public typealias ErrorType
          auto alias = Impl.createDeclWithClangNode<TypeAliasDecl>(
                         decl, Accessibility::Public, loc, C.Id_ErrorType, loc,
                         TypeLoc::withoutLoc(
                           errorWrapper->getDeclaredInterfaceType()),
                         /*genericSignature=*/nullptr, enumDecl);
          alias->computeType();
          enumDecl->addMember(alias);

          // Add the 'Code' enum to the error wrapper.
          errorWrapper->addMember(enumDecl);
          result = errorWrapper;
        } else {
          result = enumDecl;
        }

        // The enumerators go into this enumeration.
        enumeratorContext = enumDecl;
        break;
      }

      case EnumKind::Options: {
        result = importAsOptionSetType(dc, name, decl);
        if (!result)
          return nullptr;

        // HACK: Make sure PrintAsObjC always omits the 'enum' tag for
        // option set enums.
        Impl.DeclsWithSuperfluousTypedefs.insert(decl);

        enumeratorContext = result;
        break;
      }
      }
      Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] = result;

      // Import each of the enumerators.
      
      bool addEnumeratorsAsMembers;
      switch (enumKind) {
      case EnumKind::Constants:
      case EnumKind::Unknown:
        addEnumeratorsAsMembers = false;
        break;
      case EnumKind::Options:
      case EnumKind::Enum:
        addEnumeratorsAsMembers = true;
        break;
      }
      
      for (auto ec = decl->enumerator_begin(), ecEnd = decl->enumerator_end();
           ec != ecEnd; ++ec) {
        Decl *enumeratorDecl;
        Decl *swift2EnumeratorDecl = nullptr;
        switch (enumKind) {
        case EnumKind::Constants:
        case EnumKind::Unknown:
          enumeratorDecl = Impl.importDecl(*ec, /*useSwift2Name=*/false);
          swift2EnumeratorDecl = Impl.importDecl(*ec, /*useSwift2Name=*/true);
          break;
        case EnumKind::Options:
          enumeratorDecl = SwiftDeclConverter(Impl, /*useSwift2Name=*/false)
                             .importOptionConstant(*ec, decl,
                                                   enumeratorContext);
          swift2EnumeratorDecl = SwiftDeclConverter(Impl,/*useSwift2Name=*/true)
                                   .importOptionConstant(*ec, decl,
                                                         enumeratorContext);
          break;
        case EnumKind::Enum:
          enumeratorDecl = SwiftDeclConverter(Impl, /*useSwift2Name=*/false)
                             .importEnumCase(*ec, decl,
                                             cast<EnumDecl>(enumeratorContext));
          swift2EnumeratorDecl = SwiftDeclConverter(Impl,/*useSwift2Name=*/true)
                                   .importEnumCase(
                                       *ec, decl,
                                       cast<EnumDecl>(enumeratorContext),
                                       enumeratorDecl);
          break;
        }
        if (!enumeratorDecl)
          continue;

        if (addEnumeratorsAsMembers) {
          // Add a member enumerator to the given nominal type.
          auto addDecl = [&](NominalTypeDecl *nominal, Decl *decl) {
            if (!decl) return;
            nominal->addMember(decl);
            if (auto *var = dyn_cast<VarDecl>(decl))
              nominal->addMember(var->getGetter());
          };

          addDecl(enumeratorContext, enumeratorDecl);
          addDecl(enumeratorContext, swift2EnumeratorDecl);
          
          // If there is an error wrapper, add an alias within the
          // wrapper to the corresponding value within the enumerator
          // context.
          if (errorWrapper) {
            auto enumeratorValue = cast<ValueDecl>(enumeratorDecl);
            auto alias = importEnumCaseAlias(enumeratorValue->getName(),
                                             *ec,
                                             enumeratorValue,
                                             decl,
                                             enumeratorContext,
                                             result);
            addDecl(result, alias);
          }
        }
      }

      // Add the type decl to ExternalDefinitions so that we can type-check
      // raw values and SILGen can emit witness tables for derived conformances.
      // FIXME: There might be better ways to do this.
      Impl.registerExternalDecl(result);
      if (result != enumeratorContext)
        Impl.registerExternalDecl(enumeratorContext);
      return result;
    }

    Decl *VisitRecordDecl(const clang::RecordDecl *decl) {
      // Track whether this record contains fields we can't reference in Swift
      // as stored properties.
      bool hasUnreferenceableStorage = false;
      
      // Track whether this record contains fields that can't be zero-
      // initialized.
      bool hasZeroInitializableStorage = true;

      // Track whether all fields in this record can be referenced in Swift,
      // either as stored or computed properties, in which case the record type
      // gets a memberwise initializer.
      bool hasMemberwiseInitializer = true;

      if (decl->isUnion()) {
        hasUnreferenceableStorage = true;

        // We generate initializers specially for unions below.
        hasMemberwiseInitializer = false;
      }

      // FIXME: Skip Microsoft __interfaces.
      if (decl->isInterface())
        return nullptr;

      // The types of anonymous structs or unions are never imported; their
      // fields are dumped directly into the enclosing class.
      if (decl->isAnonymousStructOrUnion())
        return nullptr;

      // FIXME: Figure out how to deal with incomplete types, since that
      // notion doesn't exist in Swift.
      decl = decl->getDefinition();
      if (!decl) {
        forwardDeclaration = true;
        return nullptr;
      }

      // Import the name.
      Optional<ImportedName> swift3Name;
      auto importedName = getClangDeclName(decl, swift3Name);
      if (!importedName)
        return nullptr;

      // If we've been asked to produce a Swift 2 stub, handle it via a
      // typealias.
      if (swift3Name)
        return importSwift2TypeAlias(decl, importedName, *swift3Name);

      auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
      if (!dc)
        return nullptr;

      // Create the struct declaration and record it.
      auto name = importedName.Imported.getBaseName();
      auto result = Impl.createDeclWithClangNode<StructDecl>(decl,
                                 Accessibility::Public,
                                 Impl.importSourceLoc(decl->getLocStart()),
                                 name,
                                 Impl.importSourceLoc(decl->getLocation()),
                                 None, nullptr, dc);
      result->computeType();
      Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] = result;

      // FIXME: Figure out what to do with superclasses in C++. One possible
      // solution would be to turn them into members and add conversion
      // functions.

      // Import each of the members.
      SmallVector<VarDecl *, 4> members;
      SmallVector<ConstructorDecl *, 4> ctors;

      // FIXME: Import anonymous union fields and support field access when
      // it is nested in a struct.

      for (auto m : decl->decls()) {
        auto nd = dyn_cast<clang::NamedDecl>(m);
        if (!nd) {
          // We couldn't import the member, so we can't reference it in Swift.
          hasUnreferenceableStorage = true;
          hasMemberwiseInitializer = false;
          continue;
        }

        if (auto field = dyn_cast<clang::FieldDecl>(nd)) {
          // Skip anonymous structs or unions; they'll be dealt with via the
          // IndirectFieldDecls.
          if (field->isAnonymousStructOrUnion())
            continue;

          // Non-nullable pointers can't be zero-initialized.
          if (auto nullability = field->getType()
                ->getNullability(Impl.getClangASTContext())) {
            if (*nullability == clang::NullabilityKind::NonNull)
              hasZeroInitializableStorage = false;
          }
          // TODO: If we had the notion of a closed enum with no private
          // cases or resilience concerns, then complete NS_ENUMs with
          // no case corresponding to zero would also not be zero-
          // initializable.

          // Unnamed bitfields are just for padding and should not
          // inhibit creation of a memberwise initializer.
          if (field->isUnnamedBitfield()) {
            hasUnreferenceableStorage = true;
            continue;
          }
        }

        auto member = Impl.importDecl(nd, /*useSwift2Name=*/false);
        if (!member) {
          // We don't know what this field is. Assume it may be important in C.
          hasUnreferenceableStorage = true;
          hasMemberwiseInitializer = false;
          continue;
        }

        if (isa<TypeDecl>(member)) {
          // A struct nested inside another struct will either be logically
          // a sibling of the outer struct, or contained inside of it, depending
          // on if it has a declaration name or not.
          //
          // struct foo { struct bar { ... } baz; } // sibling
          // struct foo { struct { ... } baz; } // child
          //
          // In the latter case, we add the imported type as a nested type
          // of the parent.
          //
          // TODO: C++ types have different rules.
          if (auto nominalDecl = dyn_cast<NominalTypeDecl>(member->getDeclContext())) {
            assert(nominalDecl == result && "interesting nesting of C types?");
            nominalDecl->addMember(member);
          }
          continue;
        }

        auto VD = cast<VarDecl>(member);

        // Bitfields are imported as computed properties with Clang-generated
        // accessors.
        if (auto field = dyn_cast<clang::FieldDecl>(nd)) {
          if (field->isBitField()) {
            // We can't represent this struct completely in SIL anymore,
            // but we're still able to define a memberwise initializer.
            hasUnreferenceableStorage = true;

            makeBitFieldAccessors(Impl,
                                  const_cast<clang::RecordDecl *>(decl),
                                  result,
                                  const_cast<clang::FieldDecl *>(field),
                                  VD);
          }
        }

        if (decl->isUnion()) {
          // Union fields should only be available indirectly via a computed
          // property. Since the union is made of all of the fields at once,
          // this is a trivial accessor that casts self to the correct
          // field type.

          // FIXME: Allow indirect field access of anonymous structs.
          if (isa<clang::IndirectFieldDecl>(nd))
            continue;

          Decl *getter, *setter;
          std::tie(getter, setter) = makeUnionFieldAccessors(Impl, result, VD);
          members.push_back(VD);

          // Create labeled initializers for unions that take one of the
          // fields, which only initializes the data for that field.
          auto valueCtor =
              createValueConstructor(result, VD,
                                     /*want param names*/true,
                                     /*wantBody=*/!Impl.hasFinishedTypeChecking());
          ctors.push_back(valueCtor);
        } else {
          members.push_back(VD);
        }
      }

      bool hasReferenceableFields = !members.empty();

      if (hasZeroInitializableStorage) {
        // Add constructors for the struct.
        ctors.push_back(createDefaultConstructor(result));
        if (hasReferenceableFields && hasMemberwiseInitializer) {
          // The default zero initializer suppresses the implicit value
          // constructor that would normally be formed, so we have to add that
          // explicitly as well.
          //
          // If we can completely represent the struct in SIL, leave the body
          // implicit, otherwise synthesize one to call property setters.
          bool wantBody = (hasUnreferenceableStorage &&
                           !Impl.hasFinishedTypeChecking());
          auto valueCtor = createValueConstructor(result, members,
                                                  /*want param names*/true,
                                                  /*want body*/wantBody);
          if (!hasUnreferenceableStorage)
            valueCtor->setIsMemberwiseInitializer();

          ctors.push_back(valueCtor);
        }
      }

      for (auto member : members) {
        result->addMember(member);
      }
      
      for (auto ctor : ctors) {
        result->addMember(ctor);
      }

      result->setHasUnreferenceableStorage(hasUnreferenceableStorage);

      // Add the struct decl to ExternalDefinitions so that IRGen can emit
      // metadata for it.
      // FIXME: There might be better ways to do this.
      Impl.registerExternalDecl(result);

      return result;
    }

    Decl *VisitClassTemplateSpecializationDecl(
                 const clang::ClassTemplateSpecializationDecl *decl) {
      // FIXME: We could import specializations, but perhaps only as unnamed
      // structural types.
      return nullptr;
    }

    Decl *VisitClassTemplatePartialSpecializationDecl(
                 const clang::ClassTemplatePartialSpecializationDecl *decl) {
      // Note: templates are not imported.
      return nullptr;
    }

    Decl *VisitTemplateTypeParmDecl(const clang::TemplateTypeParmDecl *decl) {
      // Note: templates are not imported.
      return nullptr;
    }

    Decl *VisitEnumConstantDecl(const clang::EnumConstantDecl *decl) {
      auto clangEnum = cast<clang::EnumDecl>(decl->getDeclContext());
      
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);
      if (!importedName) return nullptr;

      auto name = importedName.Imported.getBaseName();
      if (name.empty())
        return nullptr;

      switch (Impl.getEnumKind(clangEnum)) {
      case EnumKind::Constants: {
        // The enumeration was simply mapped to an integral type. Create a
        // constant with that integral type.

        // The context where the constant will be introduced.
        auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
        if (!dc)
          return nullptr;

        // Enumeration type.
        auto &clangContext = Impl.getClangASTContext();
        auto type = Impl.importType(clangContext.getTagDeclType(clangEnum),
                                    ImportTypeKind::Value,
                                    isInSystemModule(dc),
                                    /*isFullyBridgeable*/false);
        if (!type)
          return nullptr;
        // FIXME: Importing the type will recursively revisit this same
        // EnumConstantDecl. Short-circuit out if we already emitted the import
        // for this decl.
        if (auto Known = Impl.importDeclCached(decl, useSwift2Name))
          return Known;

        // Create the global constant.
        auto result = Impl.createConstant(name, dc, type,
                                          clang::APValue(decl->getInitVal()),
                                          ConstantConvertKind::Coerce,
                                          /*static*/ false, decl);
        Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] = result;

        // If this is a Swift 2 stub, mark it as such.
        if (swift3Name)
          markAsSwift2Variant(result, *swift3Name);

        return result;
      }

      case EnumKind::Unknown: {
        // The enumeration was mapped to a struct containing the integral
        // type. Create a constant with that struct type.

        // The context where the constant will be introduced.
        auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
        if (!dc)
          return nullptr;

        // Import the enumeration type.
        auto enumType = Impl.importType(
                          Impl.getClangASTContext().getTagDeclType(clangEnum),
                          ImportTypeKind::Value,
                          isInSystemModule(dc),
                          /*isFullyBridgeable*/false);
        if (!enumType)
          return nullptr;

        // FIXME: Importing the type will can recursively revisit this same
        // EnumConstantDecl. Short-circuit out if we already emitted the import
        // for this decl.
        if (auto Known = Impl.importDeclCached(decl, useSwift2Name))
          return Known;

        // Create the global constant.
        auto result = Impl.createConstant(name, dc, enumType,
                                          clang::APValue(decl->getInitVal()),
                                          ConstantConvertKind::Construction,
                                          /*static*/ false, decl);
        Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] = result;

        // If this is a Swift 2 stub, mark it as such.
        if (swift3Name)
          markAsSwift2Variant(result, *swift3Name);

        return result;
      }

      case EnumKind::Enum:
      case EnumKind::Options: {
        // The enumeration was mapped to a high-level Swift type, and its
        // elements were created as children of that enum. They aren't available
        // independently.

        // FIXME: This is gross. We shouldn't have to import
        // everything to get at the individual constants.
        return nullptr;
      }
      }
    }


    Decl *
    VisitUnresolvedUsingValueDecl(const clang::UnresolvedUsingValueDecl *decl) {
      // Note: templates are not imported.
      return nullptr;
    }

    Decl *VisitIndirectFieldDecl(const clang::IndirectFieldDecl *decl) {
      // Check whether the context of any of the fields in the chain is a
      // union. If so, don't import this field.
      for (auto f = decl->chain_begin(), fEnd = decl->chain_end(); f != fEnd;
           ++f) {
        if (auto record = dyn_cast<clang::RecordDecl>((*f)->getDeclContext())) {
          if (record->isUnion())
            return nullptr;
        }
      }

      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);
      if (!importedName) return nullptr;

      auto name = importedName.Imported.getBaseName();

      auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
      if (!dc)
        return nullptr;

      auto type = Impl.importType(decl->getType(),
                                  ImportTypeKind::Variable,
                                  isInSystemModule(dc),
                                  /*isFullyBridgeable*/false);
      if (!type)
        return nullptr;

      // Map this indirect field to a Swift variable.
      auto result = Impl.createDeclWithClangNode<VarDecl>(decl,
                       Accessibility::Public,
                       /*static*/ false, /*IsLet*/ false,
                       Impl.importSourceLoc(decl->getLocStart()),
                       name, type, dc);

      // If this is a Swift 2 stub, mark is as such.
      if (swift3Name)
        markAsSwift2Variant(result, *swift3Name);

      return result;
    }

    ParameterList *getNonSelfParamList(
        DeclContext *dc, const clang::FunctionDecl *decl,
        Optional<unsigned> selfIdx, ArrayRef<Identifier> argNames,
        bool allowNSUIntegerAsInt, bool isAccessor) {
      if (bool(selfIdx)) {
        assert(((decl->getNumParams() == argNames.size() + 1) || isAccessor) &&
               (*selfIdx < decl->getNumParams()) && "where's self?");
      } else {
        assert(decl->getNumParams() == argNames.size() || isAccessor);
      }

      SmallVector<const clang::ParmVarDecl *, 4> nonSelfParams;
      for (unsigned i = 0; i < decl->getNumParams(); ++i) {
        if (selfIdx && i == *selfIdx)
          continue;
        nonSelfParams.push_back(decl->getParamDecl(i));
      }
      return Impl.importFunctionParameterList(dc, decl, nonSelfParams,
                                              decl->isVariadic(),
                                              allowNSUIntegerAsInt, argNames);
    }

    Decl *importGlobalAsInitializer(const clang::FunctionDecl *decl,
                                    DeclName name, DeclContext *dc,
                                    CtorInitializerKind initKind) {
      // TODO: Should this be an error? How can this come up?
      assert(dc->isTypeContext() && "cannot import as member onto non-type");

      // Check for some invalid imports
      if (dc->getAsProtocolOrProtocolExtensionContext()) {
        // FIXME: clang source location
        Impl.SwiftContext.Diags.diagnose({}, diag::swift_name_protocol_static,
                                         /*isInit=*/true);
        Impl.SwiftContext.Diags.diagnose({}, diag::note_while_importing,
                                         decl->getName());
        return nullptr;
      }

      bool allowNSUIntegerAsInt =
        Impl.shouldAllowNSUIntegerAsInt(isInSystemModule(dc), decl);

      ArrayRef<Identifier> argNames = name.getArgumentNames();

      ParameterList *parameterList = nullptr;
      if (argNames.size() == 1 && decl->getNumParams() == 0) {
        // Special case: We need to create an empty first parameter for our
        // argument label
        parameterList =
            ParameterList::createWithoutLoc(new (Impl.SwiftContext) ParamDecl(
                /*IsLet=*/true, SourceLoc(), SourceLoc(),
                argNames.front(), SourceLoc(),
                argNames.front(), Impl.SwiftContext.TheEmptyTupleType, dc));
      } else {
        parameterList = Impl.importFunctionParameterList(
            dc, decl, {decl->param_begin(), decl->param_end()},
            decl->isVariadic(), allowNSUIntegerAsInt, argNames);
      }
      if (!parameterList)
        return nullptr;

      bool selfIsInOut =
          !dc->getDeclaredInterfaceType()->hasReferenceSemantics();
      auto selfParam = ParamDecl::createSelf(SourceLoc(), dc, /*static=*/false,
                                             /*inout=*/selfIsInOut);

      OptionalTypeKind initOptionality;
      auto resultType = Impl.importFunctionReturnType(
          dc, decl, decl->getReturnType(), allowNSUIntegerAsInt);
      (void)resultType->getAnyOptionalObjectType(initOptionality);

      auto result = Impl.createDeclWithClangNode<ConstructorDecl>(
          decl, Accessibility::Public, name, /*NameLoc=*/SourceLoc(),
          initOptionality, /*FailabilityLoc=*/SourceLoc(),
          /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
          selfParam, parameterList,
          /*GenericParams=*/nullptr, dc);
      result->setInitKind(initKind);
      result->setImportAsStaticMember();

      // Set the constructor's type(s).
      Type argType = parameterList->getType(Impl.SwiftContext);
      Type fnType = FunctionType::get(argType, resultType);
      Type selfType = selfParam->getType();
      Type initType = FunctionType::get(selfType, fnType);
      result->setInitializerInterfaceType(initType);
      Type selfMetaType = MetatypeType::get(selfType->getInOutObjectType());
      Type allocType = FunctionType::get(selfMetaType, fnType);
      result->setType(allocType);
      result->setInterfaceType(allocType);

      finishFuncDecl(decl, result);
      return result;
    }

    Decl *importGlobalAsMethod(const clang::FunctionDecl *decl, DeclName name,
                               DeclContext *dc, Optional<unsigned> selfIdx) {
      if (dc->getAsProtocolOrProtocolExtensionContext() && !selfIdx) {
        // FIXME: source location...
        Impl.SwiftContext.Diags.diagnose({}, diag::swift_name_protocol_static,
                                         /*isInit=*/false);
        Impl.SwiftContext.Diags.diagnose({}, diag::note_while_importing,
                                         decl->getName());
        return nullptr;
      }

      if (!decl->hasPrototype()) {
        // FIXME: source location...
        Impl.SwiftContext.Diags.diagnose({}, diag::swift_name_no_prototype);
        Impl.SwiftContext.Diags.diagnose({}, diag::note_while_importing,
                                         decl->getName());
        return nullptr;
      }

      bool allowNSUIntegerAsInt =
          Impl.shouldAllowNSUIntegerAsInt(isInSystemModule(dc), decl);

      auto &C = Impl.SwiftContext;
      SmallVector<ParameterList *, 2> bodyParams;

      // There is an inout 'self' when we have an instance method of a
      // value-semantic type whose 'self' parameter is a
      // pointer-to-non-const.
      bool selfIsInOut = false;
      if (selfIdx && !dc->getDeclaredTypeOfContext()->hasReferenceSemantics()) {
        auto selfParam = decl->getParamDecl(*selfIdx);
        auto selfParamTy = selfParam->getType();
        if ((selfParamTy->isPointerType() || selfParamTy->isReferenceType()) &&
            !selfParamTy->getPointeeType().isConstQualified())
          selfIsInOut = true;
      }

      bodyParams.push_back(ParameterList::createWithoutLoc(
          ParamDecl::createSelf(SourceLoc(), dc, !selfIdx.hasValue(),
                                selfIsInOut)));
      bodyParams.push_back(getNonSelfParamList(
          dc, decl, selfIdx, name.getArgumentNames(), allowNSUIntegerAsInt, !name));

      auto swiftResultTy = Impl.importFunctionReturnType(
          dc, decl, decl->getReturnType(), allowNSUIntegerAsInt);
      auto fnType = ParameterList::getFullInterfaceType(swiftResultTy, bodyParams,
                                                        dc);

      auto loc = Impl.importSourceLoc(decl->getLocation());
      auto nameLoc = Impl.importSourceLoc(decl->getLocation());
      auto result =
        FuncDecl::create(C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
                         /*FuncLoc=*/loc, name, nameLoc,
                         /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                         /*AccessorKeywordLoc=*/SourceLoc(),
                         /*GenericParams=*/nullptr, bodyParams, Type(),
                         TypeLoc::withoutLoc(swiftResultTy), dc, decl);

      Type interfaceType;
      std::tie(fnType, interfaceType) =
          getGenericMethodType(dc, fnType->castTo<AnyFunctionType>());
      result->setType(fnType);
      result->setInterfaceType(interfaceType);
      result->setGenericSignature(dc->getGenericSignatureOfContext());

      result->setBodyResultType(swiftResultTy);
      result->setAccessibility(Accessibility::Public);
      if (selfIsInOut)
        result->setMutating();
      if (selfIdx) {
        result->setSelfIndex(selfIdx.getValue());
      } else {
        result->setStatic();
        result->setImportAsStaticMember();
      }
      assert(selfIdx ? result->getSelfIndex() == *selfIdx
                     : result->isImportAsStaticMember());

      if (dc->getAsClassOrClassExtensionContext())
        // FIXME: only if the class itself is not marked final
        result->getAttrs().add(new (C) FinalAttr(/*IsImplicit=*/true));

      finishFuncDecl(decl, result);
      return result;
    }

    /// Create an implicit property given the imported name of one of
    /// the accessors.
    VarDecl *getImplicitProperty(ImportedName importedName,
                                 const clang::FunctionDecl *accessor) {
      // Check whether we already know about the property.
      auto knownProperty = Impl.FunctionsAsProperties.find(accessor);
      if (knownProperty != Impl.FunctionsAsProperties.end())
        return knownProperty->second;

      typedef ClangImporter::Implementation::ImportedAccessorKind
        ImportedAccessorKind;

      // Determine whether we have the getter or setter.
      const clang::FunctionDecl *getter = nullptr;
      ImportedName getterName;
      Optional<ImportedName> swift3GetterName;
      const clang::FunctionDecl *setter = nullptr;
      ImportedName setterName;
      Optional<ImportedName> swift3SetterName;
      switch (importedName.AccessorKind) {
      case ImportedAccessorKind::None:
      case ImportedAccessorKind::SubscriptGetter:
      case ImportedAccessorKind::SubscriptSetter:
        llvm_unreachable("Not a property accessor");

      case ImportedAccessorKind::PropertyGetter:
        getter = accessor;
        getterName = importedName;
        break;

      case ImportedAccessorKind::PropertySetter:
        setter = accessor;
        setterName = importedName;
        break;
      }

      // Find the other accessor, if it exists.
      auto propertyName = importedName.Imported.getBaseName();
      auto lookupTable =
        Impl.findLookupTable(*Impl.getClangSubmoduleForDecl(accessor));
      assert(lookupTable && "No lookup table?");
      bool foundAccessor = false;
      for (auto entry : lookupTable->lookup(propertyName.str(),
                                            importedName.EffectiveContext)) {
        auto decl = entry.dyn_cast<clang::NamedDecl *>();
        if (!decl) continue;

        auto function = dyn_cast<clang::FunctionDecl>(decl);
        if (!function) continue;

        if (function->getCanonicalDecl() == accessor->getCanonicalDecl()) {
          foundAccessor = true;
          continue;
        }

        if (!getter) {
          // Find the self index for the getter.
          getterName = importFullName(function, swift3GetterName);
          if (!getterName) continue;

          getter = function;
          continue;
        }

        if (!setter) {
          // Find the self index for the setter.
          setterName = importFullName(function, swift3SetterName);
          if (!setterName) continue;

          setter = function;
          continue;
        }

        // We already have both a getter and a setter; something is
        // amiss, so bail out.
        return nullptr;
      }

      assert(foundAccessor && "Didn't find the original accessor? "
                              "Try clearing your module cache");

      // If there is no getter, there's nothing we can do.
      if (!getter) return nullptr;

      // Retrieve the type of the property that is implied by the getter.
      auto propertyType =
        ClangImporter::Implementation::getAccessorPropertyType(
          getter, false, getterName.SelfIndex);
      if (propertyType.isNull()) return nullptr;

      // If there is a setter, check that the property it implies
      // matches that of the getter.
      if (setter) {
        auto setterPropertyType =
          ClangImporter::Implementation::getAccessorPropertyType(
            setter, true, setterName.SelfIndex);
        if (setterPropertyType.isNull()) return nullptr;

        // If the inferred property types don't match up, we can't
        // form a property.
        if (!getter->getASTContext().hasSameType(propertyType,
                                                 setterPropertyType))
          return nullptr;
      }

      // Import the property's context.
      auto dc = Impl.importDeclContextOf(getter, getterName.EffectiveContext);
      if (!dc) return nullptr;

      // Is this a static property?
      bool isStatic = false;
      if (dc->isTypeContext() && !getterName.SelfIndex)
        isStatic = true;

      // Compute the property type.
      bool isFromSystemModule = isInSystemModule(dc);
      Type swiftPropertyType =
        Impl.importType(propertyType, ImportTypeKind::Property,
                        Impl.shouldAllowNSUIntegerAsInt(isFromSystemModule,
                                                        getter),
                        /*isFullyBridgeable*/ true,
                        OTK_ImplicitlyUnwrappedOptional);
      if (!swiftPropertyType) return nullptr;

      auto property = Impl.createDeclWithClangNode<VarDecl>(getter,
                                                        Accessibility::Public,
                                                            isStatic,
                                                            /*isLet=*/false,
                                                            SourceLoc(),
                                                            propertyName,
                                                            swiftPropertyType,
                                                            dc);

      // Note that we've formed this property.
      Impl.FunctionsAsProperties[getter] = property;
      if (setter) Impl.FunctionsAsProperties[setter] = property;

      // If this property is in a class or class extension context,
      // add "final".
      if (dc->getAsClassOrClassExtensionContext())
        property->getAttrs().add(new (Impl.SwiftContext)
                                     FinalAttr(/*IsImplicit=*/true));

      // Import the getter.
      FuncDecl *swiftGetter =
        dyn_cast_or_null<FuncDecl>(VisitFunctionDecl(getter, getterName, None,
                                                     property));
      if (!swiftGetter) return nullptr;
      Impl.importAttributes(getter, swiftGetter);
      Impl.ImportedDecls[{getter, useSwift2Name}] = swiftGetter;
      if (swift3GetterName)
        markAsSwift2Variant(swiftGetter, *swift3GetterName);

      // Import the setter.
      FuncDecl *swiftSetter = nullptr;
      if (setter) {
        swiftSetter = dyn_cast_or_null<FuncDecl>(
                        VisitFunctionDecl(setter, setterName, None, property));
        if (!swiftSetter) return nullptr;
        Impl.importAttributes(setter, swiftSetter);
        Impl.ImportedDecls[{setter, useSwift2Name}] = swiftSetter;
        if (swift3SetterName)
          markAsSwift2Variant(swiftSetter, *swift3SetterName);
      }

      // Make this a computed property.
      property->makeComputed(SourceLoc(), swiftGetter, swiftSetter, nullptr,
                             SourceLoc());

      // Make the property the alternate declaration for the getter.
      Impl.AlternateDecls[swiftGetter] = property;

      return property;
    }

    Decl *VisitFunctionDecl(const clang::FunctionDecl *decl) {
      // Import the name of the function.
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);
      if (!importedName)
        return nullptr;

      typedef ClangImporter::Implementation::ImportedAccessorKind
        ImportedAccessorKind;

      AbstractStorageDecl *owningStorage;
      switch (importedName.AccessorKind) {
      case ImportedAccessorKind::None:
        owningStorage = nullptr;
        break;

      case ImportedAccessorKind::SubscriptGetter:
      case ImportedAccessorKind::SubscriptSetter:
        llvm_unreachable("Not possible for a function");

      case ImportedAccessorKind::PropertyGetter: {
        auto property = getImplicitProperty(importedName, decl);
        if (!property) return nullptr;
        return property->getGetter();
      }

      case ImportedAccessorKind::PropertySetter:
        auto property = getImplicitProperty(importedName, decl);
        if (!property) return nullptr;
        return property->getSetter();
      }

      return VisitFunctionDecl(decl, importedName, swift3Name, nullptr);
    }

    Decl *VisitFunctionDecl(
            const clang::FunctionDecl *decl,
            ImportedName importedName,
            Optional<ImportedName> swift3Name,
            AbstractStorageDecl *owningStorage) {
      auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
      if (!dc)
        return nullptr;

      DeclName name = owningStorage ? DeclName() : importedName.Imported;
      bool hasCustomName = importedName.HasCustomName;

      if (importedName.ImportAsMember) {
        assert(!swift3Name && "Swift 2 didn't support import-as-member!");

        // Handle initializers.
        if (name.getBaseName() == Impl.SwiftContext.Id_init)
          return importGlobalAsInitializer(decl, name, dc,
                                           importedName.InitKind);

        // Everything else is a method.
        return importGlobalAsMethod(decl, name, dc, importedName.SelfIndex);
      }

      // Import the function type. If we have parameters, make sure their names
      // get into the resulting function type.
      ParameterList *bodyParams = nullptr;
      Type type = Impl.importFunctionType(dc,
                                          decl,
                                          decl->getReturnType(),
                                          { decl->param_begin(),
                                            decl->param_size() },
                                          decl->isVariadic(),
                                          decl->isNoReturn(),
                                          isInSystemModule(dc),
                                          hasCustomName, bodyParams, name);
      if (!type)
        return nullptr;

      auto resultTy = type->castTo<FunctionType>()->getResult();
      auto loc = Impl.importSourceLoc(decl->getLocation());

      if (name && name.isSimpleName()) {
        assert(hasCustomName && "imported function with simple name?");
        // Just fill in empty argument labels.
        name = DeclName(Impl.SwiftContext, name.getBaseName(), bodyParams);
      }

      // FIXME: Poor location info.
      auto nameLoc = Impl.importSourceLoc(decl->getLocation());
      auto result = FuncDecl::create(
          Impl.SwiftContext, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
          /*FuncLoc=*/loc, name, nameLoc,
          /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
          /*AccessorKeywordLoc=*/SourceLoc(),
          /*GenericParams=*/nullptr, bodyParams, type,
          TypeLoc::withoutLoc(resultTy), dc, decl);

      result->setInterfaceType(type);
      result->setBodyResultType(resultTy);

      // Someday, maybe this will need to be 'open' for C++ virtual methods.
      result->setAccessibility(Accessibility::Public);
      finishFuncDecl(decl, result);

      // If this is a Swift 2 stub, mark it as such.
      if (swift3Name)
        markAsSwift2Variant(result, *swift3Name);

      return result;
    }

    void finishFuncDecl(const clang::FunctionDecl *decl,
                        AbstractFunctionDecl *result) {
      // Keep track of inline function bodies so that we can generate
      // IR from them using Clang's IR generator.
      if ((decl->isInlined() || decl->hasAttr<clang::AlwaysInlineAttr>() ||
           !decl->isExternallyVisible()) &&
          decl->hasBody()) {
        Impl.registerExternalDecl(result);
      }

      // Set availability.
      if (decl->isVariadic()) {
        Impl.markUnavailable(result, "Variadic function is unavailable");
      }
    }

    Decl *VisitCXXMethodDecl(const clang::CXXMethodDecl *decl) {
      // FIXME: Import C++ member functions as methods.
      return nullptr;
    }

    Decl *VisitFieldDecl(const clang::FieldDecl *decl) {
      // Fields are imported as variables.
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);
      if (!importedName) return nullptr;

      auto name = importedName.Imported.getBaseName();

      auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
      if (!dc)
        return nullptr;

      auto type = Impl.importType(decl->getType(),
                                  ImportTypeKind::RecordField,
                                  isInSystemModule(dc),
                                  /*isFullyBridgeable*/false);
      if (!type)
        return nullptr;

      auto result =
        Impl.createDeclWithClangNode<VarDecl>(decl, Accessibility::Public,
                              /*static*/ false, /*IsLet*/ false,
                              Impl.importSourceLoc(decl->getLocation()),
                              name, type, dc);

      // Handle attributes.
      if (decl->hasAttr<clang::IBOutletAttr>())
        result->getAttrs().add(
            new (Impl.SwiftContext) IBOutletAttr(/*IsImplicit=*/false));
      // FIXME: Handle IBOutletCollection.

      // If this is a Swift 2 stub, handle it as such.
      if (swift3Name)
        markAsSwift2Variant(result, *swift3Name);

      return result;
    }

    Decl *VisitObjCIvarDecl(const clang::ObjCIvarDecl *decl) {
      // Disallow direct ivar access (and avoid conflicts with property names).
      return nullptr;
    }

    Decl *VisitObjCAtDefsFieldDecl(const clang::ObjCAtDefsFieldDecl *decl) {
      // @defs is an anachronism; ignore it.
      return nullptr;
    }

    Decl *VisitVarDecl(const clang::VarDecl *decl) {
      // FIXME: Swift does not have static variables in structs/classes yet.
      if (decl->getDeclContext()->isRecord())
        return nullptr;

      // Variables are imported as... variables.
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);
      if (!importedName) return nullptr;

      auto name = importedName.Imported.getBaseName();
      auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
      if (!dc)
        return nullptr;

      // If the declaration is const, consider it audited.
      // We can assume that loading a const global variable doesn't
      // involve an ownership transfer.
      bool isAudited = decl->getType().isConstQualified();

      auto declType = decl->getType();

      // Special case: NS Notifications
      if (ClangImporter::Implementation::isNSNotificationGlobal(decl))
        if (auto newtypeDecl =
                Impl.findSwiftNewtype(decl, Impl.getClangSema(), false))
          declType = Impl.getClangASTContext().getTypedefType(newtypeDecl);

      Type type = Impl.importType(declType,
                                  (isAudited ? ImportTypeKind::AuditedVariable
                                   : ImportTypeKind::Variable),
                                  isInSystemModule(dc),
                                  /*isFullyBridgeable*/false);

      if (!type)
        return nullptr;

      // If we've imported this variable as a member, it's a static
      // member.
      bool isStatic = false;
      if (dc->isTypeContext())
        isStatic = true;

      auto result = Impl.createDeclWithClangNode<VarDecl>(decl,
                       Accessibility::Public, isStatic,
                       Impl.shouldImportGlobalAsLet(decl->getType()),
                       Impl.importSourceLoc(decl->getLocation()),
                       name, type, dc);

      // If imported as member, the member should be final.
      if (dc->getAsClassOrClassExtensionContext())
        result->getAttrs().add(new (Impl.SwiftContext)
                                 FinalAttr(/*IsImplicit=*/true));

      if (!decl->hasExternalStorage())
        Impl.registerExternalDecl(result);

      // If this is a Swift 2 stub, mark it as such.
      if (swift3Name)
        markAsSwift2Variant(result, *swift3Name);

      return result;
    }

    Decl *VisitImplicitParamDecl(const clang::ImplicitParamDecl *decl) {
      // Parameters are never directly imported.
      return nullptr;
    }

    Decl *VisitParmVarDecl(const clang::ParmVarDecl *decl) {
      // Parameters are never directly imported.
      return nullptr;
    }

    Decl *
    VisitNonTypeTemplateParmDecl(const clang::NonTypeTemplateParmDecl *decl) {
      // Note: templates are not imported.
      return nullptr;
    }

    Decl *VisitTemplateDecl(const clang::TemplateDecl *decl) {
      // Note: templates are not imported.
      return nullptr;
    }

    Decl *VisitUsingDecl(const clang::UsingDecl *decl) {
      // Using declarations are not imported.
      return nullptr;
    }

    Decl *VisitUsingShadowDecl(const clang::UsingShadowDecl *decl) {
      // Using shadow declarations are not imported; rather, name lookup just
      // looks through them.
      return nullptr;
    }

    /// Add an @objc(name) attribute with the given, optional name expressed as
    /// selector.
    ///
    /// The importer should use this rather than adding the attribute directly.
    void addObjCAttribute(ValueDecl *decl, Optional<ObjCSelector> name) {
      auto &ctx = Impl.SwiftContext;
      decl->getAttrs().add(ObjCAttr::create(ctx, name, /*implicit=*/true));

      // If the declaration we attached the 'objc' attribute to is within a
      // class, record it in the class.
      if (auto contextTy = decl->getDeclContext()->getDeclaredInterfaceType()) {
        if (auto classDecl = contextTy->getClassOrBoundGenericClass()) {
          if (auto method = dyn_cast<AbstractFunctionDecl>(decl)) {
            classDecl->recordObjCMethod(method);
          }
        }
      }
    }

    /// Add an @objc(name) attribute with the given, optional name expressed as
    /// selector.
    ///
    /// The importer should use this rather than adding the attribute directly.
    void addObjCAttribute(ValueDecl *decl, Identifier name) {
      addObjCAttribute(decl, ObjCSelector(Impl.SwiftContext, 0, name));
    }

    Decl *VisitObjCMethodDecl(const clang::ObjCMethodDecl *decl) {
      auto dc = Impl.importDeclContextOf(decl, decl->getDeclContext());
      if (!dc)
        return nullptr;

      // While importing the DeclContext, we might have imported the decl
      // itself.
      if (auto Known = Impl.importDeclCached(decl, useSwift2Name))
        return Known;

      return VisitObjCMethodDecl(decl, dc);
    }

    /// Check whether we have already imported a method with the given
    /// selector in the given context.
    bool methodAlreadyImported(ObjCSelector selector, bool isInstance,
                               DeclContext *dc) {
      // We only need to perform this check for classes.
      auto classDecl
        = dc->getDeclaredInterfaceType()->getClassOrBoundGenericClass();
      if (!classDecl)
        return false;

      // Make sure we don't search in Clang modules for this method.
      ++Impl.ActiveSelectors[{selector, isInstance}];

      // Look for a matching imported or deserialized member.
      bool result = false;
      for (auto decl : classDecl->lookupDirect(selector, isInstance)) {
        if (decl->getClangDecl()
            || !decl->getDeclContext()->getParentSourceFile()) {
          result = true;
          break;
        }
      }

      // Restore the previous active count in the active-selector mapping.
      auto activeCount = Impl.ActiveSelectors.find({selector, isInstance});
      --activeCount->second;
      if (activeCount->second == 0)
        Impl.ActiveSelectors.erase(activeCount);

      return result;
    }

    /// If the given method is a factory method, import it as a constructor
    Optional<ConstructorDecl *>
    importFactoryMethodAsConstructor(Decl *member,
                                     const clang::ObjCMethodDecl *decl,
                                     ObjCSelector selector,
                                     DeclContext *dc) {
      // Import the full name of the method.
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);

      // Check that we imported an initializer name.
      DeclName initName = importedName;
      if (initName.getBaseName() != Impl.SwiftContext.Id_init) return None;

      // ... that came from a factory method.
      if (importedName.InitKind != CtorInitializerKind::Factory &&
          importedName.InitKind != CtorInitializerKind::ConvenienceFactory)
        return None;

      bool redundant = false;
      auto result = importConstructor(decl, dc, false, importedName.InitKind,
                                      /*required=*/false, selector,
                                      importedName,
                                      {decl->param_begin(), decl->param_size()},
                                      decl->isVariadic(), redundant);

      if ((result || redundant) && member) {
        ++NumFactoryMethodsAsInitializers;

        // Mark the imported class method "unavailable", with a useful error
        // message.
        // TODO: Could add a replacement string?
        llvm::SmallString<64> message;
        llvm::raw_svector_ostream os(message);
        os << "use object construction '"
           << decl->getClassInterface()->getName() << "(";
        for (auto arg : initName.getArgumentNames()) {
          os << arg << ":";
        }
        os << ")'";
        member->getAttrs().add(
          AvailableAttr::createUnconditional(
            Impl.SwiftContext, 
            Impl.SwiftContext.AllocateCopy(os.str())));
      }

      /// Record the initializer as an alternative declaration for the
      /// member.
      if (result) {
        Impl.AlternateDecls[member] = result;

        if (swift3Name)
          markAsSwift2Variant(result, *swift3Name);
      }

      return result;
    }

    /// Determine if the given Objective-C instance method should also
    /// be imported as a class method.
    ///
    /// Objective-C root class instance methods are also reflected as
    /// class methods.
    bool shouldAlsoImportAsClassMethod(FuncDecl *method) {
      // Only instance methods.
      if (!method->isInstanceMember()) return false;

      // Must be a method within a class or extension thereof.
      auto classDecl =
        method->getDeclContext()->getAsClassOrClassExtensionContext();
      if (!classDecl) return false;

      // The class must not have a superclass.
      if (classDecl->getSuperclass()) return false;

      // There must not already be a class method with the same
      // selector.
      auto objcClass =
        cast_or_null<clang::ObjCInterfaceDecl>(classDecl->getClangDecl());
      if (!objcClass) return false;

      auto objcMethod =
        cast_or_null<clang::ObjCMethodDecl>(method->getClangDecl());
      if (!objcMethod) return false;
      return !objcClass->getClassMethod(objcMethod->getSelector(),
                                        /*AllowHidden=*/true);
    }

    Decl *VisitObjCMethodDecl(const clang::ObjCMethodDecl *decl,
                              DeclContext *dc) {
      return VisitObjCMethodDecl(decl, dc, false);
    }

  private:
    Decl *VisitObjCMethodDecl(const clang::ObjCMethodDecl *decl,
                              DeclContext *dc,
                              bool forceClassMethod) {
      // If we have an init method, import it as an initializer.
      if (Impl.isInitMethod(decl)) {
        // Cannot force initializers into class methods.
        if (forceClassMethod)
          return nullptr;

        return importConstructor(decl, dc, /*isImplicit=*/false, None,
                                 /*required=*/false);
      }

      // Check whether we already imported this method.
      if (!forceClassMethod &&
          dc == Impl.importDeclContextOf(decl, decl->getDeclContext())) {
        // FIXME: Should also be able to do this for forced class
        // methods.
        auto known = Impl.ImportedDecls.find({decl->getCanonicalDecl(),
                                              useSwift2Name});
        if (known != Impl.ImportedDecls.end())
          return known->second;
      }

      // Check whether another method with the same selector has already been
      // imported into this context.
      ObjCSelector selector = Impl.importSelector(decl->getSelector());
      bool isInstance = decl->isInstanceMethod() && !forceClassMethod;
      if (!useSwift2Name && methodAlreadyImported(selector, isInstance, dc))
        return nullptr;

      Optional<ImportedName> swift3Name;
      auto importedName
        = importFullName(decl, swift3Name,
                         ImportNameFlags::SuppressFactoryMethodAsInit);
      if (!importedName)
        return nullptr;

      assert(dc->getDeclaredTypeOfContext() && "Method in non-type context?");
      assert(isa<ClangModuleUnit>(dc->getModuleScopeContext()) &&
             "Clang method in Swift context?");

      // FIXME: We should support returning "Self.Type" for a root class
      // instance method mirrored as a class method, but it currently causes
      // problems for the type checker.
      if (forceClassMethod && decl->hasRelatedResultType())
        return nullptr;

      // Add the implicit 'self' parameter patterns.
      SmallVector<ParameterList *, 4> bodyParams;
      auto selfVar =
        ParamDecl::createSelf(SourceLoc(), dc, /*isStatic*/!isInstance);
      bodyParams.push_back(ParameterList::createWithoutLoc(selfVar));
      Type selfInterfaceType = dc->getSelfInterfaceType();
      if (!isInstance) {
        selfInterfaceType = MetatypeType::get(selfInterfaceType);
      }

      SpecialMethodKind kind = SpecialMethodKind::Regular;
      // FIXME: This doesn't handle implicit properties.
      if (decl->isPropertyAccessor())
        kind = SpecialMethodKind::PropertyAccessor;
      else if (isNSDictionaryMethod(decl, Impl.objectForKeyedSubscript))
        kind = SpecialMethodKind::NSDictionarySubscriptGetter;

      // Import the type that this method will have.
      DeclName name = importedName.Imported;
      Optional<ForeignErrorConvention> errorConvention;
      bodyParams.push_back(nullptr);
      auto type = Impl.importMethodType(dc,
                                        decl,
                                        decl->getReturnType(),
                                        { decl->param_begin(),
                                          decl->param_size() },
                                        decl->isVariadic(),
                                        decl->hasAttr<clang::NoReturnAttr>(),
                                        isInSystemModule(dc),
                                        &bodyParams.back(),
                                        importedName,
                                        name,
                                        errorConvention,
                                        kind);
      if (!type)
        return nullptr;

      // Check whether we recursively imported this method
      if (!forceClassMethod &&
          dc == Impl.importDeclContextOf(decl, decl->getDeclContext())) {
        // FIXME: Should also be able to do this for forced class
        // methods.
        auto known = Impl.ImportedDecls.find({decl->getCanonicalDecl(),
                                              useSwift2Name});
        if (known != Impl.ImportedDecls.end())
          return known->second;
      }

      auto result = FuncDecl::create(
          Impl.SwiftContext, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
          /*FuncLoc=*/SourceLoc(), name, /*NameLoc=*/SourceLoc(),
          /*Throws=*/importedName.ErrorInfo.hasValue(), /*ThrowsLoc=*/SourceLoc(),
          /*AccessorKeywordLoc=*/SourceLoc(),
          /*GenericParams=*/nullptr, bodyParams,
          Type(), TypeLoc(), dc, decl);

      result->setAccessibility(getOverridableAccessibility(dc));

      auto resultTy = type->castTo<FunctionType>()->getResult();
      Type interfaceType;

      // If the method has a related result type that is representable
      // in Swift as DynamicSelf, do so.
      if (decl->hasRelatedResultType()) {
        result->setDynamicSelf(true);
        resultTy = result->getDynamicSelfInterface();
        assert(resultTy && "failed to get dynamic self");

        Type dynamicSelfTy = result->getDynamicSelfInterface();
        OptionalTypeKind nullability = OTK_ImplicitlyUnwrappedOptional;
        if (auto typeNullability = decl->getReturnType()->getNullability(
                                     Impl.getClangASTContext())) {
          // If the return type has nullability, use it.
          nullability = Impl.translateNullability(*typeNullability);
        }
        if (nullability != OTK_None && !errorConvention.hasValue()) {
          resultTy = OptionalType::get(nullability, resultTy);
          dynamicSelfTy = OptionalType::get(nullability, dynamicSelfTy);
        }

        // Update the method type with the new result type.
        auto methodTy = type->castTo<FunctionType>();
        type = FunctionType::get(methodTy->getInput(), resultTy, 
                                 methodTy->getExtInfo());
      }

      // Add the 'self' parameter to the function type.
      type = FunctionType::get(selfInterfaceType, type);

      std::tie(type, interfaceType)
        = getGenericMethodType(dc, type->castTo<AnyFunctionType>());

      result->setBodyResultType(resultTy);
      result->setType(type);
      result->setInterfaceType(interfaceType);
      result->setGenericSignature(dc->getGenericSignatureOfContext());

      // Optional methods in protocols.
      if (decl->getImplementationControl() == clang::ObjCMethodDecl::Optional &&
          isa<ProtocolDecl>(dc))
        result->getAttrs().add(new (Impl.SwiftContext)
                                      OptionalAttr(/*implicit*/false));

      // Mark class methods as static.
      if (decl->isClassMethod() || forceClassMethod)
        result->setStatic();
      if (forceClassMethod)
        result->setImplicit();

      // Mark this method @objc.
      addObjCAttribute(result, selector);

      // If this method overrides another method, mark it as such.
      recordObjCOverride(result);

      // Record the error convention.
      if (errorConvention) {
        result->setForeignErrorConvention(*errorConvention);
      }

      // Handle attributes.
      if (decl->hasAttr<clang::IBActionAttr>())
        result->getAttrs().add(
            new (Impl.SwiftContext) IBActionAttr(/*IsImplicit=*/false));

      // Check whether there's some special method to import.
      if (!forceClassMethod) {
        if (dc == Impl.importDeclContextOf(decl, decl->getDeclContext()) &&
            !Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}])
          Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}]
            = result;

        if (importedName.isSubscriptAccessor()) {
          // If this was a subscript accessor, try to create a
          // corresponding subscript declaration.
          (void)importSubscript(result, decl);
        } else if (shouldAlsoImportAsClassMethod(result)) {
          // If we should import this instance method also as a class
          // method, do so and mark the result as an alternate
          // declaration.
          if (auto imported = VisitObjCMethodDecl(decl, dc,
                                                  /*forceClassMethod=*/true))
            Impl.AlternateDecls[result] = cast<ValueDecl>(imported);
        } else if (auto factory = importFactoryMethodAsConstructor(
                                    result, decl, selector, dc)) {
          // We imported the factory method as an initializer, so
          // record it as an alternate declaration.
          if (*factory)
            Impl.AlternateDecls[result] = *factory;
        }

      }

      // If this is a Swift 2 stub, mark it as such.
      if (swift3Name)
        markAsSwift2Variant(result, *swift3Name);

      return result;
    }

  public:
    /// Record the function or initializer overridden by the given Swift method.
    void recordObjCOverride(AbstractFunctionDecl *decl) {
      // Figure out the class in which this method occurs.
      auto classTy = decl->getExtensionType()->getAs<ClassType>();
      if (!classTy)
        return;

      auto superTy = classTy->getSuperclass(nullptr);
      if (!superTy)
        return;

      // Dig out the Objective-C superclass.
      auto superDecl = superTy->getAnyNominal();
      SmallVector<ValueDecl *, 4> results;
      superDecl->lookupQualified(superTy, decl->getFullName(),
                                 NL_QualifiedDefault | NL_KnownNoDependency,
                                 Impl.getTypeResolver(),
                                 results);

      for (auto member : results) {
        if (member->getKind() != decl->getKind() ||
            member->isInstanceMember() != decl->isInstanceMember())
          continue;

        // Set function override.
        if (auto func = dyn_cast<FuncDecl>(decl)) {
          auto foundFunc = cast<FuncDecl>(member);

          // Require a selector match.
          if (func->getObjCSelector() != foundFunc->getObjCSelector())
            continue;

          func->setOverriddenDecl(foundFunc);
          return;
        }

        // Set constructor override.
        auto ctor = cast<ConstructorDecl>(decl);
        auto memberCtor = cast<ConstructorDecl>(member);

        // Require a selector match.
        if (ctor->getObjCSelector() != memberCtor->getObjCSelector())
          continue;

        ctor->setOverriddenDecl(memberCtor);

        // Propagate 'required' to subclass initializers.
        if (memberCtor->isRequired() &&
            !ctor->getAttrs().hasAttribute<RequiredAttr>()) {
          ctor->getAttrs().add(
            new (Impl.SwiftContext) RequiredAttr(/*implicit=*/true));
        }
      }
    }

    /// \brief Given an imported method, try to import it as a constructor.
    ///
    /// Objective-C methods in the 'init' family are imported as
    /// constructors in Swift, enabling object construction syntax, e.g.,
    ///
    /// \code
    /// // in objc: [[NSArray alloc] initWithCapacity:1024]
    /// NSArray(capacity: 1024)
    /// \endcode
    ConstructorDecl *importConstructor(const clang::ObjCMethodDecl *objcMethod,
                                       DeclContext *dc,
                                       bool implicit,
                                       Optional<CtorInitializerKind> kind,
                                       bool required) {
      // Only methods in the 'init' family can become constructors.
      assert(Impl.isInitMethod(objcMethod) && "Not a real init method");

      // Check whether we've already created the constructor.
      auto known = Impl.Constructors.find(std::make_tuple(objcMethod, dc,
                                                          useSwift2Name));
      if (known != Impl.Constructors.end())
        return known->second;
      
      // Check whether there is already a method with this selector.
      auto selector = Impl.importSelector(objcMethod->getSelector());
      if (!useSwift2Name &&
          methodAlreadyImported(selector, /*isInstance=*/true, dc))
        return nullptr;

      // Map the name and complete the import.
      ArrayRef<const clang::ParmVarDecl *> params{
        objcMethod->param_begin(),
        objcMethod->param_end()
      };

      bool variadic = objcMethod->isVariadic();
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(objcMethod, swift3Name);
      if (!importedName) return nullptr;

      // If we dropped the variadic, handle it now.
      if (importedName.DroppedVariadic) {
        selector = ObjCSelector(Impl.SwiftContext, selector.getNumArgs()-1,
                                selector.getSelectorPieces().drop_back());
        params = params.drop_back(1);
        variadic = false;
      }

      bool redundant;
      auto result = importConstructor(objcMethod, dc, implicit, kind, required,
                                      selector, importedName, params,
                                      variadic, redundant);

      // If this is a Swift 2 stub, mark it as such.
      if (result && swift3Name)
        markAsSwift2Variant(result, *swift3Name);

      return result;
    }

    /// Returns the latest "introduced" version on the current platform for
    /// \p D.
    clang::VersionTuple findLatestIntroduction(const clang::Decl *D) {
      clang::VersionTuple result;

      for (auto *attr : D->specific_attrs<clang::AvailabilityAttr>()) {
        if (attr->getPlatform()->getName() == "swift") {
          clang::VersionTuple maxVersion{~0U, ~0U, ~0U};
          return maxVersion;
        }

        // Does this availability attribute map to the platform we are
        // currently targeting?
        if (!Impl.PlatformAvailabilityFilter ||
            !Impl.PlatformAvailabilityFilter(attr->getPlatform()->getName()))
          continue;

        // Take advantage of the empty version being 0.0.0.0.
        result = std::max(result, attr->getIntroduced());
      }

      return result;
    }

    /// Returns true if importing \p objcMethod will produce a "better"
    /// initializer than \p existingCtor.
    bool
    existingConstructorIsWorse(const ConstructorDecl *existingCtor,
                               const clang::ObjCMethodDecl *objcMethod,
                               CtorInitializerKind kind) {
      CtorInitializerKind existingKind = existingCtor->getInitKind();

      // If one constructor is unavailable in Swift and the other is
      // not, keep the available one.
      bool existingIsUnavailable =
        existingCtor->getAttrs().isUnavailable(Impl.SwiftContext);
      bool newIsUnavailable = Impl.isUnavailableInSwift(objcMethod);
      if (existingIsUnavailable != newIsUnavailable)
        return existingIsUnavailable;

      // If the new kind is the same as the existing kind, stick with
      // the existing constructor.
      if (existingKind == kind)
        return false;

      // Check for cases that are obviously better or obviously worse.
      if (kind == CtorInitializerKind::Designated ||
          existingKind == CtorInitializerKind::Factory)
        return true;

      if (kind == CtorInitializerKind::Factory ||
          existingKind == CtorInitializerKind::Designated)
        return false;

      assert(kind == CtorInitializerKind::Convenience ||
             kind == CtorInitializerKind::ConvenienceFactory);
      assert(existingKind == CtorInitializerKind::Convenience ||
             existingKind == CtorInitializerKind::ConvenienceFactory);

      // Between different kinds of convenience initializers, keep the one that
      // was introduced first.
      // FIXME: But if one of them is now deprecated, should we prefer the
      // other?
      clang::VersionTuple introduced = findLatestIntroduction(objcMethod);
      AvailabilityContext existingAvailability =
          AvailabilityInference::availableRange(existingCtor,
                                                Impl.SwiftContext);
      assert(!existingAvailability.isKnownUnreachable());

      if (existingAvailability.isAlwaysAvailable()) {
        if (!introduced.empty())
          return false;
      } else {
        VersionRange existingIntroduced = existingAvailability.getOSVersion();
        if (introduced != existingIntroduced.getLowerEndpoint()) {
          return introduced < existingIntroduced.getLowerEndpoint();
        }
      }

      // The "introduced" versions are the same. Prefer Convenience over
      // ConvenienceFactory, but otherwise prefer leaving things as they are.
      if (kind == CtorInitializerKind::Convenience &&
          existingKind == CtorInitializerKind::ConvenienceFactory)
        return true;

      return false;
    }

    /// \brief Given an imported method, try to import it as a constructor.
    ///
    /// Objective-C methods in the 'init' family are imported as
    /// constructors in Swift, enabling object construction syntax, e.g.,
    ///
    /// \code
    /// // in objc: [[NSArray alloc] initWithCapacity:1024]
    /// NSArray(capacity: 1024)
    /// \endcode
    ///
    /// This variant of the function is responsible for actually binding the
    /// constructor declaration appropriately.
    ConstructorDecl *importConstructor(const clang::ObjCMethodDecl *objcMethod,
                                       DeclContext *dc,
                                       bool implicit,
                                       Optional<CtorInitializerKind> kindIn,
                                       bool required,
                                       ObjCSelector selector,
                                       ImportedName importedName,
                                       ArrayRef<const clang::ParmVarDecl*> args,
                                       bool variadic,
                                       bool &redundant) {
      redundant = false;

      // Figure out the type of the container.
      auto ownerNominal = dc->getAsNominalTypeOrNominalTypeExtensionContext();
      assert(ownerNominal && "Method in non-type context?");

      // Find the interface, if we can.
      const clang::ObjCInterfaceDecl *interface = nullptr;
      if (auto classDecl = dyn_cast<ClassDecl>(ownerNominal)) {
        interface = dyn_cast_or_null<clang::ObjCInterfaceDecl>(
                      classDecl->getClangDecl());
      }

      // If we weren't told what kind of initializer this should be,
      // figure it out now.
      CtorInitializerKind kind;

      if (kindIn) {
        kind = *kindIn;

        // If we know this is a designated initializer, mark it as such.
        if (interface && Impl.hasDesignatedInitializers(interface) &&
            Impl.isDesignatedInitializer(interface, objcMethod))
          kind = CtorInitializerKind::Designated;
      } else {
        // If the owning Objective-C class has designated initializers and this
        // is not one of them, treat it as a convenience initializer.
        if (interface && Impl.hasDesignatedInitializers(interface) &&
            !Impl.isDesignatedInitializer(interface, objcMethod)) {
          kind = CtorInitializerKind::Convenience;
        } else {
          kind = CtorInitializerKind::Designated;
        }
      }

      // Add the implicit 'self' parameter patterns.
      SmallVector<ParameterList*, 4> bodyParams;
      auto selfMetaVar = ParamDecl::createSelf(SourceLoc(), dc, /*static*/true);
      auto selfTy = dc->getSelfInterfaceType();
      auto selfMetaTy = MetatypeType::get(selfTy);
      bodyParams.push_back(ParameterList::createWithoutLoc(selfMetaVar));

      // Import the type that this method will have.
      Optional<ForeignErrorConvention> errorConvention;
      DeclName name = importedName.Imported;
      bodyParams.push_back(nullptr);
      auto type = Impl.importMethodType(dc,
                                        objcMethod,
                                        objcMethod->getReturnType(),
                                        args,
                                        variadic,
                                        objcMethod->hasAttr<clang::NoReturnAttr>(),
                                        isInSystemModule(dc),
                                        &bodyParams.back(),
                                        importedName,
                                        name,
                                        errorConvention,
                                        SpecialMethodKind::Constructor);
      if (!type)
        return nullptr;

      // Determine the failability of this initializer.
      auto oldFnType = type->castTo<AnyFunctionType>();
      OptionalTypeKind failability;
      (void)oldFnType->getResult()->getAnyOptionalObjectType(failability);

      // Rebuild the function type with the appropriate result type;
      Type resultTy = selfTy;
      if (failability)
        resultTy = OptionalType::get(failability, resultTy);

      type = FunctionType::get(oldFnType->getInput(), resultTy,
                               oldFnType->getExtInfo());

      // Add the 'self' parameter to the function types.
      Type allocType = FunctionType::get(selfMetaTy, type);
      Type initType = FunctionType::get(selfTy, type);

      // Look for other imported constructors that occur in this context with
      // the same name.
      Type allocParamType = allocType->castTo<AnyFunctionType>()->getResult()
                              ->castTo<AnyFunctionType>()->getInput();
      for (auto other : ownerNominal->lookupDirect(name)) {
        auto ctor = dyn_cast<ConstructorDecl>(other);
        if (!ctor || ctor->isInvalid() ||
            ctor->getAttrs().isUnavailable(Impl.SwiftContext) ||
            !ctor->getClangDecl())
          continue;

        // Resolve the type of the constructor.
        if (!ctor->hasType())
          Impl.getTypeResolver()->resolveDeclSignature(ctor);

        // If the types don't match, this is a different constructor with
        // the same selector. This can happen when an overlay overloads an
        // existing selector with a Swift-only signature.
        Type ctorParamType = ctor->getType()->castTo<AnyFunctionType>()
                               ->getResult()->castTo<AnyFunctionType>()
                               ->getInput();
        if (!ctorParamType->isEqual(allocParamType)) {
          continue;
        }

        // If the existing constructor has a less-desirable kind, mark
        // the existing constructor unavailable.
        if (existingConstructorIsWorse(ctor, objcMethod, kind)) {
          // Show exactly where this constructor came from.
          llvm::SmallString<32> errorStr;
          errorStr += "superseded by import of ";
          if (objcMethod->isClassMethod())
            errorStr += "+[";
          else
            errorStr += "-[";

          auto objcDC = objcMethod->getDeclContext();
          if (auto objcClass = dyn_cast<clang::ObjCInterfaceDecl>(objcDC)) {
            errorStr += objcClass->getName();
            errorStr += ' ';
          } else if (auto objcCat = dyn_cast<clang::ObjCCategoryDecl>(objcDC)) {
            errorStr += objcCat->getClassInterface()->getName();
            auto catName = objcCat->getName();
            if (!catName.empty()) {
              errorStr += '(';
              errorStr += catName;
              errorStr += ')';
            }
            errorStr += ' ';
          } else if (auto objcProto=dyn_cast<clang::ObjCProtocolDecl>(objcDC)) {
            errorStr += objcProto->getName();
            errorStr += ' ';
          }

          errorStr += objcMethod->getSelector().getAsString();
          errorStr += ']';

          auto attr
            = AvailableAttr::createUnconditional(
                Impl.SwiftContext,
                Impl.SwiftContext.AllocateCopy(errorStr.str()));
          ctor->getAttrs().add(attr);
          continue;
        }

        // Otherwise, we shouldn't create a new constructor, because
        // it will be no better than the existing one.
        redundant = true;
        return nullptr;
      }

      // Check whether we've already created the constructor.
      auto known = Impl.Constructors.find(std::make_tuple(objcMethod, dc,
                                                          useSwift2Name));
      if (known != Impl.Constructors.end())
        return known->second;

      auto *selfVar = ParamDecl::createSelf(SourceLoc(), dc);

      // Create the actual constructor.
      auto result = Impl.createDeclWithClangNode<ConstructorDecl>(objcMethod,
                      Accessibility::Public, name, /*NameLoc=*/SourceLoc(),
                      failability, /*FailabilityLoc=*/SourceLoc(),
                      /*Throws=*/importedName.ErrorInfo.hasValue(),
                      /*ThrowsLoc=*/SourceLoc(),
                      selfVar, bodyParams.back(),
                      /*GenericParams=*/nullptr,
                      dc);

      // Make the constructor declaration immediately visible in its
      // class or protocol type.
      ownerNominal->makeMemberVisible(result);

      addObjCAttribute(result, selector);

      // Calculate the function type of the result.
      Type interfaceAllocType;
      Type interfaceInitType;
      std::tie(allocType, interfaceAllocType)
        = getGenericMethodType(dc, allocType->castTo<AnyFunctionType>());
      std::tie(initType, interfaceInitType)
        = getGenericMethodType(dc, initType->castTo<AnyFunctionType>());

      result->setInitializerInterfaceType(interfaceInitType);
      result->setInterfaceType(interfaceAllocType);
      result->setGenericSignature(dc->getGenericSignatureOfContext());

      result->setType(allocType);

      if (implicit)
        result->setImplicit();

      // Set the kind of initializer.
      result->setInitKind(kind);

      // Consult API notes to determine whether this initializer is required.
      if (!required && Impl.isRequiredInitializer(objcMethod))
        required = true;

      // Check whether this initializer satisfies a requirement in a protocol.
      if (!required && !isa<ProtocolDecl>(dc) &&
          objcMethod->isInstanceMethod()) {
        auto objcParent = cast<clang::ObjCContainerDecl>(
                            objcMethod->getDeclContext());

        if (isa<clang::ObjCProtocolDecl>(objcParent)) {
          // An initializer declared in a protocol is required.
          required = true;
        } else {
          // If the class in which this initializer was declared conforms to a
          // protocol that requires this initializer, then this initializer is
          // required.
          SmallPtrSet<clang::ObjCProtocolDecl *, 8> objcProtocols;
          objcParent->getASTContext().CollectInheritedProtocols(objcParent,
                                                                objcProtocols);
          for (auto objcProto : objcProtocols) {
            for (auto decl : objcProto->lookup(objcMethod->getSelector())) {
              if (cast<clang::ObjCMethodDecl>(decl)->isInstanceMethod()) {
                required = true;
                break;
              }
            }

            if (required)
              break;
          }
        }
      }

      // If this initializer is required, add the appropriate attribute.
      if (required) {
        result->getAttrs().add(
          new (Impl.SwiftContext) RequiredAttr(/*implicit=*/true));
      }

      // Record the error convention.
      if (errorConvention) {
        result->setForeignErrorConvention(*errorConvention);
      }

      // Record the constructor for future re-use.
      Impl.Constructors[std::make_tuple(objcMethod, dc, useSwift2Name)] =
        result;

      // If this constructor overrides another constructor, mark it as such.
      recordObjCOverride(result);

      // Inform the context that we have external definitions.
      Impl.registerExternalDecl(result);

      return result;
    }

    /// \brief Retrieve the single variable described in the given pattern.
    ///
    /// This routine assumes that the pattern is something very simple
    /// like (x : type) or (x).
    VarDecl *getSingleVar(Pattern *pattern) {
      pattern = pattern->getSemanticsProvidingPattern();
      if (auto tuple = dyn_cast<TuplePattern>(pattern)) {
        pattern = tuple->getElement(0).getPattern()
                    ->getSemanticsProvidingPattern();
      }

      return cast<NamedPattern>(pattern)->getDecl();
    }

    std::pair<Type, Type> getGenericMethodType(DeclContext *dc,
                                               AnyFunctionType *fnType) {
      assert(!fnType->hasArchetype());

      auto *sig = dc->getGenericSignatureOfContext();
      if (!sig)
        return { fnType, fnType };

      Type inputType = ArchetypeBuilder::mapTypeIntoContext(
          dc, fnType->getInput());
      Type resultType = ArchetypeBuilder::mapTypeIntoContext(
          dc, fnType->getResult());
      Type type = PolymorphicFunctionType::get(inputType, resultType,
                                               dc->getGenericParamsOfContext());

      Type interfaceType = GenericFunctionType::get(
          sig,
          fnType->getInput(), fnType->getResult(),
          AnyFunctionType::ExtInfo());

      return { type, interfaceType };
    }


    /// Build a declaration for an Objective-C subscript getter.
    FuncDecl *buildSubscriptGetterDecl(const FuncDecl *getter, Type elementTy,
                                       DeclContext *dc, ParamDecl *index) {
      auto &C = Impl.SwiftContext;
      auto loc = getter->getLoc();

      // self & index.
      ParameterList *getterArgs[] = {
        ParameterList::createSelf(SourceLoc(), dc),
        ParameterList::create(C, index)
      };

      // Form the type of the getter.
      auto getterType =
          ParameterList::getFullInterfaceType(elementTy, getterArgs, dc);

      Type interfaceType;
      std::tie(getterType, interfaceType)
        = getGenericMethodType(dc, getterType->castTo<AnyFunctionType>());

      // Create the getter thunk.
      FuncDecl *thunk = FuncDecl::create(
          C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
          /*FuncLoc=*/loc, /*Name=*/Identifier(), /*NameLoc=*/SourceLoc(),
          /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
          /*AccessorKeywordLoc=*/SourceLoc(),
          /*GenericParams=*/nullptr, getterArgs,
          getterType, TypeLoc::withoutLoc(elementTy), dc,
          getter->getClangNode());
      thunk->setBodyResultType(elementTy);
      thunk->setInterfaceType(interfaceType);
      thunk->setGenericSignature(dc->getGenericSignatureOfContext());
      thunk->setAccessibility(getOverridableAccessibility(dc));

      auto objcAttr = getter->getAttrs().getAttribute<ObjCAttr>();
      assert(objcAttr);
      thunk->getAttrs().add(objcAttr->clone(C));
      // FIXME: Should we record thunks?

      return thunk;
    }

      /// Build a declaration for an Objective-C subscript setter.
    FuncDecl *buildSubscriptSetterDecl(const FuncDecl *setter, Type elementInterfaceTy,
                                       DeclContext *dc, ParamDecl *index) {
      auto &C = Impl.SwiftContext;
      auto loc = setter->getLoc();

      // Objective-C subscript setters are imported with a function type
      // such as:
      //
      //   (self) -> (value, index) -> ()
      //
      // Build a setter thunk with the latter signature that maps to the
      // former.
      auto valueIndex = setter->getParameterList(1);

      // 'self'
      auto selfDecl = ParamDecl::createSelf(SourceLoc(), dc);
      auto elementTy = ArchetypeBuilder::mapTypeIntoContext(
          dc, elementInterfaceTy);

      auto paramVarDecl = new (C) ParamDecl(/*isLet=*/false, SourceLoc(),
                                            SourceLoc(), Identifier(),loc,
                                            valueIndex->get(0)->getName(),
                                            elementTy, dc);
      paramVarDecl->setInterfaceType(elementInterfaceTy);

      auto valueIndicesPL = ParameterList::create(C, {
        paramVarDecl,
        index
      });
      
      // Form the argument lists.
      ParameterList *setterArgs[] = {
        ParameterList::createWithoutLoc(selfDecl),
        valueIndicesPL
      };
      
      // Form the type of the setter.
      Type setterType =
          ParameterList::getFullInterfaceType(TupleType::getEmpty(C),
                                              setterArgs,
                                              dc);

      Type interfaceType;
      std::tie(setterType, interfaceType)
        = getGenericMethodType(dc, setterType->castTo<AnyFunctionType>());

      // Create the setter thunk.
      FuncDecl *thunk = FuncDecl::create(
          C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
          /*FuncLoc=*/setter->getLoc(),
          /*Name=*/Identifier(), /*NameLoc=*/SourceLoc(),
          /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
          /*AccessorKeywordLoc=*/SourceLoc(),
          /*GenericParams=*/nullptr,
          setterArgs, setterType,
          TypeLoc::withoutLoc(TupleType::getEmpty(C)), dc,
          setter->getClangNode());
      thunk->setBodyResultType(TupleType::getEmpty(C));
      thunk->setInterfaceType(interfaceType);
      thunk->setGenericSignature(dc->getGenericSignatureOfContext());
      thunk->setAccessibility(getOverridableAccessibility(dc));

      auto objcAttr = setter->getAttrs().getAttribute<ObjCAttr>();
      assert(objcAttr);
      thunk->getAttrs().add(objcAttr->clone(C));

      return thunk;
    }
    
    /// Retrieve the element interface type and key param decl of a subscript
    /// setter.
    std::pair<Type, ParamDecl *>
    decomposeSubscriptSetter(FuncDecl *setter) {
      auto *PL = setter->getParameterList(1);
      if (PL->size() != 2)
        return { nullptr, nullptr };

      // Setter type is (self) -> (elem_type, key_type) -> ()
      Type elementType =
        setter->getInterfaceType()->castTo<AnyFunctionType>()->getResult()
        ->castTo<AnyFunctionType>()->getInput()
        ->castTo<TupleType>()->getElementType(0);
      ParamDecl *keyDecl = PL->get(1);
      
      return { elementType, keyDecl };
    }

    /// Rectify the (possibly different) types determined by the
    /// getter and setter for a subscript.
    ///
    /// \param canUpdateType whether the type of subscript can be
    /// changed from the getter type to something compatible with both
    /// the getter and the setter.
    ///
    /// \returns the type to be used for the subscript, or a null type
    /// if the types cannot be rectified.
    Type rectifySubscriptTypes(Type getterType, Type setterType,
                               bool canUpdateType) {
      // If the caller couldn't provide a setter type, there is
      // nothing to rectify.
      if (!setterType) return nullptr;

      // Trivial case: same type in both cases.
      if (getterType->isEqual(setterType)) return getterType;

      // The getter/setter types are different. If we cannot update
      // the type, we have to fail.
      if (!canUpdateType) return nullptr;

      // Unwrap one level of optionality from each.
      if (Type getterObjectType = getterType->getAnyOptionalObjectType())
        getterType = getterObjectType;
      if (Type setterObjectType = setterType->getAnyOptionalObjectType())
        setterType = setterObjectType;

      // If they are still different, fail.
      // FIXME: We could produce the greatest common supertype of the
      // two types.
      if (!getterType->isEqual(setterType)) return nullptr;

      // Create an implicitly-unwrapped optional of the object type,
      // which subsumes both behaviors.
      return ImplicitlyUnwrappedOptionalType::get(setterType);
    }

    void recordObjCOverride(SubscriptDecl *subscript) {
      // Figure out the class in which this subscript occurs.
      auto classTy =
        subscript->getDeclContext()->getAsClassOrClassExtensionContext();
      if (!classTy)
        return;

      auto superTy = classTy->getSuperclass();
      if (!superTy)
        return;

      // Determine whether this subscript operation overrides another subscript
      // operation.
      SmallVector<ValueDecl *, 2> lookup;
      subscript->getModuleContext()
        ->lookupQualified(superTy, subscript->getFullName(),
                          NL_QualifiedDefault | NL_KnownNoDependency,
                          Impl.getTypeResolver(), lookup);
      Type unlabeledIndices;
      for (auto result : lookup) {
        auto parentSub = dyn_cast<SubscriptDecl>(result);
        if (!parentSub)
          continue;

        // Compute the type of indices for our own subscript operation, lazily.
        if (!unlabeledIndices) {
          unlabeledIndices = subscript->getIndices()->getType(Impl.SwiftContext)
                               ->getUnlabeledType(Impl.SwiftContext);
        }

        // Compute the type of indices for the subscript we found.
        auto parentUnlabeledIndices =
          parentSub->getIndices()->getType(Impl.SwiftContext)
               ->getUnlabeledType(Impl.SwiftContext);
        if (!unlabeledIndices->isEqual(parentUnlabeledIndices))
          continue;

        // The index types match. This is an override, so mark it as such.
        subscript->setOverriddenDecl(parentSub);
        auto getterThunk = subscript->getGetter();
        getterThunk->setOverriddenDecl(parentSub->getGetter());
        if (auto parentSetter = parentSub->getSetter()) {
          if (auto setterThunk = subscript->getSetter())
            setterThunk->setOverriddenDecl(parentSetter);
        }

        // FIXME: Eventually, deal with multiple overrides.
        break;
      }
    }

    /// \brief Given either the getter or setter for a subscript operation,
    /// create the Swift subscript declaration.
    SubscriptDecl *importSubscript(Decl *decl,
                                   const clang::ObjCMethodDecl *objcMethod) {
      assert(objcMethod->isInstanceMethod() && "Caller must filter");

      // If the method we're attempting to import has the
      // swift_private attribute, don't import as a subscript.
      if (objcMethod->hasAttr<clang::SwiftPrivateAttr>())
        return nullptr;

      // Figure out where to look for the counterpart.
      const clang::ObjCInterfaceDecl *interface = nullptr;
      const clang::ObjCProtocolDecl *protocol =
          dyn_cast<clang::ObjCProtocolDecl>(objcMethod->getDeclContext());
      if (!protocol)
        interface = objcMethod->getClassInterface();
      auto lookupInstanceMethod = [&](clang::Selector Sel) ->
          const clang::ObjCMethodDecl * {
        if (interface)
          return interface->lookupInstanceMethod(Sel);

        return protocol->lookupInstanceMethod(Sel);
      };

      auto findCounterpart = [&](clang::Selector sel) -> FuncDecl * {
        // If the declaration we're starting from is in a class, first
        // look for a class member with the appropriate selector.
        if (auto classDecl
              = decl->getDeclContext()->getAsClassOrClassExtensionContext()) {
          auto swiftSel = Impl.importSelector(sel);
          for (auto found : classDecl->lookupDirect(swiftSel, true)) {
            if (auto foundFunc = dyn_cast<FuncDecl>(found))
              return foundFunc;
          }
        }

        // Find based on selector within the current type.
        auto counterpart = lookupInstanceMethod(sel);
        if (!counterpart) return nullptr;

        return cast_or_null<FuncDecl>(Impl.importDecl(counterpart, false));
      };

      // Determine the selector of the counterpart.
      FuncDecl *getter = nullptr, *setter = nullptr;
      clang::Selector counterpartSelector;
      if (objcMethod->getSelector() == Impl.objectAtIndexedSubscript) {
        getter = cast<FuncDecl>(decl);
        counterpartSelector = Impl.setObjectAtIndexedSubscript;
      } else if (objcMethod->getSelector() == Impl.setObjectAtIndexedSubscript){
        setter = cast<FuncDecl>(decl);
        counterpartSelector = Impl.objectAtIndexedSubscript;
      } else if (objcMethod->getSelector() == Impl.objectForKeyedSubscript) {
        getter = cast<FuncDecl>(decl);
        counterpartSelector = Impl.setObjectForKeyedSubscript;
      } else if (objcMethod->getSelector() == Impl.setObjectForKeyedSubscript) {
        setter = cast<FuncDecl>(decl);
        counterpartSelector = Impl.objectForKeyedSubscript;
      } else {
        llvm_unreachable("Unknown getter/setter selector");
      }

      // Find the counterpart.
      bool optionalMethods = (objcMethod->getImplementationControl() ==
                              clang::ObjCMethodDecl::Optional);

      if (auto *counterpart = findCounterpart(counterpartSelector)) {
        // If the counterpart to the method we're attempting to import has the
        // swift_private attribute, don't import as a subscript.
        if (auto importedFrom = counterpart->getClangDecl()) {
          if (importedFrom->hasAttr<clang::SwiftPrivateAttr>())
            return nullptr;

          auto counterpartMethod
            = dyn_cast<clang::ObjCMethodDecl>(importedFrom);
          if (optionalMethods)
            optionalMethods = (counterpartMethod->getImplementationControl() ==
                               clang::ObjCMethodDecl::Optional);
        }

        assert(!counterpart || !counterpart->isStatic());

        if (getter)
          setter = counterpart;
        else
          getter = counterpart;
      }

      // Swift doesn't have write-only subscripting.
      if (!getter)
        return nullptr;

      // Check whether we've already created a subscript operation for
      // this getter/setter pair.
      if (auto subscript = Impl.Subscripts[{getter, setter}]) {
        return subscript->getDeclContext() == decl->getDeclContext()
                 ? subscript
                 : nullptr;
      }

      // Find the getter indices and make sure they match.
      ParamDecl *getterIndex;
      {
        auto params = getter->getParameterList(1);
        if (params->size() != 1)
          return nullptr;
        getterIndex = params->get(0);
      }

      // Compute the element type based on the getter, looking through
      // the implicit 'self' parameter and the normal function
      // parameters.
      auto elementTy
        = getter->getInterfaceType()->castTo<AnyFunctionType>()->getResult()
            ->castTo<AnyFunctionType>()->getResult();
      auto elementContextTy
        = getter->getType()->castTo<AnyFunctionType>()->getResult()
          ->castTo<AnyFunctionType>()->getResult();

      // Local function to mark the setter unavailable.
      auto makeSetterUnavailable = [&] {
        if (setter && !setter->getAttrs().isUnavailable(Impl.SwiftContext))
          Impl.markUnavailable(setter, "use subscripting");
      };

      // If we have a setter, rectify it with the getter.
      ParamDecl *setterIndex;
      bool getterAndSetterInSameType = false;
      if (setter) {
        // Whether there is an existing read-only subscript for which
        // we have now found a setter.
        SubscriptDecl *existingSubscript = Impl.Subscripts[{getter, nullptr}];

        // Are the getter and the setter in the same type.
        getterAndSetterInSameType =
          (getter->getDeclContext()
             ->getAsNominalTypeOrNominalTypeExtensionContext()
           == setter->getDeclContext()
               ->getAsNominalTypeOrNominalTypeExtensionContext());
        // TODO: Possible that getter and setter are different instantiations
        // of the same objc generic type?

        // Whether we can update the types involved in the subscript
        // operation.
        bool canUpdateSubscriptType
          = !existingSubscript && getterAndSetterInSameType;

        // Determine the setter's element type and indices.
        Type setterElementTy;
        std::tie(setterElementTy, setterIndex) =
          decomposeSubscriptSetter(setter);

        // Rectify the setter element type with the getter's element type.
        Type newElementTy = rectifySubscriptTypes(elementTy, setterElementTy,
                                                  canUpdateSubscriptType);
        if (!newElementTy)
          return decl == getter ? existingSubscript : nullptr;

        // Update the element type.
        elementTy = newElementTy;

        // Make sure that the index types are equivalent.
        // FIXME: Rectify these the same way we do for element types.
        if (!setterIndex->getType()->isEqual(getterIndex->getType())) {
          // If there is an existing subscript operation, we're done.
          if (existingSubscript)
            return decl == getter ? existingSubscript : nullptr;

          // Otherwise, just forget we had a setter.
          // FIXME: This feels very, very wrong.
          setter = nullptr;
          setterIndex = nullptr;
        }

        // If there is an existing subscript within this context, we
        // cannot create a new subscript. Update it if possible.
        if (setter && existingSubscript && getterAndSetterInSameType) {
          // Can we update the subscript by adding the setter?
          if (existingSubscript->hasClangNode() &&
              !existingSubscript->isSettable()) {
            // Create the setter thunk.
            auto setterThunk = buildSubscriptSetterDecl(
                                 setter, elementTy, setter->getDeclContext(),
                                 setterIndex);

            // Set the computed setter.
            existingSubscript->setComputedSetter(setterThunk);

            // Mark the setter as unavailable; one should use
            // subscripting when it is present.
            makeSetterUnavailable();
          }

          return decl == getter ? existingSubscript : nullptr;
        }
      }

      // The context into which the subscript should go.
      bool associateWithSetter = setter && !getterAndSetterInSameType;
      DeclContext *dc = associateWithSetter ? setter->getDeclContext()
                                            : getter->getDeclContext();

      // Build the thunks.
      FuncDecl *getterThunk = buildSubscriptGetterDecl(getter, elementTy, dc,
                                                       getterIndex);

      FuncDecl *setterThunk = nullptr;
      if (setter)
        setterThunk = buildSubscriptSetterDecl(setter, elementTy, dc,
                                               setterIndex);

      // Build the subscript declaration.
      auto &C = Impl.SwiftContext;
      auto bodyParams = getterThunk->getParameterList(1)->clone(C);
      DeclName name(C, C.Id_subscript, { Identifier() });
      auto subscript
        = Impl.createDeclWithClangNode<SubscriptDecl>(getter->getClangNode(),
                                      getOverridableAccessibility(dc),
                                      name, decl->getLoc(), bodyParams,
                                      decl->getLoc(),
                                      TypeLoc::withoutLoc(elementContextTy),dc);

      /// Record the subscript as an alternative declaration.
      Impl.AlternateDecls[associateWithSetter ? setter : getter] = subscript;

      subscript->makeComputed(SourceLoc(), getterThunk, setterThunk, nullptr,
                              SourceLoc());
      auto indicesType = bodyParams->getType(C);

      // TODO: no good when generics are around
      auto fnType = FunctionType::get(indicesType, elementTy);
      subscript->setType(fnType);
      subscript->setInterfaceType(fnType);

      addObjCAttribute(subscript, None);

      // Optional subscripts in protocols.
      if (optionalMethods && isa<ProtocolDecl>(dc))
        subscript->getAttrs().add(new (Impl.SwiftContext) OptionalAttr(true));

      // Note that we've created this subscript.
      Impl.Subscripts[{getter, setter}] = subscript;
      if (setter && !Impl.Subscripts[{getter, nullptr}])
        Impl.Subscripts[{getter, nullptr}] = subscript;

      // Make the getter/setter methods unavailable.
      if (!getter->getAttrs().isUnavailable(Impl.SwiftContext))
        Impl.markUnavailable(getter, "use subscripting");
      makeSetterUnavailable();

      // Wire up overrides.
      recordObjCOverride(subscript);

      return subscript;
    }

    /// Import the accessor and its attributes.
    FuncDecl *importAccessor(clang::ObjCMethodDecl *clangAccessor,
                             DeclContext *dc) {
      SwiftDeclConverter converter(Impl, /*useSwift2Name=*/false);
      auto *accessor =
          cast_or_null<FuncDecl>(
            converter.VisitObjCMethodDecl(clangAccessor, dc));
      if (!accessor) {
        return nullptr;
      }

      Impl.importAttributes(clangAccessor, accessor);

      return accessor;
    }

  public:

    /// Recursively add the given protocol and its inherited protocols to the
    /// given vector, guarded by the known set of protocols.
    void addProtocols(ProtocolDecl *protocol,
                      SmallVectorImpl<ProtocolDecl *> &protocols,
                      llvm::SmallPtrSet<ProtocolDecl *, 4> &known) {
      if (!known.insert(protocol).second)
        return;

      protocols.push_back(protocol);
      for (auto inherited : protocol->getInheritedProtocols(
                              Impl.getTypeResolver()))
        addProtocols(inherited, protocols, known);
    }

    // Import the given Objective-C protocol list, along with any
    // implicitly-provided protocols, and attach them to the given
    // declaration.
    void importObjCProtocols(Decl *decl,
                             const clang::ObjCProtocolList &clangProtocols,
                             SmallVectorImpl<TypeLoc> &inheritedTypes) {
      SmallVector<ProtocolDecl *, 4> protocols;
      llvm::SmallPtrSet<ProtocolDecl *, 4> knownProtocols;
      if (auto nominal = dyn_cast<NominalTypeDecl>(decl)) {
        nominal->getImplicitProtocols(protocols);
        knownProtocols.insert(protocols.begin(), protocols.end());
      }

      for (auto cp = clangProtocols.begin(), cpEnd = clangProtocols.end();
           cp != cpEnd; ++cp) {
        if (auto proto = cast_or_null<ProtocolDecl>(Impl.importDecl(*cp,
                                                                    false))) {
          addProtocols(proto, protocols, knownProtocols);
          inheritedTypes.push_back(
            TypeLoc::withoutLoc(proto->getDeclaredType()));
        }
      }

      addObjCProtocolConformances(decl, protocols);
    }

    /// Add conformances to the given Objective-C protocols to the
    /// given declaration.
    void addObjCProtocolConformances(Decl *decl,
                                     ArrayRef<ProtocolDecl*> protocols) {
      // Set the inherited protocols of a protocol.
      if (auto proto = dyn_cast<ProtocolDecl>(decl)) {
        // Copy the list of protocols.
        MutableArrayRef<ProtocolDecl *> allProtocols
          = Impl.SwiftContext.AllocateCopy(protocols);
        proto->setInheritedProtocols(allProtocols);

        return;
      }

      Impl.recordImportedProtocols(decl, protocols);

      // Synthesize trivial conformances for each of the protocols.
      SmallVector<ProtocolConformance *, 4> conformances;

      auto dc = decl->getInnermostDeclContext();
      auto &ctx = Impl.SwiftContext;
      for (unsigned i = 0, n = protocols.size(); i != n; ++i) {
        // FIXME: Build a superclass conformance if the superclass
        // conforms.
        auto conformance
          = ctx.getConformance(dc->getDeclaredTypeInContext(),
                               protocols[i], SourceLoc(),
                               dc,
                               ProtocolConformanceState::Incomplete);
        Impl.scheduleFinishProtocolConformance(conformance);
        conformances.push_back(conformance);
      }

      // Set the conformances.
      // FIXME: This could be lazier.
      unsigned id = Impl.allocateDelayedConformance(std::move(conformances));
      if (auto nominal = dyn_cast<NominalTypeDecl>(decl)) {
        nominal->setConformanceLoader(&Impl, id);
      } else {
        auto ext = cast<ExtensionDecl>(decl);
        ext->setConformanceLoader(&Impl, id);
      }
    }

    // Returns None on error. Returns nullptr if there is no type param list to
    // import or we suppress its import, as in the case of NSArray, NSSet, and
    // NSDictionary.
    Optional<GenericParamList *> importObjCGenericParams(
       const clang::ObjCInterfaceDecl *decl, DeclContext *dc)
    {
      auto typeParamList = decl->getTypeParamList();
      if (!typeParamList) {
        return nullptr;
      }
      if (Impl.shouldSuppressGenericParamsImport(decl)) {
        return nullptr;
      }
      assert(typeParamList->size() > 0);
      SmallVector<GenericTypeParamDecl *, 4> genericParams;
      for (auto *objcGenericParam : *typeParamList) {
        auto genericParamDecl =
          Impl.createDeclWithClangNode<GenericTypeParamDecl>(objcGenericParam,
              Accessibility::Public,
              dc, Impl.SwiftContext.getIdentifier(objcGenericParam->getName()),
              Impl.importSourceLoc(objcGenericParam->getLocation()),
              /*depth*/0, /*index*/genericParams.size());
        // NOTE: depth is always 0 for ObjC generic type arguments, since only
        // classes may have generic types in ObjC, and ObjC classes cannot be
        // nested.

        // Import parameter constraints.
        SmallVector<TypeLoc, 1> inherited;
        if (objcGenericParam->hasExplicitBound()) {
          assert(!objcGenericParam->getUnderlyingType().isNull());
          auto clangBound =
            objcGenericParam->getUnderlyingType()
            ->castAs<clang::ObjCObjectPointerType>();
          if (clangBound->getInterfaceDecl()) {
            auto unqualifiedClangBound =
              clangBound->stripObjCKindOfTypeAndQuals(
                  Impl.getClangASTContext());
            Type superclassType = Impl.importType(
                clang::QualType(unqualifiedClangBound, 0),
                ImportTypeKind::Abstract, false, false);
            if (!superclassType) {
              return None;
            }
            inherited.push_back(TypeLoc::withoutLoc(superclassType));
          }
          for (clang::ObjCProtocolDecl *clangProto : clangBound->quals()) {
            ProtocolDecl *proto =
              cast_or_null<ProtocolDecl>(Impl.importDecl(clangProto, false));
            if (!proto) {
              return None;
            }
            inherited.push_back(TypeLoc::withoutLoc(proto->getDeclaredType()));
          }
        }
        if (inherited.empty()) {
          auto anyObjectProto =
            Impl.SwiftContext.getProtocol(KnownProtocolKind::AnyObject);
          if (!anyObjectProto) {
            return None;
          }
          inherited.push_back(
            TypeLoc::withoutLoc(anyObjectProto->getDeclaredType()));
        }
        genericParamDecl->setInherited(
            Impl.SwiftContext.AllocateCopy(inherited));

        genericParams.push_back(genericParamDecl);
      }
      return GenericParamList::create(Impl.SwiftContext,
            Impl.importSourceLoc(typeParamList->getLAngleLoc()),
            genericParams,
            Impl.importSourceLoc(typeParamList->getRAngleLoc()));
    }

    // Calculate the generic signature for the given imported generic param
    // list. If there are any errors in doing so, return 'nullptr'.
    GenericSignature *calculateGenericSignature(GenericParamList *genericParams,
                                                DeclContext *dc) {
      ArchetypeBuilder builder(*dc->getParentModule(), Impl.SwiftContext.Diags);
      for (auto param : *genericParams) {
        if (builder.addGenericParameter(param)) {
          return nullptr;
        }
      }
      for (auto param : *genericParams) {
        if (builder.addGenericParameterRequirements(param)) {
          return nullptr;
        }
      }
      // TODO: any need to infer requirements?
      if (builder.finalize(genericParams->getSourceRange().Start)) {
        return nullptr;
      }
      SmallVector<GenericTypeParamType *, 4> genericParamTypes;
      for (auto param : *genericParams) {
        genericParamTypes.push_back(
            param->getDeclaredType()->castTo<GenericTypeParamType>());
        auto *archetype = builder.getArchetype(param);
        param->setArchetype(archetype);
      }
      genericParams->setAllArchetypes(
          Impl.SwiftContext.AllocateCopy(builder.getAllArchetypes()));

      return builder.getGenericSignature(genericParamTypes);
    }

    /// Import members of the given Objective-C container and add them to the
    /// list of corresponding Swift members.
    void importObjCMembers(const clang::ObjCContainerDecl *decl,
                           DeclContext *swiftContext,
                           llvm::SmallPtrSet<Decl *, 4> &knownMembers,
                           SmallVectorImpl<Decl *> &members) {
      for (auto m = decl->decls_begin(), mEnd = decl->decls_end();
           m != mEnd; ++m) {
        auto nd = dyn_cast<clang::NamedDecl>(*m);
        if (!nd || nd != nd->getCanonicalDecl())
          continue;

        auto member = Impl.importDecl(nd, useSwift2Name);
        if (!member) continue;

        if (auto objcMethod = dyn_cast<clang::ObjCMethodDecl>(nd)) {
          // If there is an alternate declaration for this member, add it.
          if (auto alternate = Impl.getAlternateDecl(member)) {
            if (alternate->getDeclContext() == member->getDeclContext() &&
                knownMembers.insert(alternate).second)
              members.push_back(alternate);
          }

          // If this declaration shouldn't be visible, don't add it to
          // the list.
          if (Impl.shouldSuppressDeclImport(objcMethod)) continue;
        }

        members.push_back(member);
      }
    }

    static bool
    classImplementsProtocol(const clang::ObjCInterfaceDecl *constInterface,
                            const clang::ObjCProtocolDecl *constProto,
                            bool checkCategories) {
      auto interface = const_cast<clang::ObjCInterfaceDecl *>(constInterface);
      auto proto = const_cast<clang::ObjCProtocolDecl *>(constProto);
      return interface->ClassImplementsProtocol(proto, checkCategories);
    }

    /// \brief Import the members of all of the protocols to which the given
    /// Objective-C class, category, or extension explicitly conforms into
    /// the given list of members, so long as the method was not already
    /// declared in the class.
    ///
    /// FIXME: This whole thing is a hack, because name lookup should really
    /// just find these members when it looks in the protocol. Unfortunately,
    /// that's not something the name lookup code can handle right now, and
    /// it may still be necessary when the protocol's instance methods become
    /// class methods on a root class (e.g. NSObject-the-protocol's instance
    /// methods become class methods on NSObject).
    void importMirroredProtocolMembers(const clang::ObjCContainerDecl *decl,
                                       DeclContext *dc,
                                       ArrayRef<ProtocolDecl *> protocols,
                                       SmallVectorImpl<Decl *> &members,
                                       ASTContext &Ctx) {
      assert(dc);
      const clang::ObjCInterfaceDecl *interfaceDecl = nullptr;
      const ClangModuleUnit *declModule;
      const ClangModuleUnit *interfaceModule;

      for (auto proto : protocols) {
        auto clangProto =
          cast_or_null<clang::ObjCProtocolDecl>(proto->getClangDecl());
        if (!clangProto)
          continue;

        if (!interfaceDecl) {
          declModule = Impl.getClangModuleForDecl(decl);
          if ((interfaceDecl = dyn_cast<clang::ObjCInterfaceDecl>(decl))) {
            interfaceModule = declModule;
          } else {
            auto category = cast<clang::ObjCCategoryDecl>(decl);
            interfaceDecl = category->getClassInterface();
            interfaceModule = Impl.getClangModuleForDecl(interfaceDecl);
          }
        }

        // Don't import a protocol's members if the superclass already adopts
        // the protocol, or (for categories) if the class itself adopts it
        // in its main @interface.
        if (decl != interfaceDecl)
          if (classImplementsProtocol(interfaceDecl, clangProto, false))
            continue;
        if (auto superInterface = interfaceDecl->getSuperClass())
          if (classImplementsProtocol(superInterface, clangProto, true))
            continue;

        for (auto member : proto->getMembers()) {
          // Skip Swift 2 stubs; there's no reason to mirror them.
          if (member->getAttrs().isUnavailableInCurrentSwift())
            continue;

          if (auto prop = dyn_cast<VarDecl>(member)) {
            auto objcProp =
              dyn_cast_or_null<clang::ObjCPropertyDecl>(prop->getClangDecl());
            if (!objcProp)
              continue;

            // We can't import a property if there's already a method with this
            // name. (This also covers other properties with that same name.)
            // FIXME: We should still mirror the setter as a method if it's
            // not already there.
            clang::Selector sel = objcProp->getGetterName();
            if (interfaceDecl->getInstanceMethod(sel))
              continue;

            bool inNearbyCategory =
                std::any_of(interfaceDecl->visible_categories_begin(),
                            interfaceDecl->visible_categories_end(),
                            [=](const clang::ObjCCategoryDecl *category)->bool {
              if (category != decl) {
                auto *categoryModule = Impl.getClangModuleForDecl(category);
                if (categoryModule != declModule &&
                    categoryModule != interfaceModule) {
                  return false;
                }
              }
              return category->getInstanceMethod(sel);
            });
            if (inNearbyCategory)
              continue;

            if (auto imported = Impl.importMirroredDecl(objcProp, dc,
                                                        useSwift2Name, proto)) {
              members.push_back(imported);
              // FIXME: We should mirror properties of the root class onto the
              // metatype.
            }

            continue;
          }

          auto afd = dyn_cast<AbstractFunctionDecl>(member);
          if (!afd)
            continue;

          if (auto func = dyn_cast<FuncDecl>(afd))
            if (func->isAccessor())
              continue;

          auto objcMethod =
            dyn_cast_or_null<clang::ObjCMethodDecl>(member->getClangDecl());
          if (!objcMethod)
            continue;

          // When mirroring an initializer, make it designated and required.
          if (Impl.isInitMethod(objcMethod)) {
            // Import the constructor.
            if (auto imported = importConstructor(
                                  objcMethod, dc, /*implicit=*/true,
                                  CtorInitializerKind::Designated,
                                  /*required=*/true)){
              members.push_back(imported);
            }

            continue;
          }

          // Import the method.
          if (auto imported = Impl.importMirroredDecl(objcMethod, dc,
                                                      useSwift2Name, proto)) {
            members.push_back(imported);

            if (auto alternate = Impl.getAlternateDecl(imported))
              if (imported->getDeclContext() == alternate->getDeclContext())
                members.push_back(alternate);
          }
        }
      }
    }

    /// \brief Import constructors from our superclasses (and their
    /// categories/extensions), effectively "inheriting" constructors.
    void importInheritedConstructors(ClassDecl *classDecl,
                                     SmallVectorImpl<Decl *> &newMembers) {
      if (!classDecl->hasSuperclass())
        return;

      auto curObjCClass
        = cast<clang::ObjCInterfaceDecl>(classDecl->getClangDecl());

      auto inheritConstructors = [&](DeclRange members,
                                     Optional<CtorInitializerKind> kind) {
        for (auto member : members) {
          auto ctor = dyn_cast<ConstructorDecl>(member);
          if (!ctor)
            continue;

          // Don't inherit Swift 2 stubs.
          if (ctor->getAttrs().isUnavailableInCurrentSwift())
            continue;

          // Don't inherit (non-convenience) factory initializers.
          // Note that convenience factories return instancetype and can be
          // inherited.
          switch (ctor->getInitKind()) {
          case CtorInitializerKind::Factory:
            continue;
          case CtorInitializerKind::ConvenienceFactory:
          case CtorInitializerKind::Convenience:
          case CtorInitializerKind::Designated:
            break;
          }

          auto objcMethod
            = dyn_cast_or_null<clang::ObjCMethodDecl>(ctor->getClangDecl());
          if (!objcMethod)
            continue;

          auto &clangSourceMgr = Impl.getClangASTContext().getSourceManager();
          clang::PrettyStackTraceDecl trace(objcMethod, clang::SourceLocation(),
                                            clangSourceMgr,
                                            "importing (inherited)");

          // If this initializer came from a factory method, inherit
          // it as an initializer.
          if (objcMethod->isClassMethod()) {
            assert(ctor->getInitKind() ==
                     CtorInitializerKind::ConvenienceFactory);

            Optional<ImportedName> swift3Name;
            ImportedName importedName = importFullName(objcMethod, swift3Name);
            assert(!swift3Name &&
                   "Import inherited initializers never references swift3name");
            importedName.HasCustomName = true;
            bool redundant;
            if (auto newCtor = importConstructor(objcMethod, classDecl,
                                                 /*implicit=*/true,
                                                 ctor->getInitKind(),
                                                 /*required=*/false, 
                                                 ctor->getObjCSelector(),
                                                 importedName,
                                                 objcMethod->parameters(),
                                                 objcMethod->isVariadic(),
                                                 redundant)) {
              // If this is a Swift 2 stub, mark it as such.
              if (swift3Name)
                markAsSwift2Variant(newCtor, *swift3Name);

              Impl.importAttributes(objcMethod, newCtor, curObjCClass);
              newMembers.push_back(newCtor);
            }
            continue;
          }

          // Figure out what kind of constructor this will be.
          CtorInitializerKind myKind;
          bool isRequired = false;
          if (ctor->isRequired()) {
            // Required initializers are always considered designated.
            isRequired = true;
            myKind = CtorInitializerKind::Designated;
          } else if (kind) {
            myKind = *kind;
          } else {
            myKind = ctor->getInitKind();
          }

          // Import the constructor into this context.
          if (auto newCtor = importConstructor(objcMethod, classDecl,
                                               /*implicit=*/true,
                                               myKind,
                                               isRequired)) {
            Impl.importAttributes(objcMethod, newCtor, curObjCClass);
            newMembers.push_back(newCtor);
          }
        }
      };

      // The kind of initializer to import. If this class has designated
      // initializers, everything it imports is a convenience initializer.
      Optional<CtorInitializerKind> kind;
      if (Impl.hasDesignatedInitializers(curObjCClass))
        kind = CtorInitializerKind::Convenience;

      auto superclass
        = cast<ClassDecl>(classDecl->getSuperclass()->getAnyNominal());

      // If we have a superclass, import from it.
      if (auto superclassClangDecl = superclass->getClangDecl()) {
        if (isa<clang::ObjCInterfaceDecl>(superclassClangDecl)) {
          inheritConstructors(superclass->getMembers(), kind);

          for (auto ext : superclass->getExtensions())
            inheritConstructors(ext->getMembers(), kind);
        }
      }
    }

    Decl *VisitObjCCategoryDecl(const clang::ObjCCategoryDecl *decl) {
      // If the declaration is invalid, fail.
      if (decl->isInvalidDecl()) return nullptr;

      // Objective-C categories and extensions map to Swift extensions.
      if (ClangImporter::Implementation::hasNativeSwiftDecl(decl))
        return nullptr;

      // Find the Swift class being extended.
      auto objcClass
        = cast_or_null<ClassDecl>(Impl.importDecl(decl->getClassInterface(),
                                                  false));
      if (!objcClass)
        return nullptr;

      auto dc = Impl.importDeclContextOf(decl, decl->getDeclContext());
      if (!dc)
        return nullptr;

      auto loc = Impl.importSourceLoc(decl->getLocStart());
      auto result = ExtensionDecl::create(
                      Impl.SwiftContext, loc,
                      TypeLoc::withoutLoc(objcClass->getDeclaredType()),
                      { }, dc, nullptr, decl);

      // Determine the type and generic args of the extension.
      if (objcClass->getGenericParams()) {
        // Clone generic parameters.
        SmallVector<GenericTypeParamDecl *, 2> toGenericParams;
        for (auto fromGP : *objcClass->getGenericParams()) {
          // Create the new generic parameter.
          auto toGP = new (Impl.SwiftContext) GenericTypeParamDecl(
              result, fromGP->getName(), SourceLoc(), fromGP->getDepth(),
              fromGP->getIndex());
          toGP->setImplicit(true);
          toGP->setInherited(
              Impl.SwiftContext.AllocateCopy(fromGP->getInherited()));
          // Record new generic parameter.
          toGenericParams.push_back(toGP);
        }

        auto genericParams = GenericParamList::create(Impl.SwiftContext,
            SourceLoc(), toGenericParams, SourceLoc());
        result->setGenericParams(genericParams);
        if (auto sig = calculateGenericSignature(genericParams, result)) {
          result->setGenericSignature(sig);
        } else {
          return nullptr;
        }
        // Calculate the correct bound-generic extended type.
        SmallVector<Type, 2> genericArgs;
        for (auto gp : *genericParams) {
          genericArgs.push_back(gp->getArchetype());
        }
        Type extendedType =
          BoundGenericClassType::get(objcClass, nullptr, genericArgs);
        result->getExtendedTypeLoc().setType(extendedType);
      }

      // Create the extension declaration and record it.
      objcClass->addExtension(result);
      Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] = result;
      SmallVector<TypeLoc, 4> inheritedTypes;
      importObjCProtocols(result, decl->getReferencedProtocols(),
                          inheritedTypes);
      result->setValidated();
      result->setInherited(Impl.SwiftContext.AllocateCopy(inheritedTypes));
      result->setCheckedInheritanceClause();
      result->setMemberLoader(&Impl, 0);

      return result;
    }

    template <typename T, typename U>
    T *resolveSwiftDeclImpl(const U *decl, Identifier name, Module *adapter) {
      SmallVector<ValueDecl *, 4> results;
      adapter->lookupValue({}, name, NLKind::QualifiedLookup, results);
      T *found = nullptr;
      for (auto result : results) {
        if (auto singleResult = dyn_cast<T>(result)) {
          if (auto typeResolver = Impl.getTypeResolver())
            typeResolver->resolveDeclSignature(singleResult);

          // Skip Swift 2 variants.
          if (singleResult->getAttrs().isUnavailableInCurrentSwift())
            continue;

          if (found)
            return nullptr;

          found = singleResult;
        }
      }

      if (found)
        Impl.ImportedDecls[{decl->getCanonicalDecl(), false}] = found;

      return found;
    }

    template <typename T, typename U>
    T *resolveSwiftDecl(const U *decl, Identifier name,
                        ClangModuleUnit *clangModule) {
      if (auto adapter = clangModule->getAdapterModule())
        return resolveSwiftDeclImpl<T>(decl, name, adapter);
      if (clangModule == Impl.ImportedHeaderUnit) {
        // Use an index-based loop because new owners can come in as we're
        // iterating.
        for (size_t i = 0; i < Impl.ImportedHeaderOwners.size(); ++i) {
          Module *owner = Impl.ImportedHeaderOwners[i];
          if (T *result = resolveSwiftDeclImpl<T>(decl, name, owner))
            return result;
        }
      }
      return nullptr;
    }

    template <typename T, typename U>
    bool hasNativeSwiftDecl(const U *decl, Identifier name,
                            const DeclContext *dc, T *&swiftDecl) {
      if (!ClangImporter::Implementation::hasNativeSwiftDecl(decl))
        return false;
      if (auto *nameAttr = decl->template getAttr<clang::SwiftNameAttr>()) {
        StringRef customName = nameAttr->getName();
        if (Lexer::isIdentifier(customName))
          name = Impl.SwiftContext.getIdentifier(customName);
      }
      auto wrapperUnit = cast<ClangModuleUnit>(dc->getModuleScopeContext());
      swiftDecl = resolveSwiftDecl<T>(decl, name, wrapperUnit);
      return true;
    }

    void markMissingSwiftDecl(ValueDecl *VD) {
      const char *message;
      if (isa<ClassDecl>(VD))
        message = "cannot find Swift declaration for this class";
      else if (isa<ProtocolDecl>(VD))
        message = "cannot find Swift declaration for this protocol";
      else
        llvm_unreachable("unknown bridged decl kind");
      auto attr = AvailableAttr::createUnconditional(Impl.SwiftContext,
                                                        message);
      VD->getAttrs().add(attr);
    }
    
    Decl *VisitObjCProtocolDecl(const clang::ObjCProtocolDecl *decl) {
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);
      if (!importedName) return nullptr;

      // If we've been asked to produce a Swift 2 stub, handle it via a
      // typealias.
      if (swift3Name)
        return importSwift2TypeAlias(decl, importedName, *swift3Name);

      Identifier name = importedName.Imported.getBaseName();

      // FIXME: Figure out how to deal with incomplete protocols, since that
      // notion doesn't exist in Swift.
      if (!decl->hasDefinition()) {
        // Check if this protocol is implemented in its adapter.
        if (auto clangModule = Impl.getClangModuleForDecl(decl, true))
          if (auto native = resolveSwiftDecl<ProtocolDecl>(decl, name,
                                                           clangModule))
            return native;

        forwardDeclaration = true;
        return nullptr;
      }

      decl = decl->getDefinition();

      auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
      if (!dc)
        return nullptr;

      ProtocolDecl *nativeDecl;
      bool declaredNative = hasNativeSwiftDecl(decl, name, dc, nativeDecl);
      if (declaredNative && nativeDecl)
        return nativeDecl;

      // Create the protocol declaration and record it.
      auto result = Impl.createDeclWithClangNode<ProtocolDecl>(decl,
                                   Accessibility::Public,
                                   dc,
                                   Impl.importSourceLoc(decl->getLocStart()),
                                   Impl.importSourceLoc(decl->getLocation()),
                                   name,
                                   None);
      result->computeType();
      addObjCAttribute(result, Impl.importIdentifier(decl->getIdentifier()));

      if (declaredNative)
        markMissingSwiftDecl(result);

      Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] = result;

      // Create the archetype for the implicit 'Self'.
      auto selfId = Impl.SwiftContext.Id_Self;
      auto selfDecl = result->getProtocolSelf();
      ArchetypeType *selfArchetype =
                           ArchetypeType::getNew(Impl.SwiftContext, nullptr,
                                                 result, selfId,
                                                 Type(result->getDeclaredType()),
                                                 Type(), false);
      selfDecl->setArchetype(selfArchetype);

      // Set AllArchetypes of the protocol. ObjC protocols don't have associated
      // types so only the Self archetype is present.
      
      result->getGenericParams()->setAllArchetypes(
             Impl.SwiftContext.AllocateCopy(llvm::makeArrayRef(selfArchetype)));
      
      // Set the generic parameters and requirements.
      auto genericParam = selfDecl->getDeclaredType()
                            ->castTo<GenericTypeParamType>();
      Requirement genericRequirements[2] = {
        Requirement(RequirementKind::WitnessMarker, genericParam, Type()),
        Requirement(RequirementKind::Conformance, genericParam,
                    result->getDeclaredType())
      };
      auto sig = GenericSignature::get(genericParam, genericRequirements);
      result->setGenericSignature(sig);

      result->setCircularityCheck(CircularityCheck::Checked);

      // Import protocols this protocol conforms to.
      SmallVector<TypeLoc, 4> inheritedTypes;
      importObjCProtocols(result, decl->getReferencedProtocols(),
                          inheritedTypes);
      result->setInherited(Impl.SwiftContext.AllocateCopy(inheritedTypes));
      result->setCheckedInheritanceClause();

      result->setMemberLoader(&Impl, 0);

      // Add the protocol decl to ExternalDefinitions so that IRGen can emit
      // metadata for it.
      // FIXME: There might be better ways to do this.
      Impl.registerExternalDecl(result);

      return result;
    }

    // Add inferred attributes.
    void addInferredAttributes(Decl *decl, unsigned attributes) {
      using namespace inferred_attributes;
      if (attributes & requires_stored_property_inits) {
        auto a = new (Impl.SwiftContext)
          RequiresStoredPropertyInitsAttr(/*IsImplicit=*/true);
        decl->getAttrs().add(a);
        cast<ClassDecl>(decl)->setRequiresStoredPropertyInits(true);
      }
    }

    Decl *VisitObjCInterfaceDecl(const clang::ObjCInterfaceDecl *decl) {
      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);
      if (!importedName) return nullptr;

      // If we've been asked to produce a Swift 2 stub, handle it via a
      // typealias.
      if (swift3Name)
        return importSwift2TypeAlias(decl, importedName, *swift3Name);

      auto name = importedName.Imported.getBaseName();

      auto createRootClass = [=](DeclContext *dc = nullptr) -> ClassDecl * {
        if (!dc) {
          dc = Impl.getClangModuleForDecl(decl->getCanonicalDecl(),
                                          /*forwardDeclaration=*/true);
        }

        auto result = Impl.createDeclWithClangNode<ClassDecl>(decl,
                                                        Accessibility::Open,
                                                        SourceLoc(), name,
                                                        SourceLoc(), None,
                                                        nullptr, dc);
        result->computeType();
        Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] = result;
        result->setCircularityCheck(CircularityCheck::Checked);
        result->setSuperclass(Type());
        result->setCheckedInheritanceClause();
        result->setAddedImplicitInitializers(); // suppress all initializers
        addObjCAttribute(result, Impl.importIdentifier(decl->getIdentifier()));
        Impl.registerExternalDecl(result);
        return result;
      };

      // Special case for Protocol, which gets forward-declared as an ObjC
      // class which is hidden in modern Objective-C runtimes.
      // We treat it as a foreign class (like a CF type) because it doesn't
      // have a real public class object.
      clang::ASTContext &clangCtx = Impl.getClangASTContext();
      if (decl->getCanonicalDecl() ==
          clangCtx.getObjCProtocolDecl()->getCanonicalDecl()) {
        Type nsObjectTy = Impl.getNSObjectType();
        if (!nsObjectTy)
          return nullptr;
        const ClassDecl *nsObjectDecl =
          nsObjectTy->getClassOrBoundGenericClass();

        auto result = createRootClass(nsObjectDecl->getDeclContext());
        result->setForeignClassKind(ClassDecl::ForeignKind::RuntimeOnly);
        return result;
      }

      if (!decl->hasDefinition()) {
        // Check if this class is implemented in its adapter.
        if (auto clangModule = Impl.getClangModuleForDecl(decl, true)) {
          if (auto native = resolveSwiftDecl<ClassDecl>(decl, name,
                                                        clangModule)) {
            return native;
          }
        }

        if (Impl.ImportForwardDeclarations) {
          // Fake it by making an unavailable opaque @objc root class.
          auto result = createRootClass();
          result->setImplicit();
          auto attr = AvailableAttr::createUnconditional(Impl.SwiftContext,
              "This Objective-C class has only been forward-declared; "
              "import its owning module to use it");
          result->getAttrs().add(attr);
          return result;
        }

        forwardDeclaration = true;
        return nullptr;
      }

      decl = decl->getDefinition();
      assert(decl);

      auto dc = Impl.importDeclContextOf(decl, importedName.EffectiveContext);
      if (!dc)
        return nullptr;

      ClassDecl *nativeDecl;
      bool declaredNative = hasNativeSwiftDecl(decl, name, dc, nativeDecl);
      if (declaredNative && nativeDecl)
        return nativeDecl;

      // Create the class declaration and record it.
      auto result = Impl.createDeclWithClangNode<ClassDecl>(decl,
                                Accessibility::Open,
                                Impl.importSourceLoc(decl->getLocStart()),
                                name,
                                Impl.importSourceLoc(decl->getLocation()),
                                None, nullptr, dc);

      // Import generic arguments, if any.
      if (auto gpImportResult = importObjCGenericParams(decl, dc)) {
        auto genericParams = *gpImportResult;
        if (genericParams) {
          result->setGenericParams(genericParams);
          if (auto sig = calculateGenericSignature(genericParams, result)) {
            result->setGenericSignature(sig);
          } else {
            return nullptr;
          }
        }
      } else {
        return nullptr;
      }

      result->computeType();

      Impl.ImportedDecls[{decl->getCanonicalDecl(), useSwift2Name}] = result;
      result->setCircularityCheck(CircularityCheck::Checked);
      result->setAddedImplicitInitializers();
      addObjCAttribute(result, Impl.importIdentifier(decl->getIdentifier()));

      if (declaredNative)
        markMissingSwiftDecl(result);
      if (decl->getAttr<clang::ObjCRuntimeVisibleAttr>())
        result->setForeignClassKind(ClassDecl::ForeignKind::RuntimeOnly);

      // If this Objective-C class has a supertype, import it.
      SmallVector<TypeLoc, 4> inheritedTypes;
      Type superclassType;
      if (decl->getSuperClass()) {
        auto clangSuperclassType =
          Impl.getClangASTContext().getObjCObjectPointerType(
              clang::QualType(decl->getSuperClassType(), 0));
        superclassType = Impl.importType(clangSuperclassType,
                                         ImportTypeKind::Abstract,
                                         isInSystemModule(dc),
                                         /*isFullyBridgeable*/false);
        if (superclassType) {
          superclassType =
              ArchetypeBuilder::mapTypeOutOfContext(result, superclassType);
          assert(superclassType->is<ClassType>() ||
                 superclassType->is<BoundGenericClassType>());
          inheritedTypes.push_back(TypeLoc::withoutLoc(superclassType));
        }
      }
      result->setSuperclass(superclassType);

      // Import protocols this class conforms to.
      importObjCProtocols(result, decl->getReferencedProtocols(),
                          inheritedTypes);
      result->setInherited(Impl.SwiftContext.AllocateCopy(inheritedTypes));
      result->setCheckedInheritanceClause();

      // Add inferred attributes.
#define INFERRED_ATTRIBUTES(ModuleName, ClassName, AttributeSet)        \
      if (name.str().equals(#ClassName) &&                              \
          result->getParentModule()->getName().str().equals(#ModuleName)) {  \
        using namespace inferred_attributes;                            \
        addInferredAttributes(result, AttributeSet);                    \
      }
#include "InferredAttributes.def"

      result->setMemberLoader(&Impl, 0);

      // Pass the class to the type checker to create an implicit destructor.
      Impl.registerExternalDecl(result);

      return result;
    }

    Decl *VisitObjCImplDecl(const clang::ObjCImplDecl *decl) {
      // Implementations of Objective-C classes and categories are not
      // reflected into Swift.
      return nullptr;
    }

    Decl *VisitObjCPropertyDecl(const clang::ObjCPropertyDecl *decl) {
      auto dc = Impl.importDeclContextOf(decl, decl->getDeclContext());
      if (!dc)
        return nullptr;

      // While importing the DeclContext, we might have imported the decl
      // itself.
      if (auto Known = Impl.importDeclCached(decl, useSwift2Name))
        return Known;

      return VisitObjCPropertyDecl(decl, dc);
    }

    void applyPropertyOwnership(
        VarDecl *prop, clang::ObjCPropertyDecl::PropertyAttributeKind attrs) {
      Type ty = prop->getType();
      if (auto innerTy = ty->getAnyOptionalObjectType())
        ty = innerTy;
      if (!ty->is<GenericTypeParamType>() && !ty->isAnyClassReferenceType())
        return;

      ASTContext &ctx = prop->getASTContext();
      if (attrs & clang::ObjCPropertyDecl::OBJC_PR_copy) {
        prop->getAttrs().add(new (ctx) NSCopyingAttr(false));
        return;
      }
      if (attrs & clang::ObjCPropertyDecl::OBJC_PR_weak) {
        prop->getAttrs().add(new (ctx) OwnershipAttr(Ownership::Weak));
        prop->overwriteType(WeakStorageType::get(prop->getType(), ctx));
        return;
      }
      if ((attrs & clang::ObjCPropertyDecl::OBJC_PR_assign) ||
          (attrs & clang::ObjCPropertyDecl::OBJC_PR_unsafe_unretained)) {
        prop->getAttrs().add(new (ctx) OwnershipAttr(Ownership::Unmanaged));
        prop->overwriteType(UnmanagedStorageType::get(prop->getType(), ctx));
        return;
      }
    }

    /// Hack: Handle the case where a property is declared \c readonly in the
    /// main class interface (either explicitly or because of an adopted
    /// protocol) and then \c readwrite in a category/extension.
    ///
    /// \see VisitObjCPropertyDecl
    void handlePropertyRedeclaration(VarDecl *original,
                                     const clang::ObjCPropertyDecl *redecl) {
      // If the property isn't from Clang, we can't safely update it.
      if (!original->hasClangNode())
        return;

      // If the original declaration was implicit, we may want to change that.
      if (original->isImplicit() && !redecl->isImplicit() &&
          !isa<clang::ObjCProtocolDecl>(redecl->getDeclContext()))
        original->setImplicit(false);

      if (!original->getAttrs().hasAttribute<OwnershipAttr>() &&
          !original->getAttrs().hasAttribute<NSCopyingAttr>()) {
        applyPropertyOwnership(original,
                               redecl->getPropertyAttributesAsWritten());
      }

      auto clangSetter = redecl->getSetterMethodDecl();
      if (!clangSetter)
        return;

      // The only other transformation we know how to do safely is add a
      // setter. If the property is already settable, we're done.
      if (original->isSettable(nullptr))
        return;

      FuncDecl *setter = importAccessor(clangSetter,
                                        original->getDeclContext());
      if (!setter)
        return;

      original->setComputedSetter(setter);
    }

    Decl *VisitObjCPropertyDecl(const clang::ObjCPropertyDecl *decl,
                                DeclContext *dc) {
      assert(dc);

      Optional<ImportedName> swift3Name;
      auto name = importFullName(decl, swift3Name).Imported.getBaseName();
      if (name.empty())
        return nullptr;

      if (Impl.isAccessibilityDecl(decl))
        return nullptr;

      // Check whether there is a function with the same name as this
      // property. If so, suppress the property; the user will have to use
      // the methods directly, to avoid ambiguities.
      auto containerTy = dc->getDeclaredTypeInContext();
      VarDecl *overridden = nullptr;
      SmallVector<ValueDecl *, 2> lookup;
      dc->lookupQualified(containerTy, name,
                          NL_QualifiedDefault | NL_KnownNoDependency,
                          Impl.getTypeResolver(), lookup);
      for (auto result : lookup) {
        if (isa<FuncDecl>(result) &&
            result->isInstanceMember() == decl->isInstanceProperty() &&
            result->getFullName().getArgumentNames().empty())
          return nullptr;

        if (auto var = dyn_cast<VarDecl>(result)) {
          // If the selectors of the getter match in Objective-C, we have an
          // override.
          if (var->isInstanceMember() == decl->isInstanceProperty() &&
              var->getObjCGetterSelector() ==
                Impl.importSelector(decl->getGetterName()))
            overridden = var;
        }
      }

      if (overridden) {
        const DeclContext *overrideContext = overridden->getDeclContext();
        if (overrideContext != dc &&
            overrideContext->getDeclaredTypeInContext()->isEqual(containerTy)) {
          // We've encountered a redeclaration of the property.
          // HACK: Just update the original declaration instead of importing a
          // second property.
          handlePropertyRedeclaration(overridden, decl);
          return nullptr;
        }
      }

      Type type = Impl.importPropertyType(decl, isInSystemModule(dc));
      if (!type)
        return nullptr;

      // Import the getter.
      FuncDecl *getter = nullptr;
      if (auto clangGetter = decl->getGetterMethodDecl()) {
        getter = importAccessor(clangGetter, dc);
        if (!getter)
          return nullptr;
      }

      // Import the setter, if there is one.
      FuncDecl *setter = nullptr;
      if (auto clangSetter = decl->getSetterMethodDecl()) {
        setter = importAccessor(clangSetter, dc);
        if (!setter)
          return nullptr;
      }

      // Check whether the property already got imported.
      if (dc == Impl.importDeclContextOf(decl, decl->getDeclContext())) {
        auto known = Impl.ImportedDecls.find({decl->getCanonicalDecl(),
                                              useSwift2Name});
        if (known != Impl.ImportedDecls.end())
          return known->second;
      }

      auto result = Impl.createDeclWithClangNode<VarDecl>(decl,
          getOverridableAccessibility(dc),
          decl->isClassProperty(), /*IsLet*/ false,
          Impl.importSourceLoc(decl->getLocation()),
          name, type, dc);
      result->setInterfaceType(ArchetypeBuilder::mapTypeOutOfContext(dc, type));
      
      // Turn this into a computed property.
      // FIXME: Fake locations for '{' and '}'?
      result->makeComputed(SourceLoc(), getter, setter, nullptr, SourceLoc());
      addObjCAttribute(result, None);
      applyPropertyOwnership(result, decl->getPropertyAttributesAsWritten());

      // Handle attributes.
      if (decl->hasAttr<clang::IBOutletAttr>())
        result->getAttrs().add(
            new (Impl.SwiftContext) IBOutletAttr(/*IsImplicit=*/false));
      if (decl->getPropertyImplementation() == clang::ObjCPropertyDecl::Optional
          && isa<ProtocolDecl>(dc) &&
          !result->getAttrs().hasAttribute<OptionalAttr>())
        result->getAttrs().add(new (Impl.SwiftContext)
                                      OptionalAttr(/*implicit*/false));
      // FIXME: Handle IBOutletCollection.

      if (overridden)
        result->setOverriddenDecl(overridden);

      // If this is a Swift 2 stub, mark it as such.
      if (swift3Name)
        markAsSwift2Variant(result, *swift3Name);

      return result;
    }

    Decl *
    VisitObjCCompatibleAliasDecl(const clang::ObjCCompatibleAliasDecl *decl) {
      // Import Objective-C’s @compatibility_alias as typealias.
      EffectiveClangContext effectiveContext(decl->getDeclContext()->getRedeclContext());
      auto dc = Impl.importDeclContextOf(decl, effectiveContext);
      if (!dc) return nullptr;

      Optional<ImportedName> swift3Name;
      auto importedName = importFullName(decl, swift3Name);
      auto name = importedName.Imported.getBaseName();

      if (name.empty()) return nullptr;

      auto importedDecl = Impl.importDecl(decl->getClassInterface(),
                                          /*useSwift2Name=*/false);
      auto typeDecl = dyn_cast_or_null<TypeDecl>(importedDecl);
      if (!typeDecl) return nullptr;

      // Create typealias.
      TypeAliasDecl *typealias = nullptr;
      typealias = Impl.createDeclWithClangNode<TypeAliasDecl>(
                    decl, Accessibility::Public,
                    Impl.importSourceLoc(decl->getLocStart()),
                    name,
                    Impl.importSourceLoc(decl->getLocation()),
                    TypeLoc::withoutLoc(typeDecl->getDeclaredType()),
                    /*genericparams=*/nullptr, dc);

      typealias->computeType();

      return typealias;
    }

    Decl *VisitLinkageSpecDecl(const clang::LinkageSpecDecl *decl) {
      // Linkage specifications are not imported.
      return nullptr;
    }

    Decl *VisitObjCPropertyImplDecl(const clang::ObjCPropertyImplDecl *decl) {
      // @synthesize and @dynamic are not imported, since they are not part
      // of the interface to a class.
      return nullptr;
    }

    Decl *VisitFileScopeAsmDecl(const clang::FileScopeAsmDecl *decl) {
      return nullptr;
    }

    Decl *VisitAccessSpecDecl(const clang::AccessSpecDecl *decl) {
      return nullptr;
    }

    Decl *VisitFriendDecl(const clang::FriendDecl *decl) {
      // Friends are not imported; Swift has a different access control
      // mechanism.
      return nullptr;
    }

    Decl *VisitFriendTemplateDecl(const clang::FriendTemplateDecl *decl) {
      // Friends are not imported; Swift has a different access control
      // mechanism.
      return nullptr;
    }

    Decl *VisitStaticAssertDecl(const clang::StaticAssertDecl *decl) {
      // Static assertions are an implementation detail.
      return nullptr;
    }

    Decl *VisitBlockDecl(const clang::BlockDecl *decl) {
      // Blocks are not imported (although block types can be imported).
      return nullptr;
    }

    Decl *VisitClassScopeFunctionSpecializationDecl(
                 const clang::ClassScopeFunctionSpecializationDecl *decl) {
      // Note: templates are not imported.
      return nullptr;
    }

    Decl *VisitImportDecl(const clang::ImportDecl *decl) {
      // Transitive module imports are not handled at the declaration level.
      // Rather, they are understood from the module itself.
      return nullptr;
    }
  };
}

Decl *ClangImporter::Implementation::importDeclCached(
    const clang::NamedDecl *ClangDecl,
    bool useSwift2Name) {
  auto Known = ImportedDecls.find({ClangDecl->getCanonicalDecl(),
                                   useSwift2Name});
  if (Known != ImportedDecls.end())
    return Known->second;

  return nullptr;
}

/// Checks if we don't need to import the typedef itself.  If the typedef
/// should be skipped, returns the underlying declaration that the typedef
/// refers to -- this declaration should be imported instead.
static const clang::TagDecl *
canSkipOverTypedef(ClangImporter::Implementation &Impl,
                   const clang::NamedDecl *D,
                   bool &TypedefIsSuperfluous) {
  // If we have a typedef that refers to a tag type of the same name,
  // skip the typedef and import the tag type directly.

  TypedefIsSuperfluous = false;

  auto *ClangTypedef = dyn_cast<clang::TypedefNameDecl>(D);
  if (!ClangTypedef)
    return nullptr;

  const clang::DeclContext *RedeclContext =
      ClangTypedef->getDeclContext()->getRedeclContext();
  if (!RedeclContext->isTranslationUnit())
    return nullptr;

  clang::QualType UnderlyingType = ClangTypedef->getUnderlyingType();

  // A typedef to a typedef should get imported as a typealias.
  auto *TypedefT = UnderlyingType->getAs<clang::TypedefType>();
  if (TypedefT)
    return nullptr;

  auto *TT = UnderlyingType->getAs<clang::TagType>();
  if (!TT)
    return nullptr;

  clang::TagDecl *UnderlyingDecl = TT->getDecl();
  if (UnderlyingDecl->getDeclContext()->getRedeclContext() != RedeclContext)
    return nullptr;

  if (UnderlyingDecl->getDeclName().isEmpty())
    return UnderlyingDecl;

  auto TypedefName = ClangTypedef->getDeclName();
  auto TagDeclName = UnderlyingDecl->getDeclName();
  if (TypedefName != TagDeclName)
    return nullptr;

  TypedefIsSuperfluous = true;
  return UnderlyingDecl;
}

clang::SwiftNewtypeAttr *ClangImporter::Implementation::getSwiftNewtypeAttr(
    const clang::TypedefNameDecl *decl,
    bool useSwift2Name) {
  // If we aren't honoring the swift_newtype attribute, don't even
  // bother looking.
  if (!HonorSwiftNewtypeAttr)
    return nullptr;

  // If we're determining the Swift 2 name, don't honor this attribute.
  if (useSwift2Name)
    return nullptr;

  // Retrieve the attribute.
  auto attr = decl->getAttr<clang::SwiftNewtypeAttr>();
  if (!attr) return nullptr;

  // Blacklist types that temporarily lose their
  // swift_wrapper/swift_newtype attributes in Foundation.
  auto name = decl->getName();
  if (name == "CFErrorDomain")
    return nullptr;

  return attr;
}

StringRef ClangImporter::Implementation::
getSwiftNameFromClangName(StringRef replacement) {
  auto &clangSema = getClangSema();

  clang::IdentifierInfo *identifier =
      &clangSema.getASTContext().Idents.get(replacement);
  clang::LookupResult lookupResult(clangSema, identifier,
                                   clang::SourceLocation(),
                                   clang::Sema::LookupOrdinaryName);
  if (!clangSema.LookupName(lookupResult, nullptr))
    return "";

  auto clangDecl = lookupResult.getAsSingle<clang::NamedDecl>();
  if (!clangDecl)
    return "";

  auto importedName = importFullName(clangDecl, None, &clangSema);
  if (!importedName)
    return "";

  llvm::SmallString<64> renamed;
  {
    // Render a swift_name string.
    llvm::raw_svector_ostream os(renamed);
    printSwiftName(importedName,
                   /*fullyQualified=*/true,
                   os);
  }

  return SwiftContext.AllocateCopy(StringRef(renamed));
}

/// Import Clang attributes as Swift attributes.
void ClangImporter::Implementation::importAttributes(
    const clang::NamedDecl *ClangDecl,
    Decl *MappedDecl,
    const clang::ObjCContainerDecl *NewContext)
{
  ASTContext &C = SwiftContext;

  if (auto maybeDefinition = getDefinitionForClangTypeDecl(ClangDecl))
    if (maybeDefinition.getValue())
      ClangDecl = cast<clang::NamedDecl>(maybeDefinition.getValue());

  // Scan through Clang attributes and map them onto Swift
  // equivalents.
  bool AnyUnavailable = MappedDecl->getAttrs().isUnavailable(C);
  for (clang::NamedDecl::attr_iterator AI = ClangDecl->attr_begin(),
       AE = ClangDecl->attr_end(); AI != AE; ++AI) {
    //
    // __attribute__((unavailable))
    //
    // Mapping: @available(*,unavailable)
    //
    if (auto unavailable = dyn_cast<clang::UnavailableAttr>(*AI)) {
      auto Message = unavailable->getMessage();
      auto attr = AvailableAttr::createUnconditional(C, Message);
      MappedDecl->getAttrs().add(attr);
      AnyUnavailable = true;
      continue;
    }

    //
    // __attribute__((annotate(swift1_unavailable)))
    //
    // Mapping: @available(*, unavailable)
    //
    if (auto unavailable_annot = dyn_cast<clang::AnnotateAttr>(*AI))
      if (unavailable_annot->getAnnotation() == "swift1_unavailable") {
        auto attr = AvailableAttr::createUnconditional(
            C, "", "", UnconditionalAvailabilityKind::UnavailableInSwift);
        MappedDecl->getAttrs().add(attr);
        AnyUnavailable = true;
        continue;
      }

    //
    // __attribute__((deprecated))
    //
    // Mapping: @available(*,deprecated)
    //
    if (auto deprecated = dyn_cast<clang::DeprecatedAttr>(*AI)) {
      auto Message = deprecated->getMessage();
      auto attr = AvailableAttr::createUnconditional(C, Message, "",
                    UnconditionalAvailabilityKind::Deprecated);
      MappedDecl->getAttrs().add(attr);
      continue;
    }

    // __attribute__((availability))
    //
    if (auto avail = dyn_cast<clang::AvailabilityAttr>(*AI)) {
      StringRef Platform = avail->getPlatform()->getName();

      // Is this our special "availability(swift, unavailable)" attribute?
      if (Platform == "swift") {
        auto replacement = avail->getReplacement();
        StringRef swiftReplacement = "";
        if (!replacement.empty())
          swiftReplacement = getSwiftNameFromClangName(replacement);

        auto attr = AvailableAttr::createUnconditional(
            C, avail->getMessage(), swiftReplacement,
            UnconditionalAvailabilityKind::UnavailableInSwift);
        MappedDecl->getAttrs().add(attr);
        AnyUnavailable = true;
        continue;
      }

      // Does this availability attribute map to the platform we are
      // currently targeting?
      if (!PlatformAvailabilityFilter ||
          !PlatformAvailabilityFilter(Platform))
        continue;

      auto platformK =
        llvm::StringSwitch<Optional<PlatformKind>>(Platform)
          .Case("ios", PlatformKind::iOS)
          .Case("macos", PlatformKind::OSX)
          .Case("tvos", PlatformKind::tvOS)
          .Case("watchos", PlatformKind::watchOS)
          .Case("ios_app_extension", PlatformKind::iOSApplicationExtension)
          .Case("macos_app_extension",
                PlatformKind::OSXApplicationExtension)
          .Case("tvos_app_extension",
                PlatformKind::tvOSApplicationExtension)
          .Case("watchos_app_extension",
                PlatformKind::watchOSApplicationExtension)
          .Default(None);
      if (!platformK)
        continue;

      // Is this declaration marked unconditionally unavailable?
      auto Unconditional = UnconditionalAvailabilityKind::None;
      if (avail->getUnavailable()) {
        Unconditional = UnconditionalAvailabilityKind::Unavailable;
        AnyUnavailable = true;
      }

      StringRef message = avail->getMessage();

      const auto &deprecated = avail->getDeprecated();
      if (!deprecated.empty()) {
        if (DeprecatedAsUnavailableFilter &&
            DeprecatedAsUnavailableFilter(deprecated.getMajor(),
                                          deprecated.getMinor())) {
          AnyUnavailable = true;
          Unconditional = UnconditionalAvailabilityKind::Unavailable;
          if (message.empty())
            message = DeprecatedAsUnavailableMessage;
        }
      }

      const auto &obsoleted = avail->getObsoleted();
      const auto &introduced = avail->getIntroduced();
      const auto &replacement = avail->getReplacement();

      StringRef swiftReplacement = "";
      if (!replacement.empty())
        swiftReplacement = getSwiftNameFromClangName(replacement);

      auto AvAttr = new (C) AvailableAttr(SourceLoc(), SourceRange(),
                                          platformK.getValue(),
                                          message, swiftReplacement,
                                          introduced, deprecated, obsoleted,
                                          Unconditional, /*implicit=*/false);

      MappedDecl->getAttrs().add(AvAttr);
    }
  }

  // If the declaration is unavailable, we're done.
  if (AnyUnavailable)
    return;

  // Ban NSInvocation.
  if (auto ID = dyn_cast<clang::ObjCInterfaceDecl>(ClangDecl)) {
    if (ID->getName() == "NSInvocation") {
      auto attr = AvailableAttr::createUnconditional(C, "");
      MappedDecl->getAttrs().add(attr);
      return;
    }
  }

  // Ban CFRelease|CFRetain|CFAutorelease(CFTypeRef) as well as custom ones
  // such as CGColorRelease(CGColorRef).
  if (auto FD = dyn_cast<clang::FunctionDecl>(ClangDecl)) {
    if (FD->getNumParams() == 1 &&
         (FD->getName().endswith("Release") ||
          FD->getName().endswith("Retain") ||
          FD->getName().endswith("Autorelease")))
      if (auto t = FD->getParamDecl(0)->getType()->getAs<clang::TypedefType>())
        if (isCFTypeDecl(t->getDecl())) {
          auto attr = AvailableAttr::createUnconditional(C,
            "Core Foundation objects are automatically memory managed");
          MappedDecl->getAttrs().add(attr);
          return;
        }
  }

  // Hack: mark any method named "print" with less than two parameters as
  // warn_unqualified_access.
  if (auto MD = dyn_cast<FuncDecl>(MappedDecl)) {
    if (!MD->getName().empty() && MD->getName().str() == "print" &&
        MD->getDeclContext()->isTypeContext()) {
      auto *formalParams = MD->getParameterList(1);
      if (formalParams->size() <= 1) {
        // Use a non-implicit attribute so it shows up in the generated
        // interface.
        MD->getAttrs().add(
            new (C) WarnUnqualifiedAccessAttr(/*implicit*/false));
      }
    }
  }

  // Map __attribute__((warn_unused_result)).
  if (!ClangDecl->hasAttr<clang::WarnUnusedResultAttr>()) {
    if (auto MD = dyn_cast<FuncDecl>(MappedDecl)) {
      if (!MD->getResultType()->isVoid()) {
        MD->getAttrs().add(new (C) DiscardableResultAttr(/*implicit*/true));
      }
    }
  }
  // Map __attribute__((const)).
  if (ClangDecl->hasAttr<clang::ConstAttr>()) {
    MappedDecl->getAttrs().add(new (C) EffectsAttr(EffectsKind::ReadNone));
  }
  // Map __attribute__((pure)).
  if (ClangDecl->hasAttr<clang::PureAttr>()) {
    MappedDecl->getAttrs().add(new (C) EffectsAttr(EffectsKind::ReadOnly));
  }
}

bool ClangImporter::Implementation::isUnavailableInSwift(
    const clang::Decl *decl) {
  // FIXME: Somewhat duplicated from importAttributes(), but this is a
  // more direct path.
  if (decl->getAvailability() == clang::AR_Unavailable) return true;

  // Apply the deprecated-as-unavailable filter.
  if (!DeprecatedAsUnavailableFilter) return false;

  for (auto *attr : decl->specific_attrs<clang::AvailabilityAttr>()) {
    if (attr->getPlatform()->getName() == "swift")
      return true;

    if (PlatformAvailabilityFilter &&
        !PlatformAvailabilityFilter(attr->getPlatform()->getName())){
      continue;
    }

    clang::VersionTuple version = attr->getDeprecated();
    if (version.empty())
      continue;
    if (DeprecatedAsUnavailableFilter(version.getMajor(),
                                      version.getMinor()))
      return true;
  }

  return false;
}

Decl *
ClangImporter::Implementation::importDeclImpl(const clang::NamedDecl *ClangDecl,
                                              bool useSwift2Name,
                                              bool &TypedefIsSuperfluous,
                                              bool &HadForwardDeclaration) {
  assert(ClangDecl);

  bool SkippedOverTypedef = false;
  Decl *Result = nullptr;
  if (auto *UnderlyingDecl = canSkipOverTypedef(*this, ClangDecl,
                                                TypedefIsSuperfluous)) {
    Result = importDecl(UnderlyingDecl, useSwift2Name);
    SkippedOverTypedef = true;
  }

  if (!Result) {
    SwiftDeclConverter converter(*this, useSwift2Name);
    Result = converter.Visit(ClangDecl);
    HadForwardDeclaration = converter.hadForwardDeclaration();
  }
  if (!Result && !useSwift2Name) {
    // If we couldn't import this Objective-C entity, determine
    // whether it was a required member of a protocol.
    bool hasMissingRequiredMember = false;
    if (auto clangProto
          = dyn_cast<clang::ObjCProtocolDecl>(ClangDecl->getDeclContext())) {
      if (auto method = dyn_cast<clang::ObjCMethodDecl>(ClangDecl)) {
        if (method->getImplementationControl()
              == clang::ObjCMethodDecl::Required)
          hasMissingRequiredMember = true;
      } else if (auto prop = dyn_cast<clang::ObjCPropertyDecl>(ClangDecl)) {
        if (prop->getPropertyImplementation()
              == clang::ObjCPropertyDecl::Required)
          hasMissingRequiredMember = true;
      }

      if (hasMissingRequiredMember) {
        // Mark the protocol as having missing requirements.
        if (auto proto = cast_or_null<ProtocolDecl>(importDecl(clangProto,
                                                               false))) {
          proto->setHasMissingRequirements(true);
        }
      }
    }

    return nullptr;
  }

  // Finalize the imported declaration.
  auto finalizeDecl = [&](Decl *result) {
    importAttributes(ClangDecl, result);

    // Hack to deal with unannotated Objective-C protocols. If the protocol
    // comes from clang and is not annotated and the protocol requirement
    // itself is not annotated, then infer availability of the requirement
    // based on its types. This makes it possible for a type to conform to an
    // Objective-C protocol that is missing annotations but whose requirements
    // use types that are less available than the conforming type.
    auto dc = result->getDeclContext();
    auto *proto = dyn_cast<ProtocolDecl>(dc);
    if (!proto || proto->getAttrs().hasAttribute<AvailableAttr>())
      return;

    inferProtocolMemberAvailability(*this, dc, result);
  };

  if (Result) {
    finalizeDecl(Result);

    if (auto alternate = getAlternateDecl(Result))
      finalizeDecl(alternate);
  }

#ifndef NDEBUG
  auto Canon = cast<clang::NamedDecl>(ClangDecl->getCanonicalDecl());

  // Note that the decl was imported from Clang.  Don't mark Swift decls as
  // imported.
  if (Result &&
      (!Result->getDeclContext()->isModuleScopeContext() ||
       isa<ClangModuleUnit>(Result->getDeclContext()))) {
    // Either the Swift declaration was from stdlib,
    // or we imported the underlying decl of the typedef,
    // or we imported the decl itself.
    bool ImportedCorrectly =
        !Result->getClangDecl() || SkippedOverTypedef ||
        Result->getClangDecl()->getCanonicalDecl() == Canon;

    // Or the other type is a typedef,
    if (!ImportedCorrectly &&
        isa<clang::TypedefNameDecl>(Result->getClangDecl())) {
      // both types are ValueDecls:
      if (isa<clang::ValueDecl>(Result->getClangDecl())) {
        ImportedCorrectly =
            getClangASTContext().hasSameType(
                cast<clang::ValueDecl>(Result->getClangDecl())->getType(),
                cast<clang::ValueDecl>(Canon)->getType());
      } else if (isa<clang::TypeDecl>(Result->getClangDecl())) {
        // both types are TypeDecls:
        ImportedCorrectly =
            getClangASTContext().hasSameUnqualifiedType(
                getClangASTContext().getTypeDeclType(
                    cast<clang::TypeDecl>(Result->getClangDecl())),
                getClangASTContext().getTypeDeclType(
                    cast<clang::TypeDecl>(Canon)));
      }
      assert(ImportedCorrectly);
    }
    assert(Result->hasClangNode());
  }
#else
  (void)SkippedOverTypedef;
#endif

  return Result;
}

void ClangImporter::Implementation::startedImportingEntity() {
  ++NumCurrentImportingEntities;
  ++NumTotalImportedEntities;
}

void ClangImporter::Implementation::finishedImportingEntity() {
  assert(NumCurrentImportingEntities &&
         "finishedImportingEntity not paired with startedImportingEntity");
  if (NumCurrentImportingEntities == 1) {
    // We decrease NumCurrentImportingEntities only after pending actions
    // are finished, to avoid recursively re-calling finishPendingActions().
    finishPendingActions();
  }
  --NumCurrentImportingEntities;
}

void ClangImporter::Implementation::finishPendingActions() {
  while (true) {
    if (!RegisteredExternalDecls.empty()) {
      if (hasFinishedTypeChecking()) {
        RegisteredExternalDecls.clear();
      } else {
        Decl *D = RegisteredExternalDecls.pop_back_val();
        SwiftContext.addExternalDecl(D);
        if (auto typeResolver = getTypeResolver())
          if (auto *nominal = dyn_cast<NominalTypeDecl>(D))
            if (!nominal->hasDelayedMembers())
              typeResolver->resolveExternalDeclImplicitMembers(nominal);
      }
    } else if (!DelayedProtocolConformances.empty()) {
      NormalProtocolConformance *conformance =
          DelayedProtocolConformances.pop_back_val();
      finishProtocolConformance(conformance);
    } else {
      break;
    }
  }
}

/// Finish the given protocol conformance (for an imported type)
/// by filling in any missing witnesses.
void ClangImporter::Implementation::finishProtocolConformance(
    NormalProtocolConformance *conformance) {
  const ProtocolDecl *proto = conformance->getProtocol();

  // Create witnesses for requirements not already met.
  for (auto req : proto->getMembers()) {
    auto valueReq = dyn_cast<ValueDecl>(req);
    if (!valueReq)
      continue;

    if (!conformance->hasWitness(valueReq)) {
      if (auto func = dyn_cast<AbstractFunctionDecl>(valueReq)){
        // For an optional requirement, record an empty witness:
        // we'll end up querying this at runtime.
        auto Attrs = func->getAttrs();
        if (Attrs.hasAttribute<OptionalAttr>()) {
          conformance->setWitness(valueReq, ConcreteDeclRef());
          continue;
        }
      }

      conformance->setWitness(valueReq, valueReq);
    } else {
      // An initializer that conforms to a requirement is required.
      auto witness = conformance->getWitness(valueReq, nullptr).getDecl();
      if (auto ctor = dyn_cast_or_null<ConstructorDecl>(witness)) {
        if (!ctor->getAttrs().hasAttribute<RequiredAttr>()) {
          ctor->getAttrs().add(
            new (SwiftContext) RequiredAttr(/*implicit=*/true));
        }
      }
    }
  }

  // And make sure any inherited conformances also get completed, if necessary.
  SmallVector<ProtocolDecl *, 8> inheritedProtos;
  for (auto *inherited : proto->getInheritedProtocols(/*resolver=*/nullptr)) {
    inheritedProtos.push_back(inherited);
  }
  // Sort for deterministic import.
  llvm::array_pod_sort(inheritedProtos.begin(),
                       inheritedProtos.end(),
                       [](ProtocolDecl * const *left,
                          ProtocolDecl * const *right) -> int {
    // We know all Objective-C protocols have unique names.
    auto getDeclName = [](const ProtocolDecl *proto) -> StringRef {
      return cast<clang::ObjCProtocolDecl>(proto->getClangDecl())->getName();
    };
    return getDeclName(*left).compare(getDeclName(*right));
  });
  // Schedule any that aren't complete.
  for (auto *inherited : inheritedProtos) {
    Module *M = conformance->getDeclContext()->getParentModule();
    auto inheritedConformance = M->lookupConformance(conformance->getType(),
                                                     inherited,
                                                     /*resolver=*/nullptr);
    assert(inheritedConformance && inheritedConformance->isConcrete() &&
           "inherited conformance not found");
    conformance->setInheritedConformance(inherited,
                                         inheritedConformance->getConcrete());
  }

  conformance->setState(ProtocolConformanceState::Complete);
}

Decl *ClangImporter::Implementation::importDeclAndCacheImpl(
    const clang::NamedDecl *ClangDecl,
    bool useSwift2Name,
    bool SuperfluousTypedefsAreTransparent) {
  if (!ClangDecl)
    return nullptr;

  clang::PrettyStackTraceDecl trace(ClangDecl, clang::SourceLocation(),
                                    Instance->getSourceManager(), "importing");

  auto Canon = cast<clang::NamedDecl>(ClangDecl->getCanonicalDecl());

  if (auto Known = importDeclCached(Canon, useSwift2Name)) {
    if (!SuperfluousTypedefsAreTransparent &&
        SuperfluousTypedefs.count(Canon))
      return nullptr;
    return Known;
  }

  bool TypedefIsSuperfluous = false;
  bool HadForwardDeclaration = false;

  ImportingEntityRAII ImportingEntity(*this);
  Decl *Result = importDeclImpl(ClangDecl, useSwift2Name, TypedefIsSuperfluous,
                                HadForwardDeclaration);
  if (!Result)
    return nullptr;

  if (TypedefIsSuperfluous) {
    SuperfluousTypedefs.insert(Canon);
    if (auto tagDecl = dyn_cast_or_null<clang::TagDecl>(Result->getClangDecl()))
      DeclsWithSuperfluousTypedefs.insert(tagDecl);
  }

  if (!HadForwardDeclaration)
    ImportedDecls[{Canon, useSwift2Name}] = Result;

  if (!SuperfluousTypedefsAreTransparent && TypedefIsSuperfluous)
    return nullptr;

  return Result;
}

Decl *
ClangImporter::Implementation::importMirroredDecl(const clang::NamedDecl *decl,
                                                  DeclContext *dc,
                                                  bool useSwift2Name,
                                                  ProtocolDecl *proto) {
  assert(dc);
  if (!decl)
    return nullptr;

  clang::PrettyStackTraceDecl trace(decl, clang::SourceLocation(),
                                    Instance->getSourceManager(),
                                    "importing (mirrored)");

  auto canon = decl->getCanonicalDecl();
  auto known = ImportedProtocolDecls.find(std::make_tuple(canon, dc,
                                                          useSwift2Name));
  if (known != ImportedProtocolDecls.end())
    return known->second;

  SwiftDeclConverter converter(*this, useSwift2Name);
  Decl *result;
  if (auto method = dyn_cast<clang::ObjCMethodDecl>(decl)) {
    result = converter.VisitObjCMethodDecl(method, dc);
  } else if (auto prop = dyn_cast<clang::ObjCPropertyDecl>(decl)) {
    result = converter.VisitObjCPropertyDecl(prop, dc);
  } else {
    llvm_unreachable("unexpected mirrored decl");
  }

  if (result) {
    assert(result->getClangDecl() && result->getClangDecl() == canon);

    auto updateMirroredDecl = [&](Decl *result) {
      result->setImplicit();
    
      // Map the Clang attributes onto Swift attributes.
      importAttributes(decl, result);

      if (proto->getAttrs().hasAttribute<AvailableAttr>()) {
        if (!result->getAttrs().hasAttribute<AvailableAttr>()) {
          AvailabilityContext protoRange =
            AvailabilityInference::availableRange(proto, SwiftContext);
          applyAvailableAttribute(result, protoRange, SwiftContext);
        }
      } else {
        // Infer the same availability for the mirrored declaration as
        // we would for the protocol member it is mirroring.
        inferProtocolMemberAvailability(*this, dc, result);
      }
    };

    updateMirroredDecl(result);

    // Update the alternate declaration as well.
    if (auto alternate = getAlternateDecl(result))
      updateMirroredDecl(alternate);
  }
  if (result || !converter.hadForwardDeclaration())
    ImportedProtocolDecls[std::make_tuple(canon, dc, useSwift2Name)] = result;
  return result;
}

DeclContext *ClangImporter::Implementation::importDeclContextImpl(
    const clang::DeclContext *dc) {
  // Every declaration should come from a module, so we should not see the
  // TranslationUnit DeclContext here.
  assert(!dc->isTranslationUnit());

  auto decl = dyn_cast<clang::NamedDecl>(dc);
  if (!decl)
    return nullptr;

  auto swiftDecl = importDecl(decl, false);
  if (!swiftDecl)
    return nullptr;

  if (auto nominal = dyn_cast<NominalTypeDecl>(swiftDecl))
    return nominal;
  if (auto extension = dyn_cast<ExtensionDecl>(swiftDecl))
    return extension;
  if (auto constructor = dyn_cast<ConstructorDecl>(swiftDecl))
    return constructor;
  if (auto destructor = dyn_cast<DestructorDecl>(swiftDecl))
    return destructor;
  return nullptr;
}

DeclContext *
ClangImporter::Implementation::importDeclContextOf(
  const clang::Decl *decl,
  EffectiveClangContext context)
{
  DeclContext *importedDC = nullptr;
  switch (context.getKind()) {
  case EffectiveClangContext::DeclContext: {
    auto dc = context.getAsDeclContext();
    if (dc->isTranslationUnit()) {
      if (auto *module = getClangModuleForDecl(decl))
        return module;
      else
        return nullptr;
    }

    // Import the DeclContext.
    importedDC = importDeclContextImpl(dc);
    break;
  }

  case EffectiveClangContext::TypedefContext: {
    // Import the typedef-name as a declaration.
    auto importedDecl = importDecl(context.getTypedefName(), false);
    if (!importedDecl) return nullptr;

    importedDC = dyn_cast_or_null<DeclContext>(importedDecl);
    break;
  }

  case EffectiveClangContext::UnresolvedContext: {
    // FIXME: Resolve through name lookup. This is brittle.
    auto submodule =
      getClangSubmoduleForDecl(decl, /*allowForwardDeclaration=*/false);
    if (!submodule) return nullptr;

    if (auto lookupTable = findLookupTable(*submodule)) {
      if (auto clangDecl
            = lookupTable->resolveContext(context.getUnresolvedName())) {
        // Import the Clang declaration.
        auto decl = importDecl(clangDecl, false);
        if (!decl) return nullptr;

        // Look through typealiases.
        if (auto typealias = dyn_cast<TypeAliasDecl>(decl))
          importedDC = typealias->getDeclaredInterfaceType()->getAnyNominal();
        else // Map to a nominal type declaration.
          importedDC = dyn_cast<NominalTypeDecl>(decl);
        break;
      }
    }
  }
  }

  // If we didn't manage to import the declaration context, we're done.
  if (!importedDC) return nullptr;

  // If the declaration was not global to start with, we're done.
  bool isGlobal =
    decl->getDeclContext()->getRedeclContext()->isTranslationUnit() &&
    !isa<clang::EnumConstantDecl>(decl);
  if (!isGlobal) return importedDC;

  // If the resulting declaration context is not a nominal type,
  // we're done.
  auto nominal = dyn_cast<NominalTypeDecl>(importedDC);
  if (!nominal) return importedDC;

  // Look for the extension for the given nominal type within the
  // Clang submodule of the declaration.
  const clang::Module *declSubmodule = *getClangSubmoduleForDecl(decl);
  auto extensionKey = std::make_pair(nominal, declSubmodule);
  auto knownExtension = extensionPoints.find(extensionKey);
  if (knownExtension != extensionPoints.end())
    return knownExtension->second;

  // Create a new extension for this nominal type/Clang submodule pair.
  auto swiftTyLoc = TypeLoc::withoutLoc(nominal->getDeclaredType());
  auto ext = ExtensionDecl::create(SwiftContext, SourceLoc(), swiftTyLoc, {},
                                   getClangModuleForDecl(decl), nullptr);
  ext->setValidated();
  ext->setCheckedInheritanceClause();
  ext->setMemberLoader(this, reinterpret_cast<uintptr_t>(declSubmodule));

  if (auto protoDecl = ext->getAsProtocolExtensionContext()) {
    ext->setGenericParams(protoDecl->createGenericParams(ext));
    auto protoArchetype = protoDecl->getProtocolSelf()->getArchetype();
    auto extSelf = ext->getProtocolSelf();

    SmallVector<ProtocolDecl *, 4> conformsTo(
        protoArchetype->getConformsTo().begin(),
        protoArchetype->getConformsTo().end());
    ArchetypeType *extSelfArchetype = ArchetypeType::getNew(
        SwiftContext, protoArchetype->getParent(),
        protoArchetype->getAssocTypeOrProtocol(), protoArchetype->getName(),
        conformsTo, protoArchetype->getSuperclass(),
        protoArchetype->getIsRecursive());
    extSelf->setArchetype(extSelfArchetype);
    ext->getGenericParams()->setAllArchetypes(
        SwiftContext.AllocateCopy(llvm::makeArrayRef(extSelfArchetype)));

    auto genericParam =
        extSelf->getDeclaredType()->castTo<GenericTypeParamType>();
    Requirement genericRequirements[2] = {
        Requirement(RequirementKind::WitnessMarker, genericParam, Type()),
        Requirement(RequirementKind::Conformance, genericParam,
                    protoDecl->getDeclaredType())};
    auto sig = GenericSignature::get(genericParam, genericRequirements);
    ext->setGenericSignature(sig);
  }

  // Add the extension to the nominal type.
  nominal->addExtension(ext);

  // Record this extension so we can find it later.
  extensionPoints[extensionKey] = ext;
  return ext;
}

ValueDecl *
ClangImporter::Implementation::createConstant(Identifier name, DeclContext *dc,
                                              Type type,
                                              const clang::APValue &value,
                                              ConstantConvertKind convertKind,
                                              bool isStatic,
                                              ClangNode ClangN) {
  auto &context = SwiftContext;

  // Create the integer literal value.
  Expr *expr = nullptr;
  switch (value.getKind()) {
  case clang::APValue::AddrLabelDiff:
  case clang::APValue::Array:
  case clang::APValue::ComplexFloat:
  case clang::APValue::ComplexInt:
  case clang::APValue::LValue:
  case clang::APValue::MemberPointer:
  case clang::APValue::Struct:
  case clang::APValue::Uninitialized:
  case clang::APValue::Union:
  case clang::APValue::Vector:
    llvm_unreachable("Unhandled APValue kind");

  case clang::APValue::Float:
  case clang::APValue::Int: {
    // Print the value.
    llvm::SmallString<16> printedValueBuf;
    if (value.getKind() == clang::APValue::Int) {
      value.getInt().toString(printedValueBuf);
    } else {
      assert(value.getFloat().isFinite() && "can't handle infinities or NaNs");
      value.getFloat().toString(printedValueBuf);
    }
    StringRef printedValue = printedValueBuf.str();

    // If this was a negative number, record that and strip off the '-'.
    bool isNegative = printedValue.front() == '-';
    if (isNegative)
      printedValue = printedValue.drop_front();

    // Create the expression node.
    StringRef printedValueCopy(context.AllocateCopy(printedValue));
    if (value.getKind() == clang::APValue::Int) {
      expr = new (context) IntegerLiteralExpr(printedValueCopy, SourceLoc(),
                                              /*Implicit=*/true);
    } else {
      expr = new (context) FloatLiteralExpr(printedValueCopy, SourceLoc(),
                                            /*Implicit=*/true);
    }

    if (isNegative)
      cast<NumberLiteralExpr>(expr)->setNegative(SourceLoc());

    break;
  }
  }

  assert(expr);
  return createConstant(name, dc, type, expr, convertKind, isStatic, ClangN);
}


ValueDecl *
ClangImporter::Implementation::createConstant(Identifier name, DeclContext *dc,
                                              Type type, StringRef value,
                                              ConstantConvertKind convertKind,
                                              bool isStatic,
                                              ClangNode ClangN) {
  auto expr = new (SwiftContext) StringLiteralExpr(value, SourceRange());
  return createConstant(name, dc, type, expr, convertKind, isStatic, ClangN);
}


ValueDecl *
ClangImporter::Implementation::createConstant(Identifier name, DeclContext *dc,
                                              Type type, Expr *valueExpr,
                                              ConstantConvertKind convertKind,
                                              bool isStatic,
                                              ClangNode ClangN) {
  auto &C = SwiftContext;

  VarDecl *var = nullptr;
  if (ClangN) {
    var = createDeclWithClangNode<VarDecl>(ClangN, Accessibility::Public,
                                           isStatic, /*IsLet*/ false,
                                           SourceLoc(), name, type, dc);
  } else {
    var = new (SwiftContext)
        VarDecl(isStatic, /*IsLet*/ false, SourceLoc(), name, type, dc);
  }

  // Form the argument patterns.
  SmallVector<ParameterList*, 3> getterArgs;
  
  // 'self'
  if (dc->isTypeContext()) {
    auto *selfDecl = ParamDecl::createSelf(SourceLoc(), dc, isStatic);
    getterArgs.push_back(ParameterList::createWithoutLoc(selfDecl));
  }
  
  // empty tuple
  getterArgs.push_back(ParameterList::createEmpty(C));

  // Form the type of the getter.
  auto getterType = ParameterList::getFullInterfaceType(type, getterArgs, dc);

  // Create the getter function declaration.
  auto func =
    FuncDecl::create(C, /*StaticLoc=*/SourceLoc(), StaticSpellingKind::None,
                     /*FuncLoc=*/SourceLoc(),
                     /*Name=*/Identifier(), /*NameLoc=*/SourceLoc(),
                     /*Throws=*/false, /*ThrowsLoc=*/SourceLoc(),
                     /*AccessorKeywordLoc=*/SourceLoc(),
                     /*GenericParams=*/nullptr, getterArgs,
                     getterType, TypeLoc::withoutLoc(type), dc);
  func->setStatic(isStatic);
  func->setInterfaceType(getterType);
  func->setBodyResultType(type);
  func->setAccessibility(getOverridableAccessibility(dc));

  // If we're not done type checking, build the getter body.
  if (!hasFinishedTypeChecking()) {
    auto expr = valueExpr;

    // If we need a conversion, add one now.
    switch (convertKind) {
    case ConstantConvertKind::None:
      break;

    case ConstantConvertKind::Construction:
    case ConstantConvertKind::ConstructionWithUnwrap: {
      auto typeRef = TypeExpr::createImplicit(type, C);
      
      expr = CallExpr::createImplicit(C, typeRef, { expr }, { C.Id_rawValue });
      if (convertKind == ConstantConvertKind::ConstructionWithUnwrap)
        expr = new (C) ForceValueExpr(expr, SourceLoc());
      break;
    }

    case ConstantConvertKind::Coerce:
      break;

    case ConstantConvertKind::Downcast: {
      expr = new (C) ForcedCheckedCastExpr(expr, SourceLoc(), SourceLoc(),
                                           TypeLoc::withoutLoc(type));
      expr->setImplicit();
      break;
    }
    }

    // Create the return statement.
    auto ret = new (C) ReturnStmt(SourceLoc(), expr);

    // Finally, set the body.
    func->setBody(BraceStmt::create(C, SourceLoc(),
                                    ASTNode(ret),
                                    SourceLoc()));
  }

  // Mark the function transparent so that we inline it away completely.
  func->getAttrs().add(new (C) TransparentAttr(/*implicit*/ true));
  
  // Set the function up as the getter.
  var->makeComputed(SourceLoc(), func, nullptr, nullptr, SourceLoc());

  // Register this thunk as an external definition.
  registerExternalDecl(func);

  return var;
}

/// \brief Create a decl with error type and an "unavailable" attribute on it
/// with the specified message.
void ClangImporter::Implementation::
markUnavailable(ValueDecl *decl, StringRef unavailabilityMsgRef) {

  unavailabilityMsgRef = SwiftContext.AllocateCopy(unavailabilityMsgRef);
  auto ua = AvailableAttr::createUnconditional(SwiftContext,
                                                  unavailabilityMsgRef);
  decl->getAttrs().add(ua);
}

/// \brief Create a decl with error type and an "unavailable" attribute on it
/// with the specified message.
ValueDecl *ClangImporter::Implementation::
createUnavailableDecl(Identifier name, DeclContext *dc, Type type,
                      StringRef UnavailableMessage, bool isStatic,
                      ClangNode ClangN) {

  // Create a new VarDecl with dummy type.
  auto var = createDeclWithClangNode<VarDecl>(ClangN, Accessibility::Public,
                                              isStatic, /*IsLet*/ false,
                                              SourceLoc(), name, type, dc);
  markUnavailable(var, UnavailableMessage);

  return var;
}


void
ClangImporter::Implementation::loadAllMembers(Decl *D, uint64_t extra) {
  assert(D);

  // Check whether we're importing an Objective-C container of some sort.
  auto objcContainer =
    dyn_cast_or_null<clang::ObjCContainerDecl>(D->getClangDecl());

  // If not, we're importing globals-as-members into an extension.
  if (!objcContainer) {
    // We have extension.
    auto ext = cast<ExtensionDecl>(D);
    auto nominal = ext->getExtendedType()->getAnyNominal();

    // The submodule of the extension is encoded in the extra data.
    clang::Module *submodule = reinterpret_cast<clang::Module *>(
                                 static_cast<uintptr_t>(extra));

    // Find the lookup table.
    auto topLevelModule = submodule;
    if (topLevelModule)
      topLevelModule = topLevelModule->getTopLevelModule();
    auto table = findLookupTable(topLevelModule);
    if (!table) return;

    // Dig out the effective Clang context for this nominal type.
    auto effectiveClangContext = getEffectiveClangContext(nominal);
    if (!effectiveClangContext) return;

    // Get ready to actually load the members.
    ImportingEntityRAII Importing(*this);

    // Load the members.
    for (auto entry : table->lookupGlobalsAsMembers(effectiveClangContext)) {
      auto decl = entry.get<clang::NamedDecl *>();

      // Only continue members in the same submodule as this extension.
      if (decl->getImportedOwningModule() != submodule) continue;

      // Import the member.
      auto member = importDecl(decl, false);
      if (!member) continue;

      // Add the member.
      ext->addMember(member);

      if (auto alternate = getAlternateDecl(member)) {
        ext->addMember(alternate);
      }

      // Import the Swift 2 stub declaration.
      if (auto swift2Member = importDecl(decl, true))
        if (swift2Member->getDeclContext() == ext)
          ext->addMember(swift2Member);
    }

    return;
  }


  clang::PrettyStackTraceDecl trace(objcContainer, clang::SourceLocation(),
                                    Instance->getSourceManager(),
                                    "loading members for");

  SwiftDeclConverter converter(*this, /*useSwift2Name=*/false);
  SwiftDeclConverter swift2Converter(*this, /*useSwift2Name=*/true);

  DeclContext *DC;
  IterableDeclContext *IDC;
  SmallVector<ProtocolDecl *, 4> protos;

  // Figure out the declaration context we're importing into.
  if (auto nominal = dyn_cast<NominalTypeDecl>(D)) {
    DC = nominal;
    IDC = nominal;
  } else {
    auto ext = cast<ExtensionDecl>(D);
    DC = ext;
    IDC = ext;
  }

  ImportingEntityRAII Importing(*this);

  SmallVector<Decl *, 16> members;
  llvm::SmallPtrSet<Decl *, 4> knownMembers;
  converter.importObjCMembers(objcContainer, DC, knownMembers, members);
  swift2Converter.importObjCMembers(objcContainer, DC, knownMembers, members);

  protos = takeImportedProtocols(D);
  if (auto clangClass = dyn_cast<clang::ObjCInterfaceDecl>(objcContainer)) {
    auto swiftClass = cast<ClassDecl>(D);
    objcContainer = clangClass = clangClass->getDefinition();

    // Imported inherited initializers.
    if (clangClass->getName() != "Protocol") {
      converter.importInheritedConstructors(const_cast<ClassDecl *>(swiftClass),
                                            members);
    }

  } else if (auto clangProto
               = dyn_cast<clang::ObjCProtocolDecl>(objcContainer)) {
    objcContainer = clangProto->getDefinition();
  }

  // Import mirrored declarations for protocols to which this category
  // or extension conforms.
  // FIXME: This is supposed to be a short-term hack.
  converter.importMirroredProtocolMembers(objcContainer, DC,
                                          protos, members, SwiftContext);

  // Add the members now, before ~ImportingEntityRAII does work that might
  // involve them.
  for (auto member : members) {
    IDC->addMember(member);
  }

}

void ClangImporter::Implementation::loadAllConformances(
       const Decl *D, uint64_t contextData,
       SmallVectorImpl<ProtocolConformance *> &Conformances) {
  Conformances = takeDelayedConformance(contextData);
}

Optional<MappedTypeNameKind>
ClangImporter::Implementation::getSpecialTypedefKind(clang::TypedefNameDecl *decl) {
  auto iter = SpecialTypedefNames.find(decl->getCanonicalDecl());
  if (iter == SpecialTypedefNames.end())
    return None;
  return iter->second;
}

Identifier
ClangImporter::getEnumConstantName(const clang::EnumConstantDecl *enumConstant){
  return Impl.importFullName(enumConstant).Imported.getBaseName();
}

clang::QualType ClangImporter::Implementation::getAccessorPropertyType(
                  const clang::FunctionDecl *accessor,
                  bool isSetter,
                  Optional<unsigned> selfIndex) {
  // Simple case: the property type of the getter is in the return
  // type.
  if (!isSetter) return accessor->getReturnType();

  // For the setter, first check that we have the right number of
  // parameters.
  unsigned numExpectedParams = selfIndex ? 2 : 1;
  if (accessor->getNumParams() != numExpectedParams)
    return clang::QualType();

  // Dig out the parameter for the value.
  unsigned valueIdx = selfIndex ? (1 - *selfIndex) : 0;
  auto param = accessor->getParamDecl(valueIdx);
  return param->getType();
}
