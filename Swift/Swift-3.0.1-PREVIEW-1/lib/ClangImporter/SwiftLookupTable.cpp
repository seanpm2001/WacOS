//===--- SwiftLookupTable.cpp - Swift Lookup Table ------------------------===//
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
// This file implements support for Swift name lookup tables stored in Clang
// modules.
//
//===----------------------------------------------------------------------===//
#include "SwiftLookupTable.h"
#include "swift/Basic/STLExtras.h"
#include "swift/Basic/Version.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Bitcode/BitstreamWriter.h"
#include "llvm/Bitcode/RecordLayout.h"
#include "llvm/Support/OnDiskHashTable.h"

using namespace swift;
using namespace llvm::support;

/// Determine whether the new declarations matches an existing declaration.
static bool matchesExistingDecl(clang::Decl *decl, clang::Decl *existingDecl) {
  // If the canonical declarations are equivalent, we have a match.
  if (decl->getCanonicalDecl() == existingDecl->getCanonicalDecl()) {
    return true;
  }

  return false;
}

namespace {
enum class MacroConflictAction {
  Discard,
  Replace,
  AddAsAlternative
};
}

/// Based on the Clang module structure, decides what to do when a new
/// definition of an existing macro is seen: discard it, have it replace the
/// old one, or add it as an alternative.
///
/// Specifically, if the innermost explicit submodule containing \p newMacro
/// contains the innermost explicit submodule containing \p existingMacro,
/// \p newMacro should replace \p existingMacro; if they're the same module,
/// \p existingMacro should stay in place. Otherwise, they don't share an
/// explicit module, and should be considered alternatives.
///
/// Note that the above assumes that macro definitions are processed in reverse
/// order, i.e. the first definition seen is the last in a translation unit.
///
/// If we're not currently building a module, then the "latest" macro wins,
/// which (by the same assumption) should be the existing macro.
static MacroConflictAction
considerReplacingExistingMacro(const clang::MacroInfo *newMacro,
                               const clang::MacroInfo *existingMacro,
                               const clang::Preprocessor *PP) {
  assert(PP);
  assert(newMacro);
  assert(existingMacro);
  assert(newMacro->getOwningModuleID() == 0);
  assert(existingMacro->getOwningModuleID() == 0);

  if (PP->getLangOpts().CurrentModule.empty())
    return MacroConflictAction::Discard;

  clang::ModuleMap &moduleInfo = PP->getHeaderSearchInfo().getModuleMap();
  const clang::SourceManager &sourceMgr = PP->getSourceManager();

  auto findContainingExplicitModule =
      [&moduleInfo, &sourceMgr](const clang::MacroInfo *macro)
        -> const clang::Module * {

    clang::SourceLocation definitionLoc = macro->getDefinitionLoc();
    assert(definitionLoc.isValid() &&
           "implicitly-defined macros shouldn't show up in a module's lookup");
    clang::FullSourceLoc fullLoc(definitionLoc, sourceMgr);

    const clang::Module *module = moduleInfo.inferModuleFromLocation(fullLoc);
    assert(module && "we are building a module; everything should be modular");

    while (module->isSubModule()) {
      if (module->IsExplicit)
        break;
      module = module->Parent;
    }
    return module;
  };

  const clang::Module *newModule = findContainingExplicitModule(newMacro);
  const clang::Module *existingModule =
      findContainingExplicitModule(existingMacro);

  if (existingModule == newModule)
    return MacroConflictAction::Discard;
  if (existingModule->isSubModuleOf(newModule))
    return MacroConflictAction::Replace;
  return MacroConflictAction::AddAsAlternative;
}

bool SwiftLookupTable::contextRequiresName(ContextKind kind) {
  switch (kind) {
  case ContextKind::ObjCClass:
  case ContextKind::ObjCProtocol:
  case ContextKind::Tag:
  case ContextKind::Typedef:
    return true;

  case ContextKind::TranslationUnit:
    return false;
  }
}

/// Try to translate the given Clang declaration into a context.
static Optional<SwiftLookupTable::StoredContext>
translateDeclToContext(clang::NamedDecl *decl) {
  // Tag declaration.
  if (auto tag = dyn_cast<clang::TagDecl>(decl)) {
    if (tag->getIdentifier())
      return std::make_pair(SwiftLookupTable::ContextKind::Tag, tag->getName());
    if (auto typedefDecl = tag->getTypedefNameForAnonDecl())
      return std::make_pair(SwiftLookupTable::ContextKind::Tag,
                            typedefDecl->getName());
    return None;
  }

  // Objective-C class context.
  if (auto objcClass = dyn_cast<clang::ObjCInterfaceDecl>(decl))
    return std::make_pair(SwiftLookupTable::ContextKind::ObjCClass,
                          objcClass->getName());

  // Objective-C protocol context.
  if (auto objcProtocol = dyn_cast<clang::ObjCProtocolDecl>(decl))
    return std::make_pair(SwiftLookupTable::ContextKind::ObjCProtocol,
                          objcProtocol->getName());

  // Typedefs.
  if (auto typedefName = dyn_cast<clang::TypedefNameDecl>(decl)) {
    // If this typedef is merely a restatement of a tag declaration's type,
    // return the result for that tag.
    if (auto tag = typedefName->getUnderlyingType()->getAsTagDecl())
      return translateDeclToContext(const_cast<clang::TagDecl *>(tag));

    // Otherwise, this must be a typedef mapped to a strong type.
    return std::make_pair(SwiftLookupTable::ContextKind::Typedef,
                          typedefName->getName());
  }

  return None;
}

auto SwiftLookupTable::translateDeclContext(const clang::DeclContext *dc)
    -> Optional<SwiftLookupTable::StoredContext> {
  // Translation unit context.
  if (dc->isTranslationUnit())
    return std::make_pair(ContextKind::TranslationUnit, StringRef());

  // Tag declaration context.
  if (auto tag = dyn_cast<clang::TagDecl>(dc))
    return translateDeclToContext(const_cast<clang::TagDecl *>(tag));

  // Objective-C class context.
  if (auto objcClass = dyn_cast<clang::ObjCInterfaceDecl>(dc))
    return std::make_pair(ContextKind::ObjCClass, objcClass->getName());

  // Objective-C protocol context.
  if (auto objcProtocol = dyn_cast<clang::ObjCProtocolDecl>(dc))
    return std::make_pair(ContextKind::ObjCProtocol, objcProtocol->getName());

  return None;
}

Optional<SwiftLookupTable::StoredContext>
SwiftLookupTable::translateContext(EffectiveClangContext context) {
  switch (context.getKind()) {
  case EffectiveClangContext::DeclContext: {
    return translateDeclContext(context.getAsDeclContext());
  }

  case EffectiveClangContext::TypedefContext:
    return std::make_pair(ContextKind::Typedef,
                          context.getTypedefName()->getName());

  case EffectiveClangContext::UnresolvedContext:
    // Resolve the context.
    if (auto decl = resolveContext(context.getUnresolvedName()))
      return translateDeclToContext(decl);

    return None;
  }
}

/// Lookup an unresolved context name and resolve it to a Clang
/// declaration context or typedef name.
clang::NamedDecl *SwiftLookupTable::resolveContext(StringRef unresolvedName) {
  // Look for a context with the given Swift name.
  for (auto entry : lookup(unresolvedName, 
                           std::make_pair(ContextKind::TranslationUnit,
                                          StringRef()))) {
    if (auto decl = entry.dyn_cast<clang::NamedDecl *>()) {
      if (isa<clang::TagDecl>(decl) ||
          isa<clang::ObjCInterfaceDecl>(decl) ||
          isa<clang::TypedefNameDecl>(decl))
        return decl;
    }
  }

  // FIXME: Search imported modules to resolve the context.

  return nullptr;
}

void SwiftLookupTable::addCategory(clang::ObjCCategoryDecl *category) {
  assert(!Reader && "Cannot modify a lookup table stored on disk");

  // Add the category.
  Categories.push_back(category);
}

bool SwiftLookupTable::resolveUnresolvedEntries(
    SmallVectorImpl<SingleEntry> &unresolved) {
  // Common case: nothing left to resolve.
  unresolved.clear();
  if (UnresolvedEntries.empty()) return false;

  // Reprocess each of the unresolved entries to see if it can be
  // resolved now that we're done. This occurs when a swift_name'd
  // entity becomes a member of an entity that follows it in the
  // translation unit, e.g., given:
  //
  // \code
  //   typedef enum FooSomeEnumeration __attribute__((Foo.SomeEnum)) {
  //     ...
  //   } FooSomeEnumeration;
  //
  //   typedef struct Foo {
  //     
  //   } Foo;
  // \endcode
  //
  // FooSomeEnumeration belongs inside "Foo", but we haven't actually
  // seen "Foo" yet. Therefore, we will reprocess FooSomeEnumeration
  // at the end, once "Foo" is available. There are several reasons
  // this loop can execute:
  //
  // * Import-as-member places an entity inside of an another entity
  // that comes later in the translation unit. The number of
  // iterations that can be caused by this is bounded by the nesting
  // depth. (At present, that depth is limited to 2).
  //
  // * An erroneous import-as-member will cause an extra iteration at
  // the end, so that the loop can detect that nothing changed and
  // return a failure.
  while (true) {
    // Take the list of unresolved entries to process.
    auto prevNumUnresolvedEntries = UnresolvedEntries.size();
    auto currentUnresolved = std::move(UnresolvedEntries);
    UnresolvedEntries.clear();

    // Process each of the currently-unresolved entries.
    for (const auto &entry : currentUnresolved)
      addEntry(std::get<0>(entry), std::get<1>(entry), std::get<2>(entry));

    // Are we done?
    if (UnresolvedEntries.empty()) return false;

    // If nothing changed, fail: something is unresolvable, and the
    // caller should complain.
    if (UnresolvedEntries.size() == prevNumUnresolvedEntries) {
      for (const auto &entry : UnresolvedEntries)
        unresolved.push_back(std::get<1>(entry));
      return true;
    }

    // Something got resolved, so loop again.
    assert(UnresolvedEntries.size() < prevNumUnresolvedEntries);
  }
}

/// Determine whether the entry is a global declaration that is being
/// mapped as a member of a particular type or extension thereof.
///
/// This should only return true when the entry isn't already nested
/// within a context. For example, it will return false for
/// enumerators, because those are naturally nested within the
/// enumeration declaration.
static bool isGlobalAsMember(SwiftLookupTable::SingleEntry entry,
                             SwiftLookupTable::StoredContext context) {
  switch (context.first) {
  case SwiftLookupTable::ContextKind::TranslationUnit:
    // We're not mapping this as a member of anything.
    return false;

  case SwiftLookupTable::ContextKind::Tag:
  case SwiftLookupTable::ContextKind::ObjCClass:
  case SwiftLookupTable::ContextKind::ObjCProtocol:
  case SwiftLookupTable::ContextKind::Typedef:
    // We're mapping into a type context.
    break;
  }

  // Macros are never stored within a non-translation-unit context in
  // Clang.
  if (entry.is<clang::MacroInfo *>()) return true;

  // We have a declaration.
  auto decl = entry.get<clang::NamedDecl *>();

  // Enumerators are always stored within the enumeration, despite
  // having the translation unit as their redeclaration context.
  if (isa<clang::EnumConstantDecl>(decl)) return false;

  // If the redeclaration context is namespace-scope, then we're
  // mapping as a member.
  return decl->getDeclContext()->getRedeclContext()->isFileContext();
}

bool SwiftLookupTable::addLocalEntry(SingleEntry newEntry,
                                     SmallVectorImpl<uintptr_t> &entries,
                                     const clang::Preprocessor *PP) {
  // Check whether this entry matches any existing entry.
  auto decl = newEntry.dyn_cast<clang::NamedDecl *>();
  auto macro = newEntry.dyn_cast<clang::MacroInfo *>();
  for (auto &existingEntry : entries) {
    // If it matches an existing declaration, there's nothing to do.
    if (decl && isDeclEntry(existingEntry) &&
        matchesExistingDecl(decl, mapStoredDecl(existingEntry)))
      return false;

    // If it matches an existing macro, decide on the best course of action.
    if (macro && isMacroEntry(existingEntry)) {
      MacroConflictAction action =
         considerReplacingExistingMacro(macro,
                                        mapStoredMacro(existingEntry),
                                        PP);
      switch (action) {
      case MacroConflictAction::Discard:
        return false;
      case MacroConflictAction::Replace:
        existingEntry = encodeEntry(macro);
        return false;
      case MacroConflictAction::AddAsAlternative:
        break;
      }
    }
  }

  // Add an entry to this context.
  if (decl)
    entries.push_back(encodeEntry(decl));
  else
    entries.push_back(encodeEntry(macro));
  return true;
}

void SwiftLookupTable::addEntry(DeclName name, SingleEntry newEntry,
                                EffectiveClangContext effectiveContext,
                                const clang::Preprocessor *PP) {
  assert(!Reader && "Cannot modify a lookup table stored on disk");

  // Translate the context.
  auto contextOpt = translateContext(effectiveContext);
  if (!contextOpt) {
    // If it is a declaration with a swift_name attribute, we might be
    // able to resolve this later.
    if (auto decl = newEntry.dyn_cast<clang::NamedDecl *>()) {
      if (decl->hasAttr<clang::SwiftNameAttr>()) {
        UnresolvedEntries.push_back(
          std::make_tuple(name, newEntry, effectiveContext));
      }
    }

    return;
  }

  auto context = *contextOpt;

  // If this is a global imported as a member, record is as such.
  if (isGlobalAsMember(newEntry, context)) {
    auto &entries = GlobalsAsMembers[context];
    (void)addLocalEntry(newEntry, entries, PP);
  }

  // Find the list of entries for this base name.
  auto &entries = LookupTable[name.getBaseName().str()];
  auto decl = newEntry.dyn_cast<clang::NamedDecl *>();
  auto macro = newEntry.dyn_cast<clang::MacroInfo *>();
  for (auto &entry : entries) {
    if (entry.Context == context) {
      // We have entries for this context.
      (void)addLocalEntry(newEntry, entry.DeclsOrMacros, PP);
      return;
    }
  }

  // This is a new context for this name. Add it.
  FullTableEntry entry;
  entry.Context = context;
  if (decl)
    entry.DeclsOrMacros.push_back(encodeEntry(decl));
  else
    entry.DeclsOrMacros.push_back(encodeEntry(macro));
  entries.push_back(entry);
}

auto SwiftLookupTable::findOrCreate(StringRef baseName) 
  -> llvm::DenseMap<StringRef, SmallVector<FullTableEntry, 2>>::iterator {
  // If there is no base name, there is nothing to find.
  if (baseName.empty()) return LookupTable.end();

  // Find entries for this base name.
  auto known = LookupTable.find(baseName);

  // If we found something, we're done.
  if (known != LookupTable.end()) return known;
  
  // If there's no reader, we've found all there is to find.
  if (!Reader) return known;

  // Lookup this base name in the module file.
  SmallVector<FullTableEntry, 2> results;
  (void)Reader->lookup(baseName, results);

  // Add an entry to the table so we don't look again.
  known = LookupTable.insert({ std::move(baseName), std::move(results) }).first;

  return known;
}

SmallVector<SwiftLookupTable::SingleEntry, 4>
SwiftLookupTable::lookup(StringRef baseName,
                         llvm::Optional<StoredContext> searchContext) {
  SmallVector<SwiftLookupTable::SingleEntry, 4> result;

  // Find the lookup table entry for this base name.
  auto known = findOrCreate(baseName);
  if (known == LookupTable.end()) return result;

  // Walk each of the entries.
  for (auto &entry : known->second) {
    // If we're looking in a particular context and it doesn't match the
    // entry context, we're done.
    if (searchContext && entry.Context != *searchContext)
      continue;

    // Map each of the declarations.
    for (auto &stored : entry.DeclsOrMacros)
      result.push_back(mapStored(stored));
  }

  return result;
}

SmallVector<SwiftLookupTable::SingleEntry, 4>
SwiftLookupTable::lookupGlobalsAsMembers(StoredContext context) {
  SmallVector<SwiftLookupTable::SingleEntry, 4> result;

  // Find entries for this base name.
  auto known = GlobalsAsMembers.find(context);

  // If we didn't find anything...
  if (known == GlobalsAsMembers.end()) {
    // If there's no reader, we've found all there is to find.
    if (!Reader) return result;

    // Lookup this base name in the module extension file.
    SmallVector<uintptr_t, 2> results;
    (void)Reader->lookupGlobalsAsMembers(context, results);

    // Add an entry to the table so we don't look again.
    known = GlobalsAsMembers.insert({ std::move(context),
                                      std::move(results) }).first;
  }

  // Map each of the results.
  for (auto &entry : known->second) {
    result.push_back(mapStored(entry));
  }

  return result;
}

SmallVector<SwiftLookupTable::SingleEntry, 4>
SwiftLookupTable::lookupGlobalsAsMembers(EffectiveClangContext context) {
  // Translate context.
  if (!context) return { };

  Optional<StoredContext> storedContext = translateContext(context);
  if (!storedContext) return { };

  return lookupGlobalsAsMembers(*storedContext);
}

SmallVector<SwiftLookupTable::SingleEntry, 4>
SwiftLookupTable::allGlobalsAsMembers() {
  // If we have a reader, deserialize all of the globals-as-members data.
  if (Reader) {
    for (auto context : Reader->getGlobalsAsMembersContexts()) {
      (void)lookupGlobalsAsMembers(context);
    }
  }

  // Collect all of the keys and sort them.
  SmallVector<StoredContext, 8> contexts;
  for (const auto &globalAsMember : GlobalsAsMembers) {
    contexts.push_back(globalAsMember.first);
  }
  llvm::array_pod_sort(contexts.begin(), contexts.end());

  // Collect all of the results in order.
  SmallVector<SwiftLookupTable::SingleEntry, 4> results;
  for (const auto &context : contexts) {
    for (auto &entry : GlobalsAsMembers[context])
      results.push_back(mapStored(entry));
  }
  return results;
}

SmallVector<SwiftLookupTable::SingleEntry, 4>
SwiftLookupTable::lookup(StringRef baseName,
                         EffectiveClangContext searchContext) {
  // Translate context.
  Optional<StoredContext> context;
  if (searchContext) {
    context = translateContext(searchContext);
    if (!context) return { };
  }

  return lookup(baseName, context);
}

SmallVector<StringRef, 4> SwiftLookupTable::allBaseNames() {
  // If we have a reader, enumerate its base names.
  if (Reader) return Reader->getBaseNames();

  // Otherwise, walk the lookup table.
  SmallVector<StringRef, 4> result;
  for (const auto &entry : LookupTable) {
    result.push_back(entry.first);
  }
  return result;
}

SmallVector<clang::NamedDecl *, 4>
SwiftLookupTable::lookupObjCMembers(StringRef baseName) {
  SmallVector<clang::NamedDecl *, 4> result;

  // Find the lookup table entry for this base name.
  auto known = findOrCreate(baseName);
  if (known == LookupTable.end()) return result;

  // Walk each of the entries.
  for (auto &entry : known->second) {
    // If we're looking in a particular context and it doesn't match the
    // entry context, we're done.
    switch (entry.Context.first) {
    case ContextKind::TranslationUnit:
    case ContextKind::Tag:
      continue;

    case ContextKind::ObjCClass:
    case ContextKind::ObjCProtocol:
    case ContextKind::Typedef:
      break;
    }

    // Map each of the declarations.
    for (auto &stored : entry.DeclsOrMacros) {
      assert(isDeclEntry(stored) && "Not a declaration?");
      result.push_back(mapStoredDecl(stored));
    }
  }

  return result;
}

ArrayRef<clang::ObjCCategoryDecl *> SwiftLookupTable::categories() {
  if (!Categories.empty() || !Reader) return Categories;

  // Map categories known to the reader.
  for (auto declID : Reader->categories()) {
    auto category =
      cast_or_null<clang::ObjCCategoryDecl>(
        Reader->getASTReader().GetLocalDecl(Reader->getModuleFile(), declID));
    if (category)
      Categories.push_back(category);

  }

  return Categories;
}

static void printName(clang::NamedDecl *named, llvm::raw_ostream &out) {
  // If there is a name, print it.
  if (!named->getDeclName().isEmpty()) {
    // If we have an Objective-C method, print the class name along
    // with '+'/'-'.
    if (auto objcMethod = dyn_cast<clang::ObjCMethodDecl>(named)) {
      out << (objcMethod->isInstanceMethod() ? '-' : '+') << '[';
      if (auto classDecl = objcMethod->getClassInterface()) {
        classDecl->printName(out);
        out << ' ';
      } else if (auto proto = dyn_cast<clang::ObjCProtocolDecl>(
                                objcMethod->getDeclContext())) {
        proto->printName(out);
        out << ' ';
      }
      named->printName(out);
      out << ']';
      return;
    }

    // If we have an Objective-C property, print the class name along
    // with the property name.
    if (auto objcProperty = dyn_cast<clang::ObjCPropertyDecl>(named)) {
      auto dc = objcProperty->getDeclContext();
      if (auto classDecl = dyn_cast<clang::ObjCInterfaceDecl>(dc)) {
        classDecl->printName(out);
        out << '.';
      } else if (auto categoryDecl = dyn_cast<clang::ObjCCategoryDecl>(dc)) {
        categoryDecl->getClassInterface()->printName(out);
        out << '.';
      } else if (auto proto = dyn_cast<clang::ObjCProtocolDecl>(dc)) {
        proto->printName(out);
        out << '.';
      }
      named->printName(out);
      return;
    }

    named->printName(out);
    return;
  }

  // If this is an anonymous tag declaration with a typedef name, use that.
  if (auto tag = dyn_cast<clang::TagDecl>(named)) {
    if (auto typedefName = tag->getTypedefNameForAnonDecl()) {
      printName(typedefName, out);
      return;
    }
  }
}

void SwiftLookupTable::deserializeAll() {
  if (!Reader) return;

  for (auto baseName : Reader->getBaseNames()) {
    (void)lookup(baseName, None);
  }

  (void)categories();

  for (auto context : Reader->getGlobalsAsMembersContexts()) {
    (void)lookupGlobalsAsMembers(context);
  }
}

/// Print a stored context to the given output stream for debugging purposes.
static void printStoredContext(SwiftLookupTable::StoredContext context,
                               llvm::raw_ostream &out) {
  switch (context.first) {
  case SwiftLookupTable::ContextKind::TranslationUnit:
    out << "TU";
    break;

  case SwiftLookupTable::ContextKind::Tag:
  case SwiftLookupTable::ContextKind::ObjCClass:
  case SwiftLookupTable::ContextKind::ObjCProtocol:
  case SwiftLookupTable::ContextKind::Typedef:
    out << context.second;
    break;
  }
}

/// Print a stored entry (Clang macro or declaration) for debugging purposes.
static void printStoredEntry(const SwiftLookupTable *table, uintptr_t entry,
                             llvm::raw_ostream &out) {
  if (SwiftLookupTable::isSerializationIDEntry(entry)) {
    llvm::errs() << (SwiftLookupTable::isMacroEntry(entry) ? "macro" : "decl")
                 << " ID #" << SwiftLookupTable::getSerializationID(entry);
  } else if (SwiftLookupTable::isMacroEntry(entry)) {
    llvm::errs() << "Macro";
  } else {
    auto decl = const_cast<SwiftLookupTable *>(table)->mapStoredDecl(entry);
    printName(decl, llvm::errs());
  }
}

void SwiftLookupTable::dump() const {
  // Dump the base name -> full table entry mappings.
  SmallVector<StringRef, 4> baseNames;
  for (const auto &entry : LookupTable) {
    baseNames.push_back(entry.first);
  }
  llvm::array_pod_sort(baseNames.begin(), baseNames.end());
  llvm::errs() << "Base name -> entry mappings:\n";
  for (auto baseName : baseNames) {
    llvm::errs() << "  " << baseName << ":\n";
    const auto &entries = LookupTable.find(baseName)->second;
    for (const auto &entry : entries) {
      llvm::errs() << "    ";
      printStoredContext(entry.Context, llvm::errs());
      llvm::errs() << ": ";

      interleave(entry.DeclsOrMacros.begin(), entry.DeclsOrMacros.end(),
                 [this](uintptr_t entry) {
                   printStoredEntry(this, entry, llvm::errs());
                 },
                 [] {
                   llvm::errs() << ", ";
                 });
      llvm::errs() << "\n";
    }
  }

  if (!Categories.empty()) {
    llvm::errs() << "Categories: ";
    interleave(Categories.begin(), Categories.end(),
               [](clang::ObjCCategoryDecl *category) {
                 llvm::errs() << category->getClassInterface()->getName()
                              << "(" << category->getName() << ")";
               },
               [] {
                 llvm::errs() << ", ";
               });
    llvm::errs() << "\n";
  } else if (Reader && !Reader->categories().empty()) {
    llvm::errs() << "Categories: ";
    interleave(Reader->categories().begin(), Reader->categories().end(),
               [](clang::serialization::DeclID declID) {
                 llvm::errs() << "decl ID #" << declID;
               },
               [] {
                 llvm::errs() << ", ";
               });
    llvm::errs() << "\n";
  }

  if (!GlobalsAsMembers.empty()) {
    llvm::errs() << "Globals-as-members mapping:\n";
    SmallVector<StoredContext, 4> contexts;
    for (const auto &entry : GlobalsAsMembers) {
      contexts.push_back(entry.first);
    }
    llvm::array_pod_sort(contexts.begin(), contexts.end());
    for (auto context : contexts) {
      llvm::errs() << "  ";
      printStoredContext(context, llvm::errs());
      llvm::errs() << ": ";

      const auto &entries = GlobalsAsMembers.find(context)->second;
      interleave(entries.begin(), entries.end(),
                 [this](uintptr_t entry) {
                   printStoredEntry(this, entry, llvm::errs());
                 },
                 [] {
                   llvm::errs() << ", ";
                 });
      llvm::errs() << "\n";
    }
  }
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------
using llvm::BCArray;
using llvm::BCBlob;
using llvm::BCFixed;
using llvm::BCGenericRecordLayout;
using llvm::BCRecordLayout;
using llvm::BCVBR;

namespace {
  enum RecordTypes {
    /// Record that contains the mapping from base names to entities with that
    /// name.
    BASE_NAME_TO_ENTITIES_RECORD_ID
      = clang::serialization::FIRST_EXTENSION_RECORD_ID,

    /// Record that contains the list of Objective-C category/extension IDs.
    CATEGORIES_RECORD_ID,

    /// Record that contains the mapping from contexts to the list of
    /// globals that will be injected as members into those contexts.
    GLOBALS_AS_MEMBERS_RECORD_ID
  };

  using BaseNameToEntitiesTableRecordLayout
    = BCRecordLayout<BASE_NAME_TO_ENTITIES_RECORD_ID, BCVBR<16>, BCBlob>;

  using CategoriesRecordLayout
    = llvm::BCRecordLayout<CATEGORIES_RECORD_ID, BCBlob>;

  using GlobalsAsMembersTableRecordLayout
    = BCRecordLayout<GLOBALS_AS_MEMBERS_RECORD_ID, BCVBR<16>, BCBlob>;

  /// Trait used to write the on-disk hash table for the base name -> entities
  /// mapping.
  class BaseNameToEntitiesTableWriterInfo {
    SwiftLookupTable &Table;
    clang::ASTWriter &Writer;

  public:
    using key_type = StringRef;
    using key_type_ref = key_type;
    using data_type = SmallVector<SwiftLookupTable::FullTableEntry, 2>;
    using data_type_ref = data_type &;
    using hash_value_type = uint32_t;
    using offset_type = unsigned;

    BaseNameToEntitiesTableWriterInfo(SwiftLookupTable &table,
                                      clang::ASTWriter &writer)
      : Table(table), Writer(writer)
    {
    }

    hash_value_type ComputeHash(key_type_ref key) {
      return llvm::HashString(key);
    }

    std::pair<unsigned, unsigned> EmitKeyDataLength(raw_ostream &out,
                                                    key_type_ref key,
                                                    data_type_ref data) {
      // The length of the key.
      uint32_t keyLength = key.size();

      // # of entries
      uint32_t dataLength = sizeof(uint16_t);

      // Storage per entry.
      for (const auto &entry : data) {
        // Context info.
        dataLength += 1;
        if (SwiftLookupTable::contextRequiresName(entry.Context.first)) {
          dataLength += sizeof(uint16_t) + entry.Context.second.size();
        }

        // # of entries.
        dataLength += sizeof(uint16_t);

        // Actual entries.
        dataLength += (sizeof(clang::serialization::DeclID) *
                       entry.DeclsOrMacros.size());
      }

      endian::Writer<little> writer(out);
      writer.write<uint16_t>(keyLength);
      writer.write<uint16_t>(dataLength);
      return { keyLength, dataLength };
    }

    void EmitKey(raw_ostream &out, key_type_ref key, unsigned len) {
      out << key;
    }

    void EmitData(raw_ostream &out, key_type_ref key, data_type_ref data,
                  unsigned len) {
      endian::Writer<little> writer(out);

      // # of entries
      writer.write<uint16_t>(data.size());

      for (auto &fullEntry : data) {
        // Context.
        writer.write<uint8_t>(static_cast<uint8_t>(fullEntry.Context.first));
        if (SwiftLookupTable::contextRequiresName(fullEntry.Context.first)) {
          writer.write<uint16_t>(fullEntry.Context.second.size());
          out << fullEntry.Context.second;
        }

        // # of entries.
        writer.write<uint16_t>(fullEntry.DeclsOrMacros.size());

        // Write the declarations and macros.
        for (auto &entry : fullEntry.DeclsOrMacros) {
          uint32_t id;
          if (SwiftLookupTable::isDeclEntry(entry)) {
            auto decl = Table.mapStoredDecl(entry);
            id = (Writer.getDeclID(decl) << 2) | 0x02;
          } else {
            auto macro = Table.mapStoredMacro(entry);
            id = (Writer.getMacroID(macro) << 2) | 0x02 | 0x01;
          }
          writer.write<uint32_t>(id);
        }
      }
    }
  };

  /// Trait used to write the on-disk hash table for the
  /// globals-as-members mapping.
  class GlobalsAsMembersTableWriterInfo {
    SwiftLookupTable &Table;
    clang::ASTWriter &Writer;

  public:
    using key_type = std::pair<SwiftLookupTable::ContextKind, StringRef>;
    using key_type_ref = key_type;
    using data_type = SmallVector<uintptr_t, 2>;
    using data_type_ref = data_type &;
    using hash_value_type = uint32_t;
    using offset_type = unsigned;

    GlobalsAsMembersTableWriterInfo(SwiftLookupTable &table,
                                    clang::ASTWriter &writer)
      : Table(table), Writer(writer)
    {
    }

    hash_value_type ComputeHash(key_type_ref key) {
      return static_cast<unsigned>(key.first) + llvm::HashString(key.second);
    }

    std::pair<unsigned, unsigned> EmitKeyDataLength(raw_ostream &out,
                                                    key_type_ref key,
                                                    data_type_ref data) {
      // The length of the key.
      uint32_t keyLength = 1;
      if (SwiftLookupTable::contextRequiresName(key.first))
        keyLength += key.second.size();

      // # of entries
      uint32_t dataLength =
        sizeof(uint16_t) + sizeof(clang::serialization::DeclID) * data.size();

      endian::Writer<little> writer(out);
      writer.write<uint16_t>(keyLength);
      writer.write<uint16_t>(dataLength);
      return { keyLength, dataLength };
    }

    void EmitKey(raw_ostream &out, key_type_ref key, unsigned len) {
      endian::Writer<little> writer(out);
      writer.write<uint8_t>(static_cast<unsigned>(key.first) - 2);
      if (SwiftLookupTable::contextRequiresName(key.first))
        out << key.second;
    }

    void EmitData(raw_ostream &out, key_type_ref key, data_type_ref data,
                  unsigned len) {
      endian::Writer<little> writer(out);

      // # of entries
      writer.write<uint16_t>(data.size());

      // Actual entries.
      for (auto &entry : data) {
        uint32_t id;
        if (SwiftLookupTable::isDeclEntry(entry)) {
          auto decl = Table.mapStoredDecl(entry);
          id = (Writer.getDeclID(decl) << 2) | 0x02;
        } else {
          auto macro = Table.mapStoredMacro(entry);
          id = (Writer.getMacroID(macro) << 2) | 0x02 | 0x01;
        }
        writer.write<uint32_t>(id);
      }
    }
  };
}

void SwiftLookupTableWriter::writeExtensionContents(
       clang::Sema &sema,
       llvm::BitstreamWriter &stream) {
  // Populate the lookup table.
  SwiftLookupTable table(nullptr);
  PopulateTable(sema, table);

  SmallVector<uint64_t, 64> ScratchRecord;

  // First, gather the sorted list of base names.
  SmallVector<StringRef, 2> baseNames;
  for (const auto &entry : table.LookupTable)
    baseNames.push_back(entry.first);
  llvm::array_pod_sort(baseNames.begin(), baseNames.end());

  // Form the mapping from base names to entities with their context.
  {
    llvm::SmallString<4096> hashTableBlob;
    uint32_t tableOffset;
    {
      llvm::OnDiskChainedHashTableGenerator<BaseNameToEntitiesTableWriterInfo>
        generator;
      BaseNameToEntitiesTableWriterInfo info(table, Writer);
      for (auto baseName : baseNames)
        generator.insert(baseName, table.LookupTable[baseName], info);

      llvm::raw_svector_ostream blobStream(hashTableBlob);
      // Make sure that no bucket is at offset 0
      endian::Writer<little>(blobStream).write<uint32_t>(0);
      tableOffset = generator.Emit(blobStream, info);
    }

    BaseNameToEntitiesTableRecordLayout layout(stream);
    layout.emit(ScratchRecord, tableOffset, hashTableBlob);
  }

  // Write the categories, if there are any.
  if (!table.Categories.empty()) {
    SmallVector<clang::serialization::DeclID, 4> categoryIDs;
    for (auto category : table.Categories) {
      categoryIDs.push_back(Writer.getDeclID(category));
    }

    StringRef blob(reinterpret_cast<const char *>(categoryIDs.data()),
                   categoryIDs.size() * sizeof(clang::serialization::DeclID));
    CategoriesRecordLayout layout(stream);
    layout.emit(ScratchRecord, blob);
  }

  // Write the globals-as-members table, if non-empty.
  if (!table.GlobalsAsMembers.empty()) {
    // Sort the keys.
    SmallVector<SwiftLookupTable::StoredContext, 4> contexts;
    for (const auto &entry : table.GlobalsAsMembers) {
      contexts.push_back(entry.first);
    }
    llvm::array_pod_sort(contexts.begin(), contexts.end());

    // Create the on-disk hash table.
    llvm::SmallString<4096> hashTableBlob;
    uint32_t tableOffset;
    {
      llvm::OnDiskChainedHashTableGenerator<GlobalsAsMembersTableWriterInfo>
        generator;
      GlobalsAsMembersTableWriterInfo info(table, Writer);
      for (auto context : contexts)
        generator.insert(context, table.GlobalsAsMembers[context], info);

      llvm::raw_svector_ostream blobStream(hashTableBlob);
      // Make sure that no bucket is at offset 0
      endian::Writer<little>(blobStream).write<uint32_t>(0);
      tableOffset = generator.Emit(blobStream, info);
    }

    GlobalsAsMembersTableRecordLayout layout(stream);
    layout.emit(ScratchRecord, tableOffset, hashTableBlob);
  }
}

namespace {
  /// Used to deserialize the on-disk base name -> entities table.
  class BaseNameToEntitiesTableReaderInfo {
  public:
    using internal_key_type = StringRef;
    using external_key_type = internal_key_type;
    using data_type = SmallVector<SwiftLookupTable::FullTableEntry, 2>;
    using hash_value_type = uint32_t;
    using offset_type = unsigned;

    internal_key_type GetInternalKey(external_key_type key) {
      return key;
    }

    external_key_type GetExternalKey(internal_key_type key) {
      return key;
    }

    hash_value_type ComputeHash(internal_key_type key) {
      return llvm::HashString(key);
    }

    static bool EqualKey(internal_key_type lhs, internal_key_type rhs) {
      return lhs == rhs;
    }

    static std::pair<unsigned, unsigned>
    ReadKeyDataLength(const uint8_t *&data) {
      unsigned keyLength = endian::readNext<uint16_t, little, unaligned>(data);
      unsigned dataLength = endian::readNext<uint16_t, little, unaligned>(data);
      return { keyLength, dataLength };
    }

    static internal_key_type ReadKey(const uint8_t *data, unsigned length) {
      return StringRef((const char *)data, length);
    }

    static data_type ReadData(internal_key_type key, const uint8_t *data,
                              unsigned length) {
      data_type result;

      // # of entries.
      unsigned numEntries = endian::readNext<uint16_t, little, unaligned>(data);
      result.reserve(numEntries);

      // Read all of the entries.
      while (numEntries--) {
        SwiftLookupTable::FullTableEntry entry;

        // Read the context.
        entry.Context.first =
          static_cast<SwiftLookupTable::ContextKind>(
            endian::readNext<uint8_t, little, unaligned>(data));
        if (SwiftLookupTable::contextRequiresName(entry.Context.first)) {
          uint16_t length = endian::readNext<uint16_t, little, unaligned>(data);
          entry.Context.second = StringRef((const char *)data, length);
          data += length;
        }

        // Read the declarations and macros.
        unsigned numDeclsOrMacros =
          endian::readNext<uint16_t, little, unaligned>(data);
        while (numDeclsOrMacros--) {
          auto id = endian::readNext<uint32_t, little, unaligned>(data);
          entry.DeclsOrMacros.push_back(id);
        }

        result.push_back(entry);
      }

      return result;
    }
  };

  /// Used to deserialize the on-disk globals-as-members table.
  class GlobalsAsMembersTableReaderInfo {
  public:
    using internal_key_type = SwiftLookupTable::StoredContext;
    using external_key_type = internal_key_type;
    using data_type = SmallVector<uintptr_t, 2>;
    using hash_value_type = uint32_t;
    using offset_type = unsigned;

    internal_key_type GetInternalKey(external_key_type key) {
      return key;
    }

    external_key_type GetExternalKey(internal_key_type key) {
      return key;
    }

    hash_value_type ComputeHash(internal_key_type key) {
      return static_cast<unsigned>(key.first) + llvm::HashString(key.second);
    }

    static bool EqualKey(internal_key_type lhs, internal_key_type rhs) {
      return lhs == rhs;
    }

    static std::pair<unsigned, unsigned>
    ReadKeyDataLength(const uint8_t *&data) {
      unsigned keyLength = endian::readNext<uint16_t, little, unaligned>(data);
      unsigned dataLength = endian::readNext<uint16_t, little, unaligned>(data);
      return { keyLength, dataLength };
    }

    static internal_key_type ReadKey(const uint8_t *data, unsigned length) {
      return internal_key_type(
               static_cast<SwiftLookupTable::ContextKind>(*data + 2),
               StringRef((const char *)data + 1, length - 1));
    }

    static data_type ReadData(internal_key_type key, const uint8_t *data,
                              unsigned length) {
      data_type result;

      // # of entries.
      unsigned numEntries = endian::readNext<uint16_t, little, unaligned>(data);
      result.reserve(numEntries);

      // Read all of the entries.
      while (numEntries--) {
        auto id = endian::readNext<uint32_t, little, unaligned>(data);
        result.push_back(id);
      }

      return result;
    }
  };
}

namespace swift {
  using SerializedBaseNameToEntitiesTable =
    llvm::OnDiskIterableChainedHashTable<BaseNameToEntitiesTableReaderInfo>;

  using SerializedGlobalsAsMembersTable =
    llvm::OnDiskIterableChainedHashTable<GlobalsAsMembersTableReaderInfo>;
}

clang::NamedDecl *SwiftLookupTable::mapStoredDecl(uintptr_t &entry) {
  assert(isDeclEntry(entry) && "Not a declaration entry");

  // If we have an AST node here, just cast it.
  if (isASTNodeEntry(entry)) {
    return static_cast<clang::NamedDecl *>(getPointerFromEntry(entry));
  }

  // Otherwise, resolve the declaration.
  assert(Reader && "Cannot resolve the declaration without a reader");
  clang::serialization::DeclID declID = getSerializationID(entry);
  auto decl = cast_or_null<clang::NamedDecl>(
                Reader->getASTReader().GetLocalDecl(Reader->getModuleFile(),
                                                    declID));

  // Update the entry now that we've resolved the declaration.
  entry = encodeEntry(decl);
  return decl;
}

clang::MacroInfo *SwiftLookupTable::mapStoredMacro(uintptr_t &entry) {
  assert(isMacroEntry(entry) && "Not a macro entry");

  // If we have an AST node here, just cast it.
  if (isASTNodeEntry(entry)) {
    return static_cast<clang::MacroInfo *>(getPointerFromEntry(entry));
  }

  // Otherwise, resolve the macro.
  assert(Reader && "Cannot resolve the macro without a reader");
  clang::serialization::MacroID macroID = getSerializationID(entry);
  auto macro = cast_or_null<clang::MacroInfo>(
                Reader->getASTReader().getMacro(
                  Reader->getASTReader().getGlobalMacroID(
                    Reader->getModuleFile(),
                    macroID)));

  // Update the entry now that we've resolved the macro.
  entry = encodeEntry(macro);
  return macro;
}

SwiftLookupTable::SingleEntry SwiftLookupTable::mapStored(uintptr_t &entry) {
  if (isDeclEntry(entry))
    return mapStoredDecl(entry);

  return mapStoredMacro(entry);
}

SwiftLookupTableReader::~SwiftLookupTableReader() {
  OnRemove();
  delete static_cast<SerializedBaseNameToEntitiesTable *>(SerializedTable);
  delete static_cast<SerializedGlobalsAsMembersTable *>(GlobalsAsMembersTable);
}

std::unique_ptr<SwiftLookupTableReader>
SwiftLookupTableReader::create(clang::ModuleFileExtension *extension,
                               clang::ASTReader &reader,
                               clang::serialization::ModuleFile &moduleFile,
                               std::function<void()> onRemove,
                               const llvm::BitstreamCursor &stream)
{
  // Look for the base name -> entities table record.
  SmallVector<uint64_t, 64> scratch;
  auto cursor = stream;
  auto next = cursor.advance();
  std::unique_ptr<SerializedBaseNameToEntitiesTable> serializedTable;
  std::unique_ptr<SerializedGlobalsAsMembersTable> globalsAsMembersTable;
  ArrayRef<clang::serialization::DeclID> categories;
  while (next.Kind != llvm::BitstreamEntry::EndBlock) {
    if (next.Kind == llvm::BitstreamEntry::Error)
      return nullptr;

    if (next.Kind == llvm::BitstreamEntry::SubBlock) {
      // Unknown sub-block, possibly for use by a future version of the
      // API notes format.
      if (cursor.SkipBlock())
        return nullptr;
      
      next = cursor.advance();
      continue;
    }

    scratch.clear();
    StringRef blobData;
    unsigned kind = cursor.readRecord(next.ID, scratch, &blobData);
    switch (kind) {
    case BASE_NAME_TO_ENTITIES_RECORD_ID: {
      // Already saw base name -> entities table.
      if (serializedTable)
        return nullptr;

      uint32_t tableOffset;
      BaseNameToEntitiesTableRecordLayout::readRecord(scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(blobData.data());

      serializedTable.reset(
        SerializedBaseNameToEntitiesTable::Create(base + tableOffset,
                                                  base + sizeof(uint32_t),
                                                  base));
      break;
    }

    case CATEGORIES_RECORD_ID: {
      // Already saw categories; input is malformed.
      if (!categories.empty()) return nullptr;

      auto start =
        reinterpret_cast<const clang::serialization::DeclID *>(blobData.data());
      unsigned numElements
        = blobData.size() / sizeof(clang::serialization::DeclID);
      categories = llvm::makeArrayRef(start, numElements);
      break;
    }

    case GLOBALS_AS_MEMBERS_RECORD_ID: {
      // Already saw globals-as-members table.
      if (globalsAsMembersTable)
        return nullptr;

      uint32_t tableOffset;
      GlobalsAsMembersTableRecordLayout::readRecord(scratch, tableOffset);
      auto base = reinterpret_cast<const uint8_t *>(blobData.data());

      globalsAsMembersTable.reset(
        SerializedGlobalsAsMembersTable::Create(base + tableOffset,
                                                base + sizeof(uint32_t),
                                                base));
      break;
    }

    default:
      // Unknown record, possibly for use by a future version of the
      // module format.
      break;
    }

    next = cursor.advance();
  }

  if (!serializedTable) return nullptr;

  // Create the reader.
  return std::unique_ptr<SwiftLookupTableReader>(
           new SwiftLookupTableReader(extension, reader, moduleFile, onRemove,
                                      serializedTable.release(), categories,
                                      globalsAsMembersTable.release()));

}

SmallVector<StringRef, 4> SwiftLookupTableReader::getBaseNames() {
  auto table = static_cast<SerializedBaseNameToEntitiesTable*>(SerializedTable);
  SmallVector<StringRef, 4> results;
  for (auto key : table->keys()) {
    results.push_back(key);
  }
  return results;
}

bool SwiftLookupTableReader::lookup(
       StringRef baseName,
       SmallVectorImpl<SwiftLookupTable::FullTableEntry> &entries) {
  auto table = static_cast<SerializedBaseNameToEntitiesTable*>(SerializedTable);

  // Look for an entry with this base name.
  auto known = table->find(baseName);
  if (known == table->end()) return false;

  // Grab the results.
  entries = std::move(*known);
  return true;
}

SmallVector<SwiftLookupTable::StoredContext, 4>
SwiftLookupTableReader::getGlobalsAsMembersContexts() {
  auto table =
    static_cast<SerializedGlobalsAsMembersTable*>(GlobalsAsMembersTable);

  SmallVector<SwiftLookupTable::StoredContext, 4> results;
  if (!table) return results;

  for (auto key : table->keys()) {
    results.push_back(key);
  }
  return results;
}

bool SwiftLookupTableReader::lookupGlobalsAsMembers(
       SwiftLookupTable::StoredContext context,
       SmallVectorImpl<uintptr_t> &entries) {
  auto table =
    static_cast<SerializedGlobalsAsMembersTable*>(GlobalsAsMembersTable);
  if (!table) return false;

  // Look for an entry with this context name.
  auto known = table->find(context);
  if (known == table->end()) return false;

  // Grab the results.
  entries = std::move(*known);
  return true;
}
