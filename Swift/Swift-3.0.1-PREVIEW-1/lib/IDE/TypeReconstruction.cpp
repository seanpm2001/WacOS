//===--- TypeReconstruction.cpp -------------------------------------------===//
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

#include "swift/IDE/Utils.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Mangle.h"
#include "swift/AST/NameLookup.h"
#include "swift/Basic/Demangle.h"
#include "swift/ClangImporter/ClangImporter.h"
#include "swift/SIL/SILModule.h"
#include "swift/Strings.h"

#include <cstdio>
#include <cstdarg>
#include <mutex> // std::once

using namespace swift;

// FIXME: replace with std::string and StringRef as appropriate to each case.
typedef const std::string ConstString;

static std::string stringWithFormat(const std::string fmt_str, ...) {
  int final_n, n = ((int)fmt_str.size()) * 2;
  std::string str;
  std::unique_ptr<char[]> formatted;
  va_list ap;
  while (1) {
    formatted.reset(new char[n]);
    strcpy(&formatted[0], fmt_str.c_str());
    va_start(ap, fmt_str);
    final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
    va_end(ap);
    if (final_n < 0 || final_n >= n)
      n += abs(final_n - n + 1);
    else
      break;
  }
  return std::string(formatted.get());
}

static TypeBase *GetTemplateArgument(TypeBase *type, size_t arg_idx) {
  if (type) {
    CanType swift_can_type = type->getDesugaredType()->getCanonicalType();

    const TypeKind type_kind = swift_can_type->getKind();
    switch (type_kind) {
    case TypeKind::UnboundGeneric: {
      UnboundGenericType *unbound_generic_type =
          swift_can_type->getAs<UnboundGenericType>();
      if (!unbound_generic_type)
        break;
      auto *nominal_type_decl = unbound_generic_type->getDecl();
      if (!nominal_type_decl)
        break;
      GenericParamList *generic_param_list =
          nominal_type_decl->getGenericParams();
      if (!generic_param_list)
        break;
      if (arg_idx >= generic_param_list->getAllArchetypes().size())
        break;
      return generic_param_list->getAllArchetypes()[arg_idx];
    } break;
    case TypeKind::BoundGenericClass:
    case TypeKind::BoundGenericStruct:
    case TypeKind::BoundGenericEnum: {
      BoundGenericType *bound_generic_type =
          swift_can_type->getAs<BoundGenericType>();
      if (!bound_generic_type)
        break;
      const ArrayRef<Substitution> &substitutions =
          bound_generic_type->gatherAllSubstitutions(nullptr, nullptr);
      if (arg_idx >= substitutions.size())
        break;
      const Substitution &substitution = substitutions[arg_idx];
      return substitution.getReplacement().getPointer();
    }
    case TypeKind::PolymorphicFunction: {
      PolymorphicFunctionType *polymorphic_func_type =
          swift_can_type->getAs<PolymorphicFunctionType>();
      if (!polymorphic_func_type)
        break;
      if (arg_idx >= polymorphic_func_type->getGenericParameters().size())
        break;
      return polymorphic_func_type->getGenericParameters()[arg_idx]
          ->getArchetype();
    } break;
    default:
      break;
    }
  }

  return nullptr;
}

enum class MemberType : uint32_t { Invalid, BaseClass, Field };

struct MemberInfo {
  Type clang_type;
  const std::string name;
  uint64_t byte_size;
  uint32_t byte_offset;
  MemberType member_type;
  bool is_fragile;

  MemberInfo(MemberType member_type)
      : clang_type(), name(), byte_size(0), byte_offset(0),
        member_type(member_type), is_fragile(false) {}
};

struct EnumElementInfo {
  Type clang_type;
  ConstString name;
  uint64_t byte_size;
  uint32_t value;       // The value for this enumeration element
  uint32_t extra_value; // If not UINT32_MAX, then this value is an extra value
  // that appears at offset 0 to tell one or more empty
  // enums apart. This value will only be filled in if there
  // are one or more enum elements that have a non-zero byte size

  EnumElementInfo()
      : clang_type(), name(), byte_size(0), extra_value(UINT32_MAX) {}
};

class DeclsLookupSource {
public:
  typedef SmallVectorImpl<ValueDecl *> ValueDecls;

private:
  class VisibleDeclsConsumer : public VisibleDeclConsumer {
  private:
    std::vector<ValueDecl *> m_decls;

  public:
    virtual void foundDecl(ValueDecl *VD, DeclVisibilityKind Reason) {
      m_decls.push_back(VD);
    }
    virtual ~VisibleDeclsConsumer() = default;
    explicit operator bool() { return m_decls.size() > 0; }

    decltype(m_decls)::const_iterator begin() { return m_decls.begin(); }

    decltype(m_decls)::const_iterator end() { return m_decls.end(); }
  };

  bool lookupQualified(ModuleDecl *entry, Identifier name, NLOptions options,
                       LazyResolver *typeResolver, ValueDecls &decls) {
    if (!entry)
      return false;
    size_t decls_size = decls.size();
    entry->lookupQualified(ModuleType::get(entry), name, options, typeResolver,
                           decls);
    return decls.size() > decls_size;
  }

  bool lookupValue(ModuleDecl *entry, Identifier name,
                   Module::AccessPathTy accessPath, NLKind lookupKind,
                   ValueDecls &decls) {
    if (!entry)
      return false;
    size_t decls_size = decls.size();
    entry->lookupValue(accessPath, name, lookupKind, decls);
    return decls.size() > decls_size;
  }

public:
  enum class LookupKind { SwiftModule, Crawler, Decl, Extension, Invalid };

  typedef Optional<std::string> PrivateDeclIdentifier;

  static DeclsLookupSource GetDeclsLookupSource(ASTContext &ast,
                                                ConstString module_name,
                                                bool allow_crawler = true) {
    assert(!module_name.empty());
    static ConstString g_ObjectiveCModule(MANGLING_MODULE_OBJC);
    static ConstString g_BuiltinModule("Builtin");
    static ConstString g_CModule(MANGLING_MODULE_C);
    if (allow_crawler) {
      if (module_name == g_ObjectiveCModule || module_name == g_CModule)
        return DeclsLookupSource(&ast, module_name);
    }

    ModuleDecl *module = module_name == g_BuiltinModule
                             ? ast.TheBuiltinModule
                             : ast.getModuleByName(module_name);
    if (module == nullptr)
      return DeclsLookupSource();
    return DeclsLookupSource(module);
  }

  static DeclsLookupSource GetDeclsLookupSource(NominalTypeDecl *decl) {
    assert(decl);
    return DeclsLookupSource(decl);
  }

  static DeclsLookupSource GetDeclsLookupSource(DeclsLookupSource source,
                                                NominalTypeDecl *decl) {
    assert(source._type == LookupKind::SwiftModule);
    assert(source._module);
    assert(decl);
    return DeclsLookupSource(source._module, decl);
  }

  void lookupQualified(Identifier name, NLOptions options,
                       LazyResolver *typeResolver, ValueDecls &result) {
    if (_type == LookupKind::Crawler) {
      ASTContext *ast_ctx = _crawler._ast;
      if (ast_ctx) {
        VisibleDeclsConsumer consumer;
        ClangImporter *swift_clang_importer =
            (ClangImporter *)ast_ctx->getClangModuleLoader();
        if (!swift_clang_importer)
          return;
        swift_clang_importer->lookupValue(name, consumer);
        if (consumer) {
          auto iter = consumer.begin(), end = consumer.end();
          while (iter != end) {
            result.push_back(*iter);
            iter++;
          }
          return;
        }
      }
    } else if (_type == LookupKind::SwiftModule)
      lookupQualified(_module, name, options, typeResolver, result);
    return;
  }

  void lookupValue(Module::AccessPathTy path, Identifier name, NLKind kind,
                   ValueDecls &result) {
    if (_type == LookupKind::Crawler) {
      ASTContext *ast_ctx = _crawler._ast;
      if (ast_ctx) {
        VisibleDeclsConsumer consumer;
        ClangImporter *swift_clang_importer =
            (ClangImporter *)ast_ctx->getClangModuleLoader();
        if (!swift_clang_importer)
          return;
        swift_clang_importer->lookupValue(name, consumer);
        if (consumer) {
          auto iter = consumer.begin(), end = consumer.end();
          while (iter != end) {
            result.push_back(*iter);
            iter++;
          }
          return;
        }
      }
    } else if (_type == LookupKind::SwiftModule)
      _module->lookupValue(path, name, kind, result);
    return;
  }

  void lookupMember(DeclName id, Identifier priv_decl_id, ValueDecls &result) {
    if (_type == LookupKind::Decl)
      return lookupMember(_decl, id, priv_decl_id, result);
    if (_type == LookupKind::SwiftModule)
      return lookupMember(_module, id, priv_decl_id, result);
    if (_type == LookupKind::Extension)
      return lookupMember(_extension._decl, id, priv_decl_id, result);
    return;
  }

  void lookupMember(DeclContext *decl_ctx, DeclName id, Identifier priv_decl_id,
                    ValueDecls &result) {
    if (_type == LookupKind::Decl)
      _decl->getModuleContext()->lookupMember(result, decl_ctx, id,
                                              priv_decl_id);
    else if (_type == LookupKind::SwiftModule)
      _module->lookupMember(result, decl_ctx, id, priv_decl_id);
    else if (_type == LookupKind::Extension)
      _extension._module->lookupMember(result, decl_ctx, id, priv_decl_id);
    return;
  }

  TypeDecl *lookupLocalType(StringRef key) {
    switch (_type) {
    case LookupKind::SwiftModule:
      return _module->lookupLocalType(key);
    case LookupKind::Decl:
      return _decl->getModuleContext()->lookupLocalType(key);
    case LookupKind::Extension:
      return _extension._module->lookupLocalType(key);
    case LookupKind::Invalid:
      return nullptr;
    case LookupKind::Crawler:
      return nullptr;
    }
  }

  ConstString GetName() const {
    switch (_type) {
    case LookupKind::Invalid:
      return ConstString("Invalid");
    case LookupKind::Crawler:
      return ConstString("Crawler");
    case LookupKind::SwiftModule:
      return ConstString(_module->getName().get());
    case LookupKind::Decl:
      return ConstString(_decl->getName().get());
    case LookupKind::Extension:
      SmallString<64> builder;
      builder.append("ext ");
      builder.append(_extension._decl->getNameStr());
      builder.append(" in ");
      builder.append(_module->getNameStr());
      return builder.str();
    }
  }

  ~DeclsLookupSource() {}

  DeclsLookupSource(const DeclsLookupSource &rhs) : _type(rhs._type) {
    switch (_type) {
    case LookupKind::Invalid:
      break;
    case LookupKind::Crawler:
      _crawler._ast = rhs._crawler._ast;
      break;
    case LookupKind::SwiftModule:
      _module = rhs._module;
      break;
    case LookupKind::Decl:
      _decl = rhs._decl;
      _extension._decl = rhs._extension._decl;
      _extension._module = rhs._extension._module;
      break;
    case LookupKind::Extension:
      _extension._decl = rhs._extension._decl;
      _extension._module = rhs._extension._module;
      break;
    }
  }

  DeclsLookupSource &operator=(const DeclsLookupSource &rhs) {
    if (this != &rhs) {
      _type = rhs._type;
      switch (_type) {
      case LookupKind::Invalid:
        break;
      case LookupKind::Crawler:
        _crawler._ast = rhs._crawler._ast;
        break;
      case LookupKind::SwiftModule:
        _module = rhs._module;
        break;
      case LookupKind::Decl:
        _decl = rhs._decl;
        _extension._decl = rhs._extension._decl;
        _extension._module = rhs._extension._module;
        break;
      case LookupKind::Extension:
        _extension._decl = rhs._extension._decl;
        _extension._module = rhs._extension._module;
        break;
      }
    }
    return *this;
  }

  void Clear() {
    // no need to explicitly clean either pointer
    _type = LookupKind::Invalid;
  }

  DeclsLookupSource() : _type(LookupKind::Invalid), _module(nullptr) {}

  operator bool() {
    switch (_type) {
    case LookupKind::Invalid:
      return false;
    case LookupKind::Crawler:
      return _crawler._ast != nullptr;
    case LookupKind::SwiftModule:
      return _module != nullptr;
    case LookupKind::Decl:
      return _decl != nullptr;
    case LookupKind::Extension:
      return (_extension._decl != nullptr) && (_extension._module != nullptr);
    }
  }

  bool IsExtension() {
    return (this->operator bool()) && (_type == LookupKind::Extension);
  }

  Type GetQualifiedArchetype(size_t index, ASTContext *ast) {
    if (this->operator bool() && ast) {
      switch (_type) {
      case LookupKind::Extension: {
        TypeBase *type_ptr = _extension._decl->getType().getPointer();
        if (MetatypeType *metatype_ptr = type_ptr->getAs<MetatypeType>())
          type_ptr = metatype_ptr->getInstanceType().getPointer();
        TypeBase *archetype = GetTemplateArgument(type_ptr, index);
        return Type(archetype);
      } break;
      default:
        break;
      }
    }
    return Type();
  }

private:
  LookupKind _type;

  union {
    ModuleDecl *_module;
    struct {
      ASTContext *_ast;
    } _crawler;
    NominalTypeDecl *_decl;
    struct {
      ModuleDecl *_module;    // extension in this module
      NominalTypeDecl *_decl; // for this type
    } _extension;
  };

  DeclsLookupSource(ModuleDecl *_m) {
    if (_m) {
      _module = _m;
      _type = LookupKind::SwiftModule;
    } else
      _type = LookupKind::Invalid;
  }

  DeclsLookupSource(ASTContext *_a, ConstString _m) {
    // it is fine for the ASTContext to be null, so don't actually even
    // lldbassert there
    if (_a) {
      _crawler._ast = _a;
      _type = LookupKind::Crawler;
    } else
      _type = LookupKind::Invalid;
  }

  DeclsLookupSource(NominalTypeDecl *_d) {
    if (_d) {
      _decl = _d;
      _type = LookupKind::Decl;
    } else
      _type = LookupKind::Invalid;
  }

  DeclsLookupSource(ModuleDecl *_m, NominalTypeDecl *_d) {
    if (_m && _d) {
      _extension._decl = _d;
      _extension._module = _m;
      _type = LookupKind::Extension;
    } else
      _type = LookupKind::Invalid;
  }
};

struct VisitNodeResult {
  DeclsLookupSource _module;
  std::vector<Decl *> _decls;
  std::vector<Type> _types;
  TupleTypeElt _tuple_type_element;
  std::string _error;
  VisitNodeResult()
      : _module(), _decls(), _types(), _tuple_type_element(), _error() {}

  bool HasSingleType() { return _types.size() == 1 && _types.front(); }

  Type GetFirstType() {
    // Must ensure there is a type prior to calling this
    return _types.front();
  }

  void Clear() {
    _module.Clear();
    _decls.clear();
    _types.clear();
    _tuple_type_element = TupleTypeElt();
    _error.clear();
  }
};

static Identifier
GetIdentifier(ASTContext *ast,
              const DeclsLookupSource::PrivateDeclIdentifier &priv_decl_id) {
  do {
    if (!ast)
      break;
    if (!priv_decl_id.hasValue())
      break;
    return ast->getIdentifier(priv_decl_id.getValue().c_str());
  } while (false);
  return Identifier();
}

static bool FindFirstNamedDeclWithKind(
    ASTContext *ast, const StringRef &name, DeclKind decl_kind,
    VisitNodeResult &result,
    DeclsLookupSource::PrivateDeclIdentifier priv_decl_id =
        DeclsLookupSource::PrivateDeclIdentifier())

{
  if (!result._decls.empty()) {
    Decl *parent_decl = result._decls.back();
    if (parent_decl) {
      auto nominal_decl = dyn_cast<NominalTypeDecl>(parent_decl);

      if (nominal_decl) {
        bool check_type_aliases = false;

        DeclsLookupSource lookup(
            DeclsLookupSource::GetDeclsLookupSource(nominal_decl));
        SmallVector<ValueDecl *, 4> decls;
        lookup.lookupMember(ast->getIdentifier(name),
                            GetIdentifier(ast, priv_decl_id), decls);

        for (auto decl : decls) {
          const DeclKind curr_decl_kind = decl->getKind();

          if (curr_decl_kind == decl_kind) {
            result._decls.back() = decl;
            Type decl_type;
            if (decl->hasType()) {
              decl_type = decl->getType();
              MetatypeType *meta_type = decl_type->getAs<MetatypeType>();
              if (meta_type)
                decl_type = meta_type->getInstanceType();
            }
            if (result._types.empty())
              result._types.push_back(decl_type);
            else
              result._types.back() = decl_type;
            return true;
          } else if (curr_decl_kind == DeclKind::TypeAlias)
            check_type_aliases = true;
        }

        if (check_type_aliases) {
          for (auto decl : decls) {
            const DeclKind curr_decl_kind = decl->getKind();

            if (curr_decl_kind == DeclKind::TypeAlias) {
              result._decls.back() = decl;
              Type decl_type;
              if (decl->hasType()) {
                decl_type = decl->getType();
                MetatypeType *meta_type = decl_type->getAs<MetatypeType>();
                if (meta_type)
                  decl_type = meta_type->getInstanceType();
              }
              if (result._types.empty())
                result._types.push_back(decl_type);
              else
                result._types.back() = decl_type;
              return true;
            }
          }
        }
      }
    }
  } else if (result._module) {
    Module::AccessPathTy access_path;
    Identifier name_ident(ast->getIdentifier(name));
    SmallVector<ValueDecl *, 4> decls;
    if (priv_decl_id)
      result._module.lookupMember(
          name_ident, ast->getIdentifier(priv_decl_id.getValue().c_str()),
          decls);
    else
      result._module.lookupQualified(name_ident, NLOptions(), NULL, decls);
    if (!decls.empty()) {
      bool check_type_aliases = false;
      // Look for an exact match first
      for (auto decl : decls) {
        const DeclKind curr_decl_kind = decl->getKind();
        if (curr_decl_kind == decl_kind) {
          result._decls.assign(1, decl);
          if (decl->hasType()) {
            result._types.assign(1, decl->getType());
            MetatypeType *meta_type =
                result._types.back()->getAs<MetatypeType>();
            if (meta_type)
              result._types.back() = meta_type->getInstanceType();
          } else {
            result._types.assign(1, Type());
          }
          return true;
        } else if (curr_decl_kind == DeclKind::TypeAlias)
          check_type_aliases = true;
      }
      // If we didn't find any exact matches, accept any type aliases
      if (check_type_aliases) {
        for (auto decl : decls) {
          if (decl->getKind() == DeclKind::TypeAlias) {
            result._decls.assign(1, decl);
            if (decl->hasType()) {
              result._types.assign(1, decl->getType());
              MetatypeType *meta_type =
                  result._types.back()->getAs<MetatypeType>();
              if (meta_type)
                result._types.back() = meta_type->getInstanceType();
            } else {
              result._types.assign(1, Type());
            }
            return true;
          }
        }
      }
    }
  }
  result.Clear();
  result._error = "Generic Error";
  return false;
}

static size_t
FindNamedDecls(ASTContext *ast, const StringRef &name, VisitNodeResult &result,
               DeclsLookupSource::PrivateDeclIdentifier priv_decl_id =
                   DeclsLookupSource::PrivateDeclIdentifier()) {
  if (!result._decls.empty()) {
    Decl *parent_decl = result._decls.back();
    result._decls.clear();
    result._types.clear();
    if (parent_decl) {

      if (auto nominal_decl = dyn_cast<NominalTypeDecl>(parent_decl)) {
        DeclsLookupSource lookup(
            DeclsLookupSource::GetDeclsLookupSource(nominal_decl));
        SmallVector<ValueDecl *, 4> decls;
        lookup.lookupMember(ast->getIdentifier(name),
                            GetIdentifier(ast, priv_decl_id), decls);
        if (decls.empty()) {
          result._error = stringWithFormat(
              "no decl found in '%s' (DeclKind=%u)", name.str().c_str(),
              nominal_decl->getName().get(), (uint32_t)nominal_decl->getKind());
        } else {
          for (ValueDecl *decl : decls) {
            if (decl->hasType()) {
              result._decls.push_back(decl);
              Type decl_type;
              if (decl->hasType()) {
                decl_type = decl->getType();
                MetatypeType *meta_type = decl_type->getAs<MetatypeType>();
                if (meta_type)
                  decl_type = meta_type->getInstanceType();
              }
              result._types.push_back(decl_type);
            }
          }
          return result._types.size();
        }

      } else if (auto FD = dyn_cast<AbstractFunctionDecl>(parent_decl)) {

        // Do a local lookup into the function, using the end loc to approximate
        // being able to see all the local variables.
        // FIXME: Need a more complete/robust lookup mechanism that can handle
        // declarations in sub-stmts, etc.
        UnqualifiedLookup lookup(ast->getIdentifier(name), FD,
                                 ast->getLazyResolver(),
                                 /*isKnownPrivate=*/false, FD->getEndLoc());
        if (!lookup.isSuccess()) {
          result._error = "no decl found in function";
        } else {
          for (auto decl : lookup.Results) {
            auto *VD = decl.getValueDecl();
            result._decls.push_back(VD);
            result._types.push_back(VD->getType());
          }
        }
        return result._decls.size();

      } else {
        result._error = stringWithFormat(
            "decl is not a nominal_decl (DeclKind=%u), lookup for '%s' failed",
            (uint32_t)parent_decl->getKind(), name.str().c_str());
      }
    }
  } else if (result._module) {
    Module::AccessPathTy access_path;
    SmallVector<ValueDecl *, 4> decls;
    if (priv_decl_id)
      result._module.lookupMember(
          ast->getIdentifier(name),
          ast->getIdentifier(priv_decl_id.getValue().c_str()), decls);
    else
      result._module.lookupValue(access_path, ast->getIdentifier(name),
                                 NLKind::QualifiedLookup, decls);
    if (decls.empty()) {
      result._error =
          stringWithFormat("no decl named '%s' found in module '%s'",
                           name.str().c_str(), result._module.GetName().data());
    } else {
      for (auto decl : decls) {
        if (decl->hasType()) {
          result._decls.push_back(decl);
          if (decl->hasType()) {
            result._types.push_back(decl->getType());
            MetatypeType *meta_type =
                result._types.back()->getAs<MetatypeType>();
            if (meta_type)
              result._types.back() = meta_type->getInstanceType();
          } else {
            result._types.push_back(Type());
          }
        }
      }
      return result._types.size();
    }
  }
  result.Clear();
  result._error = "Generic error.";
  return false;
}

static const char *
SwiftDemangleNodeKindToCString(const Demangle::Node::Kind node_kind) {
#define NODE(e)                                                                \
  case Demangle::Node::Kind::e:                                                \
    return #e;

  switch (node_kind) {
#include "swift/Basic/DemangleNodes.def"
  }
  return "Demangle::Node::Kind::???";
#undef NODE
}

static DeclKind GetKindAsDeclKind(Demangle::Node::Kind node_kind) {
  switch (node_kind) {
  case Demangle::Node::Kind::TypeAlias:
    return DeclKind::TypeAlias;
  case Demangle::Node::Kind::Structure:
    return DeclKind::Struct;
  case Demangle::Node::Kind::Class:
    return DeclKind::Class;
  case Demangle::Node::Kind::Allocator:
    return DeclKind::Constructor;
  case Demangle::Node::Kind::Function:
    return DeclKind::Func;
  case Demangle::Node::Kind::Enum:
    return DeclKind::Enum;
  case Demangle::Node::Kind::Protocol:
    return DeclKind::Protocol;
  default:
    llvm_unreachable("Missing alias");
    // FIXME: can we 'log' SwiftDemangleNodeKindToCString(node_kind))
  }
}

// This should be called with a function type & its associated Decl.  If the
// type is not a function type,
// then we just return the original type, but we don't check that the Decl is
// the associated one.
// It is assumed you will get that right in calling this.
// Returns a version of the input type with the ExtInfo AbstractCC set
// correctly.
// This allows CompilerType::IsSwiftMethod to work properly off the swift Type.
// FIXME: we don't currently distinguish between Method & Witness.  These types
// don't actually get used
// to make Calling Convention choices - originally we were leaving them all at
// Normal...  But if we ever
// need to set it for that purpose we will have to fix that here.
static TypeBase *FixCallingConv(Decl *in_decl, TypeBase *in_type) {
  if (!in_decl)
    return in_type;

  AnyFunctionType *func_type = dyn_cast<AnyFunctionType>(in_type);
  if (func_type) {
    DeclContext *decl_context = in_decl->getDeclContext();
    if (decl_context && decl_context->isTypeContext()) {
      // Add the ExtInfo:
      AnyFunctionType::ExtInfo new_info(
          func_type->getExtInfo().withSILRepresentation(
              SILFunctionTypeRepresentation::Method));
      return func_type->withExtInfo(new_info);
    }
  }
  return in_type;
}

static void
VisitNode(ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
          VisitNodeResult &result,
          const VisitNodeResult &genericContext); // set by GenericType case

static void VisitNodeAddressor(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  // Addressors are apparently SIL-level functions of the form () -> RawPointer
  // and they bear no connection to their original variable at the interface
  // level
  CanFunctionType swift_can_func_type =
      CanFunctionType::get(ast->TheEmptyTupleType, ast->TheRawPointerType);
  result._types.push_back(swift_can_func_type.getPointer());
}

static void VisitNodeArchetype(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  const StringRef &archetype_name(cur_node->getText());
  VisitNodeResult protocol_list;
  for (Demangle::Node::iterator pos = cur_node->begin(), end = cur_node->end();
       pos != end; ++pos) {
    const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
    switch (child_node_kind) {
    case Demangle::Node::Kind::ProtocolList:
      nodes.push_back(*pos);
      VisitNode(ast, nodes, protocol_list, generic_context);
      break;
    default:
      result._error =
          stringWithFormat("%s encountered in generics children",
                           SwiftDemangleNodeKindToCString(child_node_kind));
      break;
    }
  }

  SmallVector<Type, 1> conforms_to;
  if (protocol_list.HasSingleType())
    conforms_to.push_back(protocol_list.GetFirstType());

  if (ast) {
    result._types.push_back(ArchetypeType::getNew(
        *ast, nullptr, (AssociatedTypeDecl *)nullptr,
        ast->getIdentifier(archetype_name), conforms_to, Type()));
  } else {
    result._error = "invalid ASTContext";
  }
}

static void VisitNodeArchetypeRef(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  const StringRef &archetype_name(cur_node->getText());
  Type result_type;
  for (const Type &archetype : generic_context._types) {
    const ArchetypeType *cast_archetype =
        dyn_cast<ArchetypeType>(archetype.getPointer());

    if (cast_archetype &&
        !cast_archetype->getName().str().compare(archetype_name)) {
      result_type = archetype;
      break;
    }
  }

  if (result_type)
    result._types.push_back(result_type);
  else {
    if (ast) {
      result._types.push_back(ArchetypeType::getNew(
          *ast, nullptr, (AssociatedTypeDecl *)nullptr,
          ast->getIdentifier(archetype_name), ArrayRef<Type>(), Type()));
    } else {
      result._error = "invalid ASTContext";
    }
  }
}

static void VisitNodeAssociatedTypeRef(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  Demangle::NodePointer root = cur_node->getChild(0);
  Demangle::NodePointer ident = cur_node->getChild(1);
  if (!root || !ident)
    return;
  nodes.push_back(root);
  VisitNodeResult type_result;
  VisitNode(ast, nodes, type_result, generic_context);
  if (type_result._types.size() == 1) {
    TypeBase *type = type_result._types[0].getPointer();
    if (type) {
      ArchetypeType *archetype = type->getAs<ArchetypeType>();
      if (archetype) {
        Identifier identifier = ast->getIdentifier(ident->getText());
        if (archetype->hasNestedType(identifier)) {
          Type nested = archetype->getNestedTypeValue(identifier);
          if (nested) {
            result._types.push_back(nested);
            result._module = type_result._module;
            return;
          }
        }
      }
    }
  }
  result._types.clear();
  result._error = stringWithFormat(
      "unable to find associated type %s in context", ident->getText().c_str());
}

static void VisitNodeBoundGeneric(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  if (cur_node->begin() != cur_node->end()) {
    VisitNodeResult generic_type_result;
    VisitNodeResult template_types_result;

    Demangle::Node::iterator end = cur_node->end();
    for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
      const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
      switch (child_node_kind) {
      case Demangle::Node::Kind::Type:
      case Demangle::Node::Kind::Metatype:
        nodes.push_back(*pos);
        VisitNode(ast, nodes, generic_type_result, generic_context);
        break;
      case Demangle::Node::Kind::TypeList:
        nodes.push_back(*pos);
        VisitNode(ast, nodes, template_types_result, generic_context);
        break;
      default:
        break;
      }
    }

    if (generic_type_result._types.size() == 1 &&
        !template_types_result._types.empty()) {
      NominalTypeDecl *nominal_type_decl =
          dyn_cast<NominalTypeDecl>(generic_type_result._decls.front());
      DeclContext *parent_decl = nominal_type_decl->getParent();
      Type parent_type;
      if (parent_decl->isTypeContext())
        parent_type = parent_decl->getDeclaredTypeOfContext();
      result._types.push_back(Type(BoundGenericType::get(
          nominal_type_decl, parent_type, template_types_result._types)));
    }
  }
}

static void VisitNodeBuiltinTypeName(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  std::string builtin_name = cur_node->getText();

  StringRef builtin_name_ref(builtin_name);

  if (builtin_name_ref.startswith("Builtin.")) {
    StringRef stripped_name_ref =
        builtin_name_ref.drop_front(strlen("Builtin."));
    SmallVector<ValueDecl *, 1> builtin_decls;

    result._module =
        DeclsLookupSource::GetDeclsLookupSource(*ast, ConstString("Builtin"));

    if (!FindNamedDecls(ast, stripped_name_ref, result)) {
      result.Clear();
      result._error = stringWithFormat("Couldn't find %s in the builtin module",
                                       builtin_name.c_str());
    }
  } else {
    result._error = stringWithFormat(
        "BuiltinTypeName %s doesn't start with Builtin.", builtin_name.c_str());
  }
}

static void VisitNodeConstructor(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  VisitNodeResult kind_type_result;
  VisitNodeResult type_result;

  Demangle::Node::iterator end = cur_node->end();
  for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
    const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
    switch (child_node_kind) {
    case Demangle::Node::Kind::Enum:
    case Demangle::Node::Kind::Class:
    case Demangle::Node::Kind::Structure:
      nodes.push_back(*pos);
      VisitNode(ast, nodes, kind_type_result, generic_context);
      break;
    case Demangle::Node::Kind::Type:
      nodes.push_back(*pos);
      VisitNode(ast, nodes, type_result, generic_context);
      break;
    default:
      break;
    }
  }

  if (kind_type_result.HasSingleType() && type_result.HasSingleType()) {
    bool found = false;
    const size_t n = FindNamedDecls(ast, StringRef("init"), kind_type_result);
    if (n == 1) {
      found = true;
      kind_type_result._types[0] = FixCallingConv(
          kind_type_result._decls[0], kind_type_result._types[0].getPointer());
      result = kind_type_result;
    } else if (n > 0) {
      const size_t num_kind_type_results = kind_type_result._types.size();
      for (size_t i = 0; i < num_kind_type_results && !found; ++i) {
        auto &identifier_type = kind_type_result._types[i];
        if (identifier_type &&
            identifier_type->getKind() ==
                type_result._types.front()->getKind()) {
          // These are the same kind of type, we need to disambiguate them
          switch (identifier_type->getKind()) {
          default:
            break;
          case TypeKind::Function: {
            const AnyFunctionType *identifier_func =
                identifier_type->getAs<AnyFunctionType>();

            // inits are typed as (Foo.Type) -> (args...) -> Foo, but don't
            // assert that in case we're dealing with broken code.
            if (identifier_func->getInput()->is<AnyMetatypeType>() &&
                identifier_func->getResult()->is<AnyFunctionType>()) {
              identifier_func =
                  identifier_func->getResult()->getAs<AnyFunctionType>();
            }

            const AnyFunctionType *type_func =
                type_result._types.front()->getAs<AnyFunctionType>();
            if (CanType(identifier_func->getResult()
                            ->getDesugaredType()
                            ->getCanonicalType()) ==
                    CanType(type_func->getResult()
                                ->getDesugaredType()
                                ->getCanonicalType()) &&
                CanType(identifier_func->getInput()
                            ->getDesugaredType()
                            ->getCanonicalType()) ==
                    CanType(type_func->getInput()
                                ->getDesugaredType()
                                ->getCanonicalType())) {
              result._module = kind_type_result._module;
              result._decls.push_back(kind_type_result._decls[i]);
              result._types.push_back(
                  FixCallingConv(kind_type_result._decls[i],
                                 kind_type_result._types[i].getPointer()));
              found = true;
            }
          } break;
          }
        }
      }
    }
    // Didn't find a match, just return the raw function type
    if (!found)
      result = type_result;
  }
}

static void VisitNodeDestructor(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  VisitNodeResult kind_type_result;

  Demangle::Node::iterator end = cur_node->end();
  for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
    const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
    switch (child_node_kind) {
    case Demangle::Node::Kind::Enum:
    case Demangle::Node::Kind::Class:
    case Demangle::Node::Kind::Structure:
      nodes.push_back(*pos);
      VisitNode(ast, nodes, kind_type_result, generic_context);
      break;
    default:
      break;
    }
  }

  if (kind_type_result.HasSingleType()) {
    bool found = false;
    const size_t n = FindNamedDecls(ast, StringRef("deinit"), kind_type_result);
    if (n == 1) {
      found = true;
      kind_type_result._types[0] = FixCallingConv(
          kind_type_result._decls[0], kind_type_result._types[0].getPointer());
      result = kind_type_result;
    } else if (n > 0) {
      // I can't think of a reason why we would get more than one decl called
      // deinit here, but
      // just in case, if it is a function type, we should remember it.
      const size_t num_kind_type_results = kind_type_result._types.size();
      for (size_t i = 0; i < num_kind_type_results && !found; ++i) {
        auto &identifier_type = kind_type_result._types[i];
        if (identifier_type) {
          switch (identifier_type->getKind()) {
          default:
            break;
          case TypeKind::Function: {
            result._module = kind_type_result._module;
            result._decls.push_back(kind_type_result._decls[i]);
            result._types.push_back(
                FixCallingConv(kind_type_result._decls[i],
                               kind_type_result._types[i].getPointer()));
            found = true;
          } break;
          }
        }
      }
    }
  }
}

static void VisitNodeDeclContext(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  switch (cur_node->getNumChildren()) {
  default:
    result._error =
        stringWithFormat("DeclContext had %llu children, giving up",
                         (unsigned long long)cur_node->getNumChildren());
    break;
  case 0:
    result._error = "empty DeclContext unusable";
    break;
  case 1:
    // nominal type
    nodes.push_back(cur_node->getFirstChild());
    VisitNode(ast, nodes, result, generic_context);
    break;
  case 2:
    // function type: decl-ctx + type
    // FIXME: we should just be able to demangle the DeclCtx and resolve the
    // function
    // this is fragile and will easily break
    Demangle::NodePointer path = cur_node->getFirstChild();
    nodes.push_back(path);
    VisitNodeResult found_decls;
    VisitNode(ast, nodes, found_decls, generic_context);
    Demangle::NodePointer generics = cur_node->getChild(1);
    if (generics->getChild(0) == nullptr)
      break;
    generics = generics->getFirstChild();
    if (generics->getKind() != Demangle::Node::Kind::DependentGenericType)
      break;
    if (generics->getChild(0) == nullptr)
      break;
    generics = generics->getFirstChild();
    //                        if (generics->getKind() !=
    //                        Demangle::Node::Kind::ArchetypeList)
    //                            break;
    AbstractFunctionDecl *func_decl = nullptr;
    for (Decl *decl : found_decls._decls) {
      func_decl = dyn_cast<AbstractFunctionDecl>(decl);
      if (!func_decl)
        continue;
      GenericParamList *gen_params = func_decl->getGenericParams();
      if (!gen_params)
        continue;
    }
    if (func_decl) {
      result._module = found_decls._module;
      result._decls.push_back(func_decl);
      result._types.push_back(func_decl->getType().getPointer());
    } else
      result._error = "could not find a matching function for the DeclContext";
    break;
  }
}

static void VisitNodeExplicitClosure(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  // FIXME: closures are mangled as hanging off a function, but they really
  // aren't
  // so we cannot really do a lot about them, other than make a function type
  // for whatever their advertised type is, and cross fingers
  VisitNodeResult function_result;
  uint64_t index = UINT64_MAX;
  VisitNodeResult closure_type_result;
  VisitNodeResult module_result;
  Demangle::Node::iterator end = cur_node->end();
  for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
    const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
    switch (child_node_kind) {
    default:
      result._error =
          stringWithFormat("%s encountered in ExplicitClosure children",
                           SwiftDemangleNodeKindToCString(child_node_kind));
      break;
    case Demangle::Node::Kind::Module:
      nodes.push_back((*pos));
      VisitNode(ast, nodes, module_result, generic_context);
      break;
    case Demangle::Node::Kind::Function:
      nodes.push_back((*pos));
      VisitNode(ast, nodes, function_result, generic_context);
      break;
    case Demangle::Node::Kind::Number:
      index = (*pos)->getIndex();
      break;
    case Demangle::Node::Kind::Type:
      nodes.push_back((*pos));
      VisitNode(ast, nodes, closure_type_result, generic_context);
      break;
    }
  }
  if (closure_type_result.HasSingleType())
    result._types.push_back(closure_type_result._types.front());
  else
    result._error = "multiple potential candidates to be this closure's type";
  // FIXME: closures are not lookupable by compiler team's decision
  // ("You cannot perform lookup into local contexts." x3)
  // so we at least store the module the closure came from to enable
  // further local types lookups
  if (module_result._module)
    result._module = module_result._module;
}

static void VisitNodeExtension(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  VisitNodeResult module_result;
  VisitNodeResult type_result;
  std::string error;
  Demangle::Node::iterator end = cur_node->end();
  for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
    const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
    switch (child_node_kind) {
    default:
      result._error =
          stringWithFormat("%s encountered in extension children",
                           SwiftDemangleNodeKindToCString(child_node_kind));
      break;

    case Demangle::Node::Kind::Module:
      nodes.push_back((*pos));
      VisitNode(ast, nodes, module_result, generic_context);
      break;

    case Demangle::Node::Kind::Class:
    case Demangle::Node::Kind::Enum:
    case Demangle::Node::Kind::Structure:
      nodes.push_back((*pos));
      VisitNode(ast, nodes, type_result, generic_context);
      break;
    }
  }

  if (module_result._module) {
    if (type_result._decls.size() == 1) {
      Decl *decl = type_result._decls[0];
      NominalTypeDecl *nominal_decl = dyn_cast_or_null<NominalTypeDecl>(decl);
      if (nominal_decl) {
        result._module = DeclsLookupSource::GetDeclsLookupSource(
            module_result._module, nominal_decl);
      } else
        result._error = "unable to find nominal type for extension";
    } else
      result._error = "unable to find unique type for extension";
  } else
    result._error = "unable to find module name for extension";
}

static bool AreBothFunctionTypes(TypeKind a, TypeKind b) {
  bool is_first = false, is_second = false;
  if (a >= TypeKind::First_AnyFunctionType &&
      a <= TypeKind::Last_AnyFunctionType)
    is_first = true;
  if (b >= TypeKind::First_AnyFunctionType &&
      b <= TypeKind::Last_AnyFunctionType)
    is_second = true;
  return (is_first && is_second);
}

static bool CompareFunctionTypes(const AnyFunctionType *f,
                                 const AnyFunctionType *g,
                                 bool *input_matches = nullptr,
                                 bool *output_matches = nullptr) {
  bool in_matches = false, out_matches = false;
  if (nullptr == f)
    return (nullptr == g);
  if (nullptr == g)
    return false;

  auto f_input = f->getInput().getCanonicalTypeOrNull();
  auto g_input = g->getInput().getCanonicalTypeOrNull();

  auto f_output = f->getResult().getCanonicalTypeOrNull();
  auto g_output = g->getResult().getCanonicalTypeOrNull();

  if (f_input == g_input) {
    in_matches = true;
    if (f_output == g_output)
      out_matches = true;
  }

  if (input_matches)
    *input_matches = in_matches;
  if (output_matches)
    *output_matches = out_matches;

  return (in_matches && out_matches);
}

// VisitNodeFunction gets used for Function, Variable and Allocator:
static void VisitNodeFunction(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  VisitNodeResult identifier_result;
  VisitNodeResult type_result;
  VisitNodeResult decl_scope_result;
  Demangle::Node::iterator end = cur_node->end();
  bool found_univocous = false;
  for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
    if (found_univocous)
      break;
    auto child = *pos;
    const Demangle::Node::Kind child_node_kind = child->getKind();
    switch (child_node_kind) {
    default:
      result._error =
          stringWithFormat("%s encountered in function children",
                           SwiftDemangleNodeKindToCString(child_node_kind));
      break;

    // TODO: any other possible containers?
    case Demangle::Node::Kind::Function:
    case Demangle::Node::Kind::Class:
    case Demangle::Node::Kind::Enum:
    case Demangle::Node::Kind::Module:
    case Demangle::Node::Kind::Structure:
      nodes.push_back((*pos));
      VisitNode(ast, nodes, decl_scope_result, generic_context);
      break;

    case Demangle::Node::Kind::LocalDeclName: {
      if (child->getNumChildren() != 2 || !child->getChild(1)->hasText()) {
        if (result._error.empty())
          result._error =
              "unable to retrieve content for Node::Kind::LocalDeclName";
        break;
      }

      auto name = child->getChild(1); // First child is number.
      FindNamedDecls(ast, name->getText(), decl_scope_result);
      if (decl_scope_result._decls.size() == 0) {
        llvm::raw_string_ostream OS(result._error);
        OS << "demangled identifier " << name->getText()
           << " could not be found by name lookup";
        break;
      }
      std::copy(decl_scope_result._decls.begin(),
                decl_scope_result._decls.end(),
                back_inserter(identifier_result._decls));
      std::copy(decl_scope_result._types.begin(),
                decl_scope_result._types.end(),
                back_inserter(identifier_result._types));
      identifier_result._module = decl_scope_result._module;
      if (decl_scope_result._decls.size() == 1)
        found_univocous = true;
      break;
    }
    case Demangle::Node::Kind::Identifier:
    case Demangle::Node::Kind::InfixOperator:
    case Demangle::Node::Kind::PrefixOperator:
    case Demangle::Node::Kind::PostfixOperator:
      FindNamedDecls(ast, (*pos)->getText(), decl_scope_result);
      if (decl_scope_result._decls.size() == 0) {
        result._error = stringWithFormat(
            "demangled identifier %s could not be found by name lookup",
            (*pos)->getText().c_str());
        break;
      }
      std::copy(decl_scope_result._decls.begin(),
                decl_scope_result._decls.end(),
                back_inserter(identifier_result._decls));
      std::copy(decl_scope_result._types.begin(),
                decl_scope_result._types.end(),
                back_inserter(identifier_result._types));
      identifier_result._module = decl_scope_result._module;
      if (decl_scope_result._decls.size() == 1)
        found_univocous = true;
      break;

    case Demangle::Node::Kind::Type:
      nodes.push_back((*pos));
      VisitNode(ast, nodes, type_result, generic_context);
      break;
    }
  }

  //                    if (node_kind == Demangle::Node::Kind::Allocator)
  //                    {
  //                        // For allocators we don't have an identifier for
  //                        the name, we will
  //                        // need to extract it from the class or struct in
  //                        "identifier_result"
  //                        //Find
  //                        if (identifier_result.HasSingleType())
  //                        {
  //                            // This contains the class or struct
  //                            StringRef init_name("init");
  //
  //                            if (FindFirstNamedDeclWithKind(ast, init_name,
  //                            DeclKind::Constructor, identifier_result))
  //                            {
  //                            }
  //                        }
  //                    }

  if (identifier_result._types.size() == 1) {
    result._module = identifier_result._module;
    result._decls.push_back(identifier_result._decls[0]);
    result._types.push_back(FixCallingConv(
        identifier_result._decls[0], identifier_result._types[0].getPointer()));
  } else if (type_result.HasSingleType()) {
    const size_t num_identifier_results = identifier_result._types.size();
    bool found = false;
    for (size_t i = 0; i < num_identifier_results && !found; ++i) {
      auto &identifier_type = identifier_result._types[i];
      if (!identifier_type)
        continue;
      if (AreBothFunctionTypes(identifier_type->getKind(),
                               type_result._types.front()->getKind())) {
        const AnyFunctionType *identifier_func =
            identifier_type->getAs<AnyFunctionType>();
        const AnyFunctionType *type_func =
            type_result._types.front()->getAs<AnyFunctionType>();
        if (CompareFunctionTypes(identifier_func, type_func)) {
          result._module = identifier_result._module;
          result._decls.push_back(identifier_result._decls[i]);
          result._types.push_back(
              FixCallingConv(identifier_result._decls[i],
                             identifier_result._types[i].getPointer()));
          found = true;
        }
      }
    }
    // Didn't find a match, just return the raw function type
    if (!found)
      result = type_result;
  }
}

static void VisitNodeFunctionType(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  VisitNodeResult arg_type_result;
  VisitNodeResult return_type_result;
  Demangle::Node::iterator end = cur_node->end();
  bool is_in_class = false;
  bool throws = false;
  for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
    const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
    switch (child_node_kind) {
    case Demangle::Node::Kind::Class: {
      is_in_class = true;
      VisitNodeResult class_type_result;
      nodes.push_back(*pos);
      VisitNode(ast, nodes, class_type_result, generic_context);
    } break;
    case Demangle::Node::Kind::Structure: {
      VisitNodeResult class_type_result;
      nodes.push_back(*pos);
      VisitNode(ast, nodes, class_type_result, generic_context);
    } break;
    case Demangle::Node::Kind::ArgumentTuple:
    case Demangle::Node::Kind::Metatype: {
      nodes.push_back(*pos);
      VisitNode(ast, nodes, arg_type_result, generic_context);
    } break;
    case Demangle::Node::Kind::ThrowsAnnotation:
      throws = true;
      break;
    case Demangle::Node::Kind::ReturnType: {
      nodes.push_back(*pos);
      VisitNode(ast, nodes, return_type_result, generic_context);
    } break;
    default:
      break;
    }
  }
  Type arg_clang_type;
  Type return_clang_type;

  switch (arg_type_result._types.size()) {
  case 0:
    arg_clang_type = TupleType::getEmpty(*ast);
    break;
  case 1:
    arg_clang_type = arg_type_result._types.front().getPointer();
    break;
  default:
    result._error = "too many argument types for a function type";
    break;
  }

  switch (return_type_result._types.size()) {
  case 0:
    return_clang_type = TupleType::getEmpty(*ast);
    break;
  case 1:
    return_clang_type = return_type_result._types.front().getPointer();
    break;
  default:
    result._error = "too many return types for a function type";
    break;
  }

  if (arg_clang_type && return_clang_type) {
    result._types.push_back(
        FunctionType::get(arg_clang_type, return_clang_type,
                          FunctionType::ExtInfo().withThrows(throws)));
  }
}

static void VisitNodeSetterGetter(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  VisitNodeResult decl_ctx_result;
  std::string identifier;
  VisitNodeResult type_result;
  Demangle::Node::Kind node_kind = cur_node->getKind();

  for (Demangle::Node::iterator pos = cur_node->begin(), end = cur_node->end();
       pos != end; ++pos) {
    const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
    switch (child_node_kind) {
    case Demangle::Node::Kind::Class:
    case Demangle::Node::Kind::Module:
    case Demangle::Node::Kind::Structure:
      nodes.push_back(*pos);
      VisitNode(ast, nodes, decl_ctx_result, generic_context);
      break;
    case Demangle::Node::Kind::Identifier:
      identifier.assign((*pos)->getText());
      break;
    case Demangle::Node::Kind::Type:
      nodes.push_back(*pos);
      VisitNode(ast, nodes, type_result, generic_context);
      break;
    default:
      result._error =
          stringWithFormat("%s encountered in generic type children",
                           SwiftDemangleNodeKindToCString(child_node_kind));
      break;
    }
  }

  if (identifier == "subscript") {
    // Subscript setters and getters are named with the reserved word
    // "subscript".
    // Since there can be many subscripts for the same nominal type, we need to
    // find the one matching the specified type.

    FindNamedDecls(ast, identifier, decl_ctx_result);
    size_t num_decls = decl_ctx_result._decls.size();

    if (num_decls == 0) {
      result._error = "Could not find a subscript decl";
      return;
    }

    SubscriptDecl *subscript_decl;
    const AnyFunctionType *type_func =
        type_result._types.front()->getAs<AnyFunctionType>();

    CanType type_result_type(
        type_func->getResult()->getDesugaredType()->getCanonicalType());
    CanType type_input_type(
        type_func->getInput()->getDesugaredType()->getCanonicalType());

    FuncDecl *identifier_func = nullptr;

    for (size_t i = 0; i < num_decls; i++) {
      subscript_decl =
          dyn_cast_or_null<SubscriptDecl>(decl_ctx_result._decls[i]);
      if (subscript_decl) {
        switch (node_kind) {
        case Demangle::Node::Kind::Getter:
          identifier_func = subscript_decl->getGetter();
          break;
        case Demangle::Node::Kind::Setter:
          identifier_func = subscript_decl->getGetter();
          break;
        case Demangle::Node::Kind::DidSet:
          identifier_func = subscript_decl->getDidSetFunc();
          break;
        case Demangle::Node::Kind::WillSet:
          identifier_func = subscript_decl->getWillSetFunc();
          break;
        default:
          identifier_func = nullptr;
          break;
        }

        if (identifier_func && identifier_func->getType()) {
          const AnyFunctionType *identifier_func_type =
              identifier_func->getType()->getAs<AnyFunctionType>();
          if (identifier_func_type) {
            // Swift function types are formally functions that take the class
            // and return the method,
            // we have to strip off the first level of function call to compare
            // against the type
            // from the demangled name.
            const AnyFunctionType *identifier_uncurried_result =
                identifier_func_type->getResult()->getAs<AnyFunctionType>();
            if (identifier_uncurried_result) {
              CanType identifier_result_type(
                  identifier_uncurried_result->getResult()
                      ->getDesugaredType()
                      ->getCanonicalType());
              CanType identifier_input_type(
                  identifier_uncurried_result->getInput()
                      ->getDesugaredType()
                      ->getCanonicalType());
              if (identifier_result_type == type_result_type &&
                  identifier_input_type == type_input_type) {
                break;
              }
            }
          }
        }
      }
      identifier_func = nullptr;
    }

    if (identifier_func) {
      result._decls.push_back(identifier_func);
      result._types.push_back(FixCallingConv(
          identifier_func, identifier_func->getType().getPointer()));
    } else {
      result._error = "could not find a matching subscript signature";
    }
  } else {
    // Otherwise this is a getter/setter/etc for a variable.  Currently you
    // can't write a getter/setter that
    // takes a different type from the type of the variable.  So there is only
    // one possible function.
    AbstractStorageDecl *var_decl = nullptr;

    FindFirstNamedDeclWithKind(ast, identifier, DeclKind::Var, decl_ctx_result);

    if (decl_ctx_result._decls.size() == 1) {
      var_decl = dyn_cast_or_null<VarDecl>(decl_ctx_result._decls[0]);
    } else if (decl_ctx_result._decls.size() > 0) {
      // TODO: can we use the type to pick the right one? can we really have
      // multiple variables with the same name?
      result._error = stringWithFormat(
          "multiple variables with the same name %s", identifier.c_str());
      return;
    } else {
      result._error =
          stringWithFormat("no variables with the name %s", identifier.c_str());
      return;
    }

    if (var_decl) {
      FuncDecl *decl = nullptr;

      if (node_kind == Demangle::Node::Kind::DidSet &&
          var_decl->getDidSetFunc()) {
        decl = var_decl->getDidSetFunc();
      } else if (node_kind == Demangle::Node::Kind::Getter &&
                 var_decl->getGetter()) {
        decl = var_decl->getGetter();
      } else if (node_kind == Demangle::Node::Kind::Setter &&
                 var_decl->getSetter()) {
        decl = var_decl->getSetter();
      } else if (node_kind == Demangle::Node::Kind::WillSet &&
                 var_decl->getWillSetFunc()) {
        decl = var_decl->getWillSetFunc();
      }

      if (decl) {
        result._decls.push_back(decl);
        result._types.push_back(
            FixCallingConv(decl, decl->getType().getPointer()));
      } else {
        result._error = stringWithFormat(
            "could not retrieve %s for variable %s",
            SwiftDemangleNodeKindToCString(node_kind), identifier.c_str());
        return;
      }
    } else {
      result._error =
          stringWithFormat("no decl object for %s", identifier.c_str());
      return;
    }
  }
}

static void VisitNodeIdentifier(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  Demangle::NodePointer parent_node = nodes[nodes.size() - 2];
  DeclKind decl_kind = GetKindAsDeclKind(parent_node->getKind());

  if (!FindFirstNamedDeclWithKind(ast, cur_node->getText(), decl_kind,
                                  result)) {
    if (result._error.empty())
      result._error =
          stringWithFormat("unable to find Node::Kind::Identifier '%s'",
                           cur_node->getText().c_str());
  }
}

static void VisitNodeLocalDeclName(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  Demangle::NodePointer parent_node = nodes[nodes.size() - 2];
  std::string remangledNode = Demangle::mangleNode(parent_node);
  TypeDecl *decl = result._module.lookupLocalType(remangledNode);
  if (!decl)
    result._error = stringWithFormat("unable to lookup local type %s",
                                     remangledNode.c_str());
  else {
    // if this were to come from a closure, there may be no decl - just a module
    if (!result._decls.empty())
      result._decls.pop_back();
    if (!result._types.empty())
      result._types.pop_back();

    result._decls.push_back(decl);
    auto type = decl->getType();
    if (MetatypeType *metatype =
            dyn_cast_or_null<MetatypeType>(type.getPointer()))
      type = metatype->getInstanceType();
    result._types.push_back(type);
  }
}

static void VisitNodePrivateDeclName(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  Demangle::NodePointer parent_node = nodes[nodes.size() - 2];
  DeclKind decl_kind = GetKindAsDeclKind(parent_node->getKind());

  if (cur_node->getNumChildren() != 2) {
    if (result._error.empty())
      result._error = stringWithFormat(
          "unable to retrieve content for Node::Kind::PrivateDeclName");
    return;
  }

  Demangle::NodePointer priv_decl_id_node(cur_node->getChild(0));
  Demangle::NodePointer id_node(cur_node->getChild(1));

  if (!priv_decl_id_node->hasText() || !id_node->hasText()) {
    if (result._error.empty())
      result._error = stringWithFormat(
          "unable to retrieve content for Node::Kind::PrivateDeclName");
    return;
  }

  if (!FindFirstNamedDeclWithKind(ast, id_node->getText(), decl_kind, result,
                                  priv_decl_id_node->getText())) {
    if (result._error.empty())
      result._error = stringWithFormat(
          "unable to find Node::Kind::PrivateDeclName '%s' in '%s'",
          id_node->getText().c_str(), priv_decl_id_node->getText().c_str());
  }
}

static void VisitNodeInOut(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  nodes.push_back(cur_node->getFirstChild());
  VisitNodeResult type_result;
  VisitNode(ast, nodes, type_result, generic_context);
  if (type_result._types.size() == 1 && type_result._types[0]) {
    result._types.push_back(Type(LValueType::get(type_result._types[0])));
  } else {
    result._error = "couldn't resolve referent type";
  }
}

static void VisitNodeMetatype(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  auto iter = cur_node->begin();
  auto end = cur_node->end();

  Optional<MetatypeRepresentation> metatype_repr;
  VisitNodeResult type_result;

  for (; iter != end; ++iter) {
    switch ((*iter)->getKind()) {
    case Demangle::Node::Kind::Type:
      nodes.push_back(*iter);
      VisitNode(ast, nodes, type_result, generic_context);
      break;
    case Demangle::Node::Kind::MetatypeRepresentation:
      if ((*iter)->getText() == "@thick")
        metatype_repr = MetatypeRepresentation::Thick;
      else if ((*iter)->getText() == "@thin")
        metatype_repr = MetatypeRepresentation::Thin;
      else if ((*iter)->getText() == "@objc")
        metatype_repr = MetatypeRepresentation::ObjC;
      else
        ; // leave it alone if we don't understand the representation
      break;
    default:
      break;
    }
  }

  if (type_result.HasSingleType()) {
    result._types.push_back(
        MetatypeType::get(type_result._types[0], metatype_repr));
  } else {
    result._error = stringWithFormat(
        "instance type for metatype cannot be uniquely resolved");
    return;
  }
}

static void VisitNodeModule(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  std::string error;
  const char *module_name = cur_node->getText().c_str();
  if (!module_name || *module_name == '\0') {
    result._error = stringWithFormat("error: empty module name.");
    return;
  }

  result._module =
      DeclsLookupSource::GetDeclsLookupSource(*ast, ConstString(module_name));
  if (!result._module) {
    result._error = stringWithFormat("unable to load module '%s' (%s)",
                                     module_name, error.data());
  }
}

static void VisitNodeNonVariadicTuple(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  if (cur_node->begin() == cur_node->end()) {
    // No children of this tuple, make an empty tuple

    if (ast) {
      result._types.push_back(TupleType::getEmpty(*ast));
    } else {
      result._error = "invalid ASTContext";
    }
  } else {
    std::vector<TupleTypeElt> tuple_fields;
    Demangle::Node::iterator end = cur_node->end();
    for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
      nodes.push_back(*pos);
      VisitNodeResult tuple_element_result;
      VisitNode(ast, nodes, tuple_element_result, generic_context);
      if (tuple_element_result._error.empty() &&
          tuple_element_result._tuple_type_element.getType()) {
        tuple_fields.push_back(tuple_element_result._tuple_type_element);
      } else {
        result._error = tuple_element_result._error;
      }
    }
    if (result._error.empty()) {
      if (ast) {
        result._types.push_back(TupleType::get(tuple_fields, *ast));
      } else {
        result._error = "invalid ASTContext";
      }
    }
  }
}

static void VisitNodeProtocolList(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  if (cur_node->begin() != cur_node->end()) {
    VisitNodeResult protocol_types_result;
    nodes.push_back(cur_node->getFirstChild());
    VisitNode(ast, nodes, protocol_types_result, generic_context);
    if (protocol_types_result._error
            .empty() /* cannot check for empty type list as Any is allowed */) {
      if (ast) {
        result._types.push_back(
            ProtocolCompositionType::get(*ast, protocol_types_result._types));
      } else {
        result._error = "invalid ASTContext";
      }
    }
  }
}

static void VisitNodeQualifiedArchetype(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  if (cur_node->begin() != cur_node->end()) {
    Demangle::Node::iterator end = cur_node->end();
    VisitNodeResult type_result;
    uint64_t index = 0xFFFFFFFFFFFFFFFF;
    for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
      switch (pos->get()->getKind()) {
      case Demangle::Node::Kind::Number:
        index = pos->get()->getIndex();
        break;
      case Demangle::Node::Kind::DeclContext:
        nodes.push_back(*pos);
        VisitNode(ast, nodes, type_result, generic_context);
        break;
      default:
        break;
      }
    }
    if (index != 0xFFFFFFFFFFFFFFFF) {
      if (type_result._types.size() == 1 && type_result._decls.size() == 1) {
        // given a method defined as func ... (args) -> (ret) {...}
        // the Swift type system represents it as
        // (SomeTypeMoniker) -> (args) -> (ret), where SomeTypeMoniker is an
        // appropriately crafted
        // reference to the type that contains the method (e.g. for a struct, an
        // @inout StructType)
        // For a qualified archetype of a method, we do not care about the
        // first-level function, but about
        // the returned function, which is the thing whose archetypes we truly
        // care to extract
        // TODO: this might be a generally useful operation, but it requires a
        // Decl as well as a type
        // to be reliably performed, and as such we cannot just put it in
        // CompilerType as of now
        // (consider, func foo (@inout StructType) -> (Int) -> () vs struct
        // StructType {func foo(Int) -> ()} to see why)
        TypeBase *type_ptr = type_result._types[0].getPointer();
        Decl *decl_ptr = type_result._decls[0];
        // if this is a function...
        if (type_ptr && type_ptr->is<AnyFunctionType>()) {
          // if this is defined in a type...
          if (decl_ptr->getDeclContext()->isTypeContext()) {
            // if I can get the function type from it
            if (auto func_type = dyn_cast_or_null<AnyFunctionType>(type_ptr)) {
              // and it has a return type which is itself a function
              auto return_func_type = dyn_cast_or_null<AnyFunctionType>(
                  func_type->getResult().getPointer());
              if (return_func_type)
                type_ptr =
                    return_func_type; // then use IT as our source of archetypes
            }
          }
        }
        TypeBase *arg_type = GetTemplateArgument(type_ptr, index);
        result._types.push_back(Type(arg_type));
      } else if (type_result._module.IsExtension()) {
        result._types.push_back(
            type_result._module.GetQualifiedArchetype(index, ast));
      }
    }
  }
}

static void VisitNodeSelfTypeRef(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  nodes.push_back(cur_node->getFirstChild());
  VisitNodeResult type_result;
  VisitNode(ast, nodes, type_result, generic_context);
  if (type_result.HasSingleType()) {
    Type supposed_protocol_type(type_result.GetFirstType());
    ProtocolType *protocol_type = supposed_protocol_type->getAs<ProtocolType>();
    ProtocolDecl *protocol_decl =
        protocol_type ? protocol_type->getDecl() : nullptr;
    if (protocol_decl) {
      ArchetypeType::AssocTypeOrProtocolType assoc_protocol_type(protocol_decl);
      if (ast) {
        CanTypeWrapper<ArchetypeType> self_type = ArchetypeType::getNew(
            *ast, nullptr, assoc_protocol_type, ast->getIdentifier("Self"),
            {supposed_protocol_type}, Type(), false);
        if (self_type.getPointer())
          result._types.push_back(Type(self_type));
        else
          result._error = "referent type cannot be made into an archetype";
      } else {
        result._error = "invalid ASTContext";
      }
    } else {
      result._error = "referent type does not resolve to a protocol";
    }
  } else {
    result._error = "couldn't resolve referent type";
  }
}

static void VisitNodeTupleElement(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  const char *tuple_name = NULL;
  VisitNodeResult tuple_type_result;
  Demangle::Node::iterator end = cur_node->end();
  for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
    const Demangle::Node::Kind child_node_kind = (*pos)->getKind();
    switch (child_node_kind) {
    case Demangle::Node::Kind::TupleElementName:
      tuple_name = (*pos)->getText().c_str();
      break;
    case Demangle::Node::Kind::Type:
      nodes.push_back((*pos)->getFirstChild());
      VisitNode(ast, nodes, tuple_type_result, generic_context);
      break;
    default:
      break;
    }
  }

  if (tuple_type_result._error.empty() &&
      tuple_type_result._types.size() == 1) {
    if (tuple_name)
      result._tuple_type_element =
          TupleTypeElt(tuple_type_result._types.front().getPointer(),
                       ast->getIdentifier(tuple_name));
    else
      result._tuple_type_element =
          TupleTypeElt(tuple_type_result._types.front().getPointer());
  }
}

static void VisitNodeTypeList(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  if (cur_node->begin() != cur_node->end()) {
    Demangle::Node::iterator end = cur_node->end();
    for (Demangle::Node::iterator pos = cur_node->begin(); pos != end; ++pos) {
      nodes.push_back(*pos);
      VisitNodeResult type_result;
      VisitNode(ast, nodes, type_result, generic_context);
      if (type_result._error.empty() && type_result._types.size() == 1) {
        if (type_result._decls.empty())
          result._decls.push_back(NULL);
        else
          result._decls.push_back(type_result._decls.front());
        result._types.push_back(type_result._types.front());
      } else {
        // If we run into any errors parsing getting any types for a type list
        // we need to bail out and return an error. We have cases where a
        // private
        // typealias was used in a mangled variable name. The private typealias
        // are not included in the modules that are available to the debugger.
        // This was contained inside of a bound generic structure type that was
        // expecting two types. If we don't bail out, we end up filling in just
        // one type in "result" instead of two and then the compiler will crash
        // later when it tries to create the bound generic from only 1 type.
        result.Clear();
        result._error = type_result._error;
        break;
      }
    }
  }
}

static void VisitNodeUnowned(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  nodes.push_back(cur_node->getFirstChild());
  VisitNodeResult type_result;
  VisitNode(ast, nodes, type_result, generic_context);
  if (type_result._types.size() == 1 && type_result._types[0]) {
    if (ast) {
      result._types.push_back(
          Type(UnownedStorageType::get(type_result._types[0], *ast)));
    } else {
      result._error = "invalid ASTContext";
    }
  } else {
    result._error = "couldn't resolve referent type";
  }
}

static void
VisitNodeWeak(ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
              Demangle::NodePointer &cur_node, VisitNodeResult &result,
              const VisitNodeResult &generic_context) { // set by GenericType case
  nodes.push_back(cur_node->getFirstChild());
  VisitNodeResult type_result;
  VisitNode(ast, nodes, type_result, generic_context);
  if (type_result._types.size() == 1 && type_result._types[0]) {
    if (ast) {
      result._types.push_back(
          Type(WeakStorageType::get(type_result._types[0], *ast)));
    } else {
      result._error = "invalid ASTContext";
    }
  } else {
    result._error = "couldn't resolve referent type";
  }
}

static void VisitFirstChildNode(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  if (cur_node->begin() != cur_node->end()) {
    nodes.push_back(cur_node->getFirstChild());
    VisitNode(ast, nodes, result, generic_context);
  }
}

static void VisitAllChildNodes(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer &cur_node, VisitNodeResult &result,
    const VisitNodeResult &generic_context) { // set by GenericType case
  Demangle::Node::iterator child_end = cur_node->end();
  for (Demangle::Node::iterator child_pos = cur_node->begin();
       child_pos != child_end; ++child_pos) {
    nodes.push_back(*child_pos);
    VisitNode(ast, nodes, result, generic_context);
  }
}

static void visitNodeImpl(
    ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
    Demangle::NodePointer node, VisitNodeResult &result,
    const VisitNodeResult &genericContext) { // set by GenericType case
  assert(result._error.empty());
  assert(nodes.back() == node);

  const Demangle::Node::Kind nodeKind = node->getKind();

  switch (nodeKind) {
  case Demangle::Node::Kind::OwningAddressor:
  case Demangle::Node::Kind::OwningMutableAddressor:
  case Demangle::Node::Kind::UnsafeAddressor:
  case Demangle::Node::Kind::UnsafeMutableAddressor:
    VisitNodeAddressor(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::ArchetypeRef:
    VisitNodeArchetypeRef(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Archetype:
    VisitNodeArchetype(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::ArgumentTuple:
    VisitFirstChildNode(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::AssociatedTypeRef:
    VisitNodeAssociatedTypeRef(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::BoundGenericClass:
  case Demangle::Node::Kind::BoundGenericStructure:
  case Demangle::Node::Kind::BoundGenericEnum:
    VisitNodeBoundGeneric(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::BuiltinTypeName:
    VisitNodeBuiltinTypeName(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Structure:
  case Demangle::Node::Kind::Class:
  case Demangle::Node::Kind::Enum:
  case Demangle::Node::Kind::Global:
  case Demangle::Node::Kind::Static:
  case Demangle::Node::Kind::TypeAlias:
  case Demangle::Node::Kind::Type:
  case Demangle::Node::Kind::TypeMangling:
  case Demangle::Node::Kind::ReturnType:
  case Demangle::Node::Kind::Protocol:
    VisitAllChildNodes(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Constructor:
    VisitNodeConstructor(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Destructor:
    VisitNodeDestructor(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::DeclContext:
    VisitNodeDeclContext(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::ErrorType:
    result._error = "error type encountered while demangling name";
    break;

  case Demangle::Node::Kind::Extension:
    VisitNodeExtension(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::ExplicitClosure:
    VisitNodeExplicitClosure(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Function:
  case Demangle::Node::Kind::Allocator:
  case Demangle::Node::Kind::Variable: // Out of order on purpose
    VisitNodeFunction(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::FunctionType:
  case Demangle::Node::Kind::UncurriedFunctionType: // Out of order on
                                                    // purpose.
    VisitNodeFunctionType(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::DidSet:
  case Demangle::Node::Kind::Getter:
  case Demangle::Node::Kind::Setter:
  case Demangle::Node::Kind::WillSet: // out of order on purpose
    VisitNodeSetterGetter(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::LocalDeclName:
    VisitNodeLocalDeclName(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Identifier:
    VisitNodeIdentifier(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::InOut:
    VisitNodeInOut(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Metatype:
    VisitNodeMetatype(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Module:
    VisitNodeModule(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::NonVariadicTuple:
    VisitNodeNonVariadicTuple(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::PrivateDeclName:
    VisitNodePrivateDeclName(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::ProtocolList:
    VisitNodeProtocolList(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::QualifiedArchetype:
    VisitNodeQualifiedArchetype(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::SelfTypeRef:
    VisitNodeSelfTypeRef(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::TupleElement:
    VisitNodeTupleElement(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::TypeList:
    VisitNodeTypeList(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Unowned:
    VisitNodeUnowned(ast, nodes, node, result, genericContext);
    break;

  case Demangle::Node::Kind::Weak:
    VisitNodeWeak(ast, nodes, node, result, genericContext);
    break;
  default:
    break;
  }
}

static void
VisitNode(ASTContext *ast, std::vector<Demangle::NodePointer> &nodes,
          VisitNodeResult &result,
          const VisitNodeResult &genericContext) { // set by GenericType case
  if (nodes.empty())
    result._error = "no node";
  else if (nodes.back() == nullptr)
    result._error = "last node is NULL";
  else
    result._error = "";

  if (result._error.empty())
    visitNodeImpl(ast, nodes, nodes.back(), result, genericContext);

  nodes.pop_back();
}

Decl *ide::getDeclFromUSR(ASTContext &context, StringRef USR,
                          std::string &error) {
  if (!USR.startswith("s:")) {
    error = "not a Swift USR";
    return nullptr;
  }

  std::string mangledName(USR);
  // Convert to a symbol name by replacing the USR prefix.
  // This relies on USR generation being very close to symbol mangling; if we
  // need to support entities with customized USRs (e.g. extensions), we will
  // need to do something smarter here.
  mangledName.replace(0, 2, "_T");

  return getDeclFromMangledSymbolName(context, mangledName, error);
}

Decl *ide::getDeclFromMangledSymbolName(ASTContext &context,
                                        StringRef mangledName,
                                        std::string &error) {
  std::vector<Demangle::NodePointer> nodes;
  nodes.push_back(
      Demangle::demangleSymbolAsNode(mangledName.data(), mangledName.size()));
  VisitNodeResult emptyGenericContext;
  VisitNodeResult result;
  VisitNode(&context, nodes, result, emptyGenericContext);
  error = result._error;
  if (error.empty() && result._decls.size() == 1) {
    return result._decls.front();
  } else {
    llvm::raw_string_ostream OS(error);
    OS << "decl for symbol name '" << mangledName << "' was not found";
    return nullptr;
  }
  return nullptr;
}

Type ide::getTypeFromMangledTypename(ASTContext &Ctx,
                                     StringRef mangledName,
                                     std::string &error) {
  std::vector<Demangle::NodePointer> nodes;
  nodes.push_back(
      Demangle::demangleTypeAsNode(mangledName.data(), mangledName.size()));
  VisitNodeResult empty_generic_context;
  VisitNodeResult result;

  VisitNode(&Ctx, nodes, result, empty_generic_context);
  error = result._error;
  if (error.empty() && result._types.size() == 1) {
    return result._types.front().getPointer();
  } else {
    error = stringWithFormat("type for typename '%s' was not found",
                             mangledName);
    return Type();
  }
  return Type();
}

Type ide::getTypeFromMangledSymbolname(ASTContext &Ctx,
                                       StringRef mangledName,
                                       std::string &error) {
  std::vector<Demangle::NodePointer> nodes;
  nodes.push_back(
      Demangle::demangleSymbolAsNode(mangledName.data(), mangledName.size()));
  VisitNodeResult empty_generic_context;
  VisitNodeResult result;

  VisitNode(&Ctx, nodes, result, empty_generic_context);
  error = result._error;
  if (error.empty() && result._types.size() == 1) {
    return result._types.front().getPointer();
  } else {
    error = stringWithFormat("type for symbolname '%s' was not found",
                             mangledName);
    return Type();
  }
  return Type();
}
