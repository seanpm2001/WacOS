//===--- SwiftLookupTable.cpp - Swift Lookup Table ------------------------===//
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
// This file implements support for Swift name lookup tables stored in Clang
// modules.
//
//===----------------------------------------------------------------------===//
#include "ImporterImpl.h"
#include "SwiftLookupTable.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsClangImporter.h"
#include "swift/Basic/STLExtras.h"
#include "swift/Basic/Version.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
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
using namespace importer;
using namespace llvm::support;

/// Determine whether the new declarations matches an existing declaration.
static bool matchesExistingDecl(clang::Decl *decl, clang::Decl *existingDecl) {
  // If the canonical declarations are equivalent, we have a match.
  if (decl->getCanonicalDecl() == existingDecl->getCanonicalDecl()) {
    return true;
  }

  return false;
}

namespace swift {
/// Module file extension writer for the Swift lookup tables.
class SwiftLookupTableWriter : public clang::ModuleFileExtensionWriter {
  clang::ASTWriter &Writer;

  ASTContext &swiftCtx;
  const PlatformAvailability &availability;
  const bool inferImportAsMember;

public:
  SwiftLookupTableWriter(clang::ModuleFileExtension *extension,
                         clang::ASTWriter &writer, ASTContext &ctx,
                         const PlatformAvailability &avail, bool inferIAM)
      : ModuleFileExtensionWriter(extension), Writer(writer), swiftCtx(ctx),
        availability(avail), inferImportAsMember(inferIAM) {}

  void writeExtensionContents(clang::Sema &sema,
                              llvm::BitstreamWriter &stream) override;

  void populateTable(SwiftLookupTable &table, NameImporter &);
};

/// Module file extension reader for the Swift lookup tables.
class SwiftLookupTableReader : public clang::ModuleFileExtensionReader {
  clang::ASTReader &Reader;
  clang::serialization::ModuleFile &ModuleFile;
  std::function<void()> OnRemove;

  void *SerializedTable;
  ArrayRef<clang::serialization::DeclID> Categories;
  void *GlobalsAsMembersTable;

  SwiftLookupTableReader(clang::ModuleFileExtension *extension,
                         clang::ASTReader &reader,
                         clang::serialization::ModuleFile &moduleFile,
                         std::function<void()> onRemove, void *serializedTable,
                         ArrayRef<clang::serialization::DeclID> categories,
                         void *globalsAsMembersTable)
      : ModuleFileExtensionReader(extension), Reader(reader),
        ModuleFile(moduleFile), OnRemove(onRemove),
        SerializedTable(serializedTable), Categories(categories),
        GlobalsAsMembersTable(globalsAsMembersTable) {}

public:
  /// Create a new lookup table reader for the given AST reader and stream
  /// position.
  static std::unique_ptr<SwiftLookupTableReader>
  create(clang::ModuleFileExtension *extension, clang::ASTReader &reader,
         clang::serialization::ModuleFile &moduleFile,
         std::function<void()> onRemove, const llvm::BitstreamCursor &stream);

  ~SwiftLookupTableReader() override;

  /// Retrieve the AST reader associated with this lookup table reader.
  clang::ASTReader &getASTReader() const { return Reader; }

  /// Retrieve the module file associated with this lookup table reader.
  clang::serialization::ModuleFile &getModuleFile() { return ModuleFile; }

  /// Retrieve the set of base names that are stored in the on-disk hash table.
  SmallVector<SerializedSwiftName, 4> getBaseNames();

  /// Retrieve the set of entries associated with the given base name.
  ///
  /// \returns true if we found anything, false otherwise.
  bool lookup(SerializedSwiftName baseName,
              SmallVectorImpl<SwiftLookupTable::FullTableEntry> &entries);

  /// Retrieve the declaration IDs of the categories.
  ArrayRef<clang::serialization::DeclID> categories() const {
    return Categories;
  }

  /// Retrieve the set of contexts that have globals-as-members
  /// injected into them.
  SmallVector<SwiftLookupTable::StoredContext, 4> getGlobalsAsMembersContexts();

  /// Retrieve the set of global declarations that are going to be
  /// imported as members into the given context.
  ///
  /// \returns true if we found anything, false otherwise.
  bool lookupGlobalsAsMembers(SwiftLookupTable::StoredContext context,
                              SmallVectorImpl<uint64_t> &entries);
};
} // namespace swift

DeclBaseName SerializedSwiftName::toDeclBaseName(ASTContext &Context) const {
  switch (Kind) {
  case DeclBaseName::Kind::Normal:
    return Context.getIdentifier(Name);
  case DeclBaseName::Kind::Subscript:
    return DeclBaseName::createSubscript();
  case DeclBaseName::Kind::Destructor:
    return DeclBaseName::createDestructor();
  }
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

  llvm_unreachable("Invalid ContextKind.");
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

  llvm_unreachable("Invalid EffectiveClangContext.");
}

/// Lookup an unresolved context name and resolve it to a Clang
/// declaration context or typedef name.
clang::NamedDecl *SwiftLookupTable::resolveContext(StringRef unresolvedName) {
  // Look for a context with the given Swift name.
  for (auto entry :
       lookup(SerializedSwiftName(unresolvedName),
              std::make_pair(ContextKind::TranslationUnit, StringRef()))) {
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
  // Force deserialization to occur before appending.
  (void) categories();

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

  // Enumerators have the translation unit as their redeclaration context,
  // but members of anonymous enums are still allowed to be in the
  // global-as-member category.
  if (isa<clang::EnumConstantDecl>(decl)) {
    const auto *theEnum = cast<clang::EnumDecl>(decl->getDeclContext());
    return !theEnum->hasNameForLinkage();
  }

  // If the redeclaration context is namespace-scope, then we're
  // mapping as a member.
  return decl->getDeclContext()->getRedeclContext()->isFileContext();
}

bool SwiftLookupTable::addLocalEntry(SingleEntry newEntry,
                                     SmallVectorImpl<uint64_t> &entries) {
  // Check whether this entry matches any existing entry.
  auto decl = newEntry.dyn_cast<clang::NamedDecl *>();
  auto macro = newEntry.dyn_cast<clang::MacroInfo *>();
  auto moduleMacro = newEntry.dyn_cast<clang::ModuleMacro *>();

  for (auto &existingEntry : entries) {
    // If it matches an existing declaration, there's nothing to do.
    if (decl && isDeclEntry(existingEntry) &&
        matchesExistingDecl(decl, mapStoredDecl(existingEntry)))
      return false;

    // If a textual macro matches an existing macro, just drop the new
    // definition.
    if (macro && isMacroEntry(existingEntry)) {
      return false;
    }

    // If a module macro matches an existing macro, be a bit more discerning.
    //
    // Specifically, if the innermost explicit submodule containing the new
    // macro contains the innermost explicit submodule containing the existing
    // macro, the new one should replace the old one; if they're the same
    // module, the old one should stay in place. Otherwise, they don't share an
    // explicit module, and should be considered alternatives.
    //
    // Note that the above assumes that macro definitions are processed in
    // reverse order, i.e. the first definition seen is the last in a
    // translation unit.
    if (moduleMacro && isMacroEntry(existingEntry)) {
      SingleEntry decodedEntry = mapStoredMacro(existingEntry,
                                                /*assumeModule*/true);
      const auto *existingMacro = decodedEntry.get<clang::ModuleMacro *>();

      const clang::Module *newModule = moduleMacro->getOwningModule();
      const clang::Module *existingModule = existingMacro->getOwningModule();

      // A simple redeclaration: drop the new definition.
      if (existingModule == newModule)
        return false;

      // A broader-scoped redeclaration: drop the old definition.
      if (existingModule->isSubModuleOf(newModule)) {
        // FIXME: What if there are /multiple/ old definitions we should be
        // dropping? What if one of the earlier early exits makes us miss
        // entries later in the list that would match this?
        existingEntry = encodeEntry(moduleMacro);
        return false;
      }

      // Otherwise, just allow both definitions to coexist.
    }
  }

  // Add an entry to this context.
  if (decl)
    entries.push_back(encodeEntry(decl));
  else if (macro)
    entries.push_back(encodeEntry(macro));
  else
    entries.push_back(encodeEntry(moduleMacro));
  return true;
}

void SwiftLookupTable::addEntry(DeclName name, SingleEntry newEntry,
                                EffectiveClangContext effectiveContext) {
  assert(newEntry);

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

  // Populate cache from reader if necessary.
  findOrCreate(name.getBaseName());

  auto context = *contextOpt;

  // If this is a global imported as a member, record is as such.
  if (isGlobalAsMember(newEntry, context)) {
    auto &entries = GlobalsAsMembers[context];
    (void)addLocalEntry(newEntry, entries);
  }

  // Find the list of entries for this base name.
  auto &entries = LookupTable[name.getBaseName()];
  auto decl = newEntry.dyn_cast<clang::NamedDecl *>();
  auto macro = newEntry.dyn_cast<clang::MacroInfo *>();
  auto moduleMacro = newEntry.dyn_cast<clang::ModuleMacro *>();
  for (auto &entry : entries) {
    if (entry.Context == context) {
      // We have entries for this context.
      (void)addLocalEntry(newEntry, entry.DeclsOrMacros);
      return;
    }
  }

  // This is a new context for this name. Add it.
  FullTableEntry entry;
  entry.Context = context;
  if (decl)
    entry.DeclsOrMacros.push_back(encodeEntry(decl));
  else if (macro)
    entry.DeclsOrMacros.push_back(encodeEntry(macro));
  else
    entry.DeclsOrMacros.push_back(encodeEntry(moduleMacro));
  entries.push_back(entry);
}

auto SwiftLookupTable::findOrCreate(SerializedSwiftName baseName)
    -> llvm::DenseMap<SerializedSwiftName,
                      SmallVector<FullTableEntry, 2>>::iterator {
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
SwiftLookupTable::lookup(SerializedSwiftName baseName,
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
      if (auto entry = mapStored(stored))
        result.push_back(entry);
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
    SmallVector<uint64_t, 2> results;
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
SwiftLookupTable::lookup(SerializedSwiftName baseName,
                         EffectiveClangContext searchContext) {
  // Translate context.
  Optional<StoredContext> context;
  if (searchContext) {
    context = translateContext(searchContext);
    if (!context) return { };
  }

  return lookup(baseName, context);
}

SmallVector<SerializedSwiftName, 4> SwiftLookupTable::allBaseNames() {
  // If we have a reader, enumerate its base names.
  if (Reader) return Reader->getBaseNames();

  // Otherwise, walk the lookup table.
  SmallVector<SerializedSwiftName, 4> result;
  for (const auto &entry : LookupTable) {
    result.push_back(entry.first);
  }
  return result;
}

SmallVector<clang::NamedDecl *, 4>
SwiftLookupTable::lookupObjCMembers(SerializedSwiftName baseName) {
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

static uint32_t getEncodedDeclID(uint64_t entry) {
  assert(SwiftLookupTable::isSerializationIDEntry(entry)); 
  assert(SwiftLookupTable::isDeclEntry(entry));
  return entry >> 2;
}

namespace {
struct LocalMacroIDs {
  uint32_t moduleID;
  uint32_t nameOrMacroID;
};
}

static LocalMacroIDs getEncodedModuleMacroIDs(uint64_t entry) {
  assert(SwiftLookupTable::isSerializationIDEntry(entry)); 
  assert(SwiftLookupTable::isMacroEntry(entry));
  return {static_cast<uint32_t>((entry & 0xFFFFFFFF) >> 2),
          static_cast<uint32_t>(entry >> 32)};
}

/// Print a stored entry (Clang macro or declaration) for debugging purposes.
static void printStoredEntry(const SwiftLookupTable *table, uint64_t entry,
                             llvm::raw_ostream &out) {
  if (SwiftLookupTable::isSerializationIDEntry(entry)) {
    if (SwiftLookupTable::isDeclEntry(entry)) {
      llvm::errs() << "decl ID #" << getEncodedDeclID(entry);
    } else {
      LocalMacroIDs macroIDs = getEncodedModuleMacroIDs(entry);
      if (macroIDs.moduleID == 0) {
        llvm::errs() << "macro ID #" << macroIDs.nameOrMacroID;
      } else {
        llvm::errs() << "macro with name ID #" << macroIDs.nameOrMacroID
                     << "in submodule #" << macroIDs.moduleID;
      }
    }
  } else if (SwiftLookupTable::isMacroEntry(entry)) {
    llvm::errs() << "Macro";
  } else {
    auto decl = const_cast<SwiftLookupTable *>(table)->mapStoredDecl(entry);
    printName(decl, llvm::errs());
  }
}

void SwiftLookupTable::dump() const {
  // Dump the base name -> full table entry mappings.
  SmallVector<SerializedSwiftName, 4> baseNames;
  for (const auto &entry : LookupTable) {
    baseNames.push_back(entry.first);
  }
  llvm::array_pod_sort(baseNames.begin(), baseNames.end());
  llvm::errs() << "Base name -> entry mappings:\n";
  for (auto baseName : baseNames) {
    switch (baseName.Kind) {
    case DeclBaseName::Kind::Normal:
      llvm::errs() << "  " << baseName.Name << ":\n";
      break;
    case DeclBaseName::Kind::Subscript:
      llvm::errs() << "  subscript:\n";
      break;
    case DeclBaseName::Kind::Destructor:
      llvm::errs() << "  deinit:\n";
      break;
    }
    const auto &entries = LookupTable.find(baseName)->second;
    for (const auto &entry : entries) {
      llvm::errs() << "    ";
      printStoredContext(entry.Context, llvm::errs());
      llvm::errs() << ": ";

      interleave(entry.DeclsOrMacros.begin(), entry.DeclsOrMacros.end(),
                 [this](uint64_t entry) {
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
                 [this](uint64_t entry) {
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
    static_assert(sizeof(DeclBaseName::Kind) <= sizeof(uint8_t),
                  "kind serialized as uint8_t");

    SwiftLookupTable &Table;
    clang::ASTWriter &Writer;

  public:
    using key_type = SerializedSwiftName;
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
      return llvm::DenseMapInfo<SerializedSwiftName>::getHashValue(key);
    }

    std::pair<unsigned, unsigned> EmitKeyDataLength(raw_ostream &out,
                                                    key_type_ref key,
                                                    data_type_ref data) {
      uint32_t keyLength = sizeof(uint8_t); // For the flag of the name's kind
      if (key.Kind == DeclBaseName::Kind::Normal) {
        keyLength += key.Name.size(); // The name's length
      }

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
        dataLength += (sizeof(uint64_t) * entry.DeclsOrMacros.size());
      }

      endian::Writer<little> writer(out);
      writer.write<uint16_t>(keyLength);
      writer.write<uint16_t>(dataLength);
      return { keyLength, dataLength };
    }

    void EmitKey(raw_ostream &out, key_type_ref key, unsigned len) {
      endian::Writer<little> writer(out);
      writer.write<uint8_t>((uint8_t)key.Kind);
      if (key.Kind == swift::DeclBaseName::Kind::Normal)
        writer.OS << key.Name;
    }

    void EmitData(raw_ostream &out, key_type_ref key, data_type_ref data,
                  unsigned len) {
      endian::Writer<little> writer(out);

      // # of entries
      writer.write<uint16_t>(data.size());

      bool isModule = Writer.getLangOpts().isCompilingModule();
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
          uint64_t id;
          auto mappedEntry = Table.mapStored(entry, isModule);
          if (auto *decl = mappedEntry.dyn_cast<clang::NamedDecl *>()) {
            id = (Writer.getDeclID(decl) << 2) | 0x02;
          } else if (auto *macro = mappedEntry.dyn_cast<clang::MacroInfo *>()) {
            id = static_cast<uint64_t>(Writer.getMacroID(macro)) << 32;
            id |= 0x02 | 0x01;
          } else {
            auto *moduleMacro = mappedEntry.get<clang::ModuleMacro *>();
            uint32_t nameID = Writer.getIdentifierRef(moduleMacro->getName());
            uint32_t submoduleID = Writer.getLocalOrImportedSubmoduleID(
                moduleMacro->getOwningModule());
            id = (static_cast<uint64_t>(nameID) << 32) | (submoduleID << 2);
            id |= 0x02 | 0x01;
          }
          writer.write<uint64_t>(id);
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
    using data_type = SmallVector<uint64_t, 2>;
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
        sizeof(uint16_t) + sizeof(uint64_t) * data.size();

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
      bool isModule = Writer.getLangOpts().isCompilingModule();
      for (auto &entry : data) {
        uint64_t id;
        auto mappedEntry = Table.mapStored(entry, isModule);
        if (auto *decl = mappedEntry.dyn_cast<clang::NamedDecl *>()) {
          id = (Writer.getDeclID(decl) << 2) | 0x02;
        } else if (auto *macro = mappedEntry.dyn_cast<clang::MacroInfo *>()) {
          id = static_cast<uint64_t>(Writer.getMacroID(macro)) << 32;
          id |= 0x02 | 0x01;
        } else {
          auto *moduleMacro = mappedEntry.get<clang::ModuleMacro *>();
          uint32_t nameID = Writer.getIdentifierRef(moduleMacro->getName());
          uint32_t submoduleID = Writer.getLocalOrImportedSubmoduleID(
              moduleMacro->getOwningModule());
          id = (static_cast<uint64_t>(nameID) << 32) | (submoduleID << 2);
          id |= 0x02 | 0x01;
        }
        writer.write<uint64_t>(id);
      }
    }
  };
} // end anonymous namespace

void SwiftLookupTableWriter::writeExtensionContents(
       clang::Sema &sema,
       llvm::BitstreamWriter &stream) {
  NameImporter nameImporter(swiftCtx, availability, sema, inferImportAsMember);

  // Populate the lookup table.
  SwiftLookupTable table(nullptr);
  populateTable(table, nameImporter);

  SmallVector<uint64_t, 64> ScratchRecord;

  // First, gather the sorted list of base names.
  SmallVector<SerializedSwiftName, 2> baseNames;
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
    using internal_key_type = SerializedSwiftName;
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
      return llvm::DenseMapInfo<SerializedSwiftName>::getHashValue(key);
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
      uint8_t kind = endian::readNext<uint8_t, little, unaligned>(data);
      switch (kind) {
      case (uint8_t)DeclBaseName::Kind::Normal: {
        StringRef str(reinterpret_cast<const char *>(data),
                      length - sizeof(uint8_t));
        return SerializedSwiftName(str);
      }
      case (uint8_t)DeclBaseName::Kind::Subscript:
        return SerializedSwiftName(DeclBaseName::Kind::Subscript);
      default:
        llvm_unreachable("Unknown kind for DeclBaseName");
      }
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
          auto id = endian::readNext<uint64_t, little, unaligned>(data);
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
    using data_type = SmallVector<uint64_t, 2>;
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
        auto id = endian::readNext<uint64_t, little, unaligned>(data);
        result.push_back(id);
      }

      return result;
    }
  };
} // end anonymous namespace

namespace swift {
  using SerializedBaseNameToEntitiesTable =
    llvm::OnDiskIterableChainedHashTable<BaseNameToEntitiesTableReaderInfo>;

  using SerializedGlobalsAsMembersTable =
    llvm::OnDiskIterableChainedHashTable<GlobalsAsMembersTableReaderInfo>;
} // namespace swift

clang::NamedDecl *SwiftLookupTable::mapStoredDecl(uint64_t &entry) {
  assert(isDeclEntry(entry) && "Not a declaration entry");

  // If we have an AST node here, just cast it.
  if (isASTNodeEntry(entry)) {
    return static_cast<clang::NamedDecl *>(getPointerFromEntry(entry));
  }

  // Otherwise, resolve the declaration.
  assert(Reader && "Cannot resolve the declaration without a reader");
  uint32_t declID = getEncodedDeclID(entry);
  auto decl = cast_or_null<clang::NamedDecl>(
                Reader->getASTReader().GetLocalDecl(Reader->getModuleFile(),
                                                    declID));

  // Update the entry now that we've resolved the declaration.
  entry = encodeEntry(decl);
  return decl;
}

static bool isPCH(SwiftLookupTableReader &reader) {
  return reader.getModuleFile().Kind == clang::serialization::MK_PCH;
}

SwiftLookupTable::SingleEntry
SwiftLookupTable::mapStoredMacro(uint64_t &entry, bool assumeModule) {
  assert(isMacroEntry(entry) && "Not a macro entry");

  // If we have an AST node here, just cast it.
  if (isASTNodeEntry(entry)) {
    if (assumeModule || (Reader && !isPCH(*Reader)))
      return static_cast<clang::ModuleMacro *>(getPointerFromEntry(entry));
    else
      return static_cast<clang::MacroInfo *>(getPointerFromEntry(entry));
  }

  // Otherwise, resolve the macro.
  assert(Reader && "Cannot resolve the macro without a reader");
  clang::ASTReader &astReader = Reader->getASTReader();

  LocalMacroIDs macroIDs = getEncodedModuleMacroIDs(entry);
  if (!assumeModule && macroIDs.moduleID == 0) {
    assert(isPCH(*Reader));
    // Not a module, and the second key is actually a macroID.
    auto macro =
        astReader.getMacro(astReader.getGlobalMacroID(Reader->getModuleFile(),
                                                      macroIDs.nameOrMacroID));

    // Update the entry now that we've resolved the macro.
    entry = encodeEntry(macro);
    return macro;
  }

  // FIXME: Clang should help us out here, but it doesn't. It can only give us
  // MacroInfos and not ModuleMacros.
  assert(!isPCH(*Reader));
  clang::IdentifierInfo *name =
      astReader.getLocalIdentifier(Reader->getModuleFile(), 
                                   macroIDs.nameOrMacroID);
  auto submoduleID = astReader.getGlobalSubmoduleID(Reader->getModuleFile(), 
                                                    macroIDs.moduleID);
  clang::Module *submodule = astReader.getSubmodule(submoduleID);
  assert(submodule);

  clang::Preprocessor &pp = Reader->getASTReader().getPreprocessor();
  // Force the ModuleMacro to be loaded if this module is visible.
  (void)pp.getLeafModuleMacros(name);
  clang::ModuleMacro *macro = pp.getModuleMacro(submodule, name);
  // This might still be NULL if the module has been imported but not made
  // visible. We need a better answer here.
  if (macro)
    entry = encodeEntry(macro);
  return macro;
}

SwiftLookupTable::SingleEntry SwiftLookupTable::mapStored(uint64_t &entry,
                                                          bool assumeModule) {
  if (isDeclEntry(entry))
    return mapStoredDecl(entry);
  return mapStoredMacro(entry, assumeModule);
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

SmallVector<SerializedSwiftName, 4> SwiftLookupTableReader::getBaseNames() {
  auto table = static_cast<SerializedBaseNameToEntitiesTable*>(SerializedTable);
  SmallVector<SerializedSwiftName, 4> results;
  for (auto key : table->keys()) {
    results.push_back(key);
  }
  return results;
}

bool SwiftLookupTableReader::lookup(
    SerializedSwiftName baseName,
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
       SmallVectorImpl<uint64_t> &entries) {
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

clang::ModuleFileExtensionMetadata
SwiftNameLookupExtension::getExtensionMetadata() const {
  clang::ModuleFileExtensionMetadata metadata;
  metadata.BlockName = "swift.lookup";
  metadata.MajorVersion = SWIFT_LOOKUP_TABLE_VERSION_MAJOR;
  metadata.MinorVersion = SWIFT_LOOKUP_TABLE_VERSION_MINOR;
  metadata.UserInfo =
      version::getSwiftFullVersion(swiftCtx.LangOpts.EffectiveLanguageVersion);
  return metadata;
}

llvm::hash_code
SwiftNameLookupExtension::hashExtension(llvm::hash_code code) const {
  return llvm::hash_combine(code, StringRef("swift.lookup"),
                            SWIFT_LOOKUP_TABLE_VERSION_MAJOR,
                            SWIFT_LOOKUP_TABLE_VERSION_MINOR,
                            inferImportAsMember);
}

void importer::addEntryToLookupTable(SwiftLookupTable &table,
                                     clang::NamedDecl *named,
                                     NameImporter &nameImporter) {
  // Determine whether this declaration is suppressed in Swift.
  if (shouldSuppressDeclImport(named))
    return;

  // Leave incomplete struct/enum/union types out of the table; Swift only
  // handles pointers to them.
  // FIXME: At some point we probably want to be importing incomplete types,
  // so that pointers to different incomplete types themselves have distinct
  // types. At that time it will be necessary to make the decision of whether
  // or not to import an incomplete type declaration based on whether it's
  // actually the struct backing a CF type:
  //
  //    typedef struct CGColor *CGColorRef;
  //
  // The best way to do this is probably to change CFDatabase.def to include
  // struct names when relevant, not just pointer names. That way we can check
  // both CFDatabase.def and the objc_bridge attribute and cover all our bases.
  if (auto *tagDecl = dyn_cast<clang::TagDecl>(named)) {
    if (!tagDecl->getDefinition())
      return;
  }

  // If we have a name to import as, add this entry to the table.
  auto currentVersion =
      ImportNameVersion::fromOptions(nameImporter.getLangOpts());
  if (auto importedName = nameImporter.importName(named, currentVersion)) {
    SmallPtrSet<DeclName, 8> distinctNames;
    distinctNames.insert(importedName.getDeclName());
    table.addEntry(importedName.getDeclName(), named,
                   importedName.getEffectiveContext());

    // Also add the subscript entry, if needed.
    if (importedName.isSubscriptAccessor())
      table.addEntry(DeclName(nameImporter.getContext(),
                              DeclBaseName::createSubscript(),
                              ArrayRef<Identifier>()),
                     named, importedName.getEffectiveContext());

    currentVersion.forEachOtherImportNameVersion(
        [&](ImportNameVersion alternateVersion) {
      auto alternateName = nameImporter.importName(named, alternateVersion);
      if (!alternateName)
        return;
      // FIXME: What if the DeclNames are the same but the contexts are
      // different?
      if (distinctNames.insert(alternateName.getDeclName()).second) {
        table.addEntry(alternateName.getDeclName(), named,
                       alternateName.getEffectiveContext());
      }
    });
  } else if (auto category = dyn_cast<clang::ObjCCategoryDecl>(named)) {
    // If the category is invalid, don't add it.
    if (category->isInvalidDecl())
      return;

    table.addCategory(category);
  }

  // Walk the members of any context that can have nested members.
  if (isa<clang::TagDecl>(named) || isa<clang::ObjCInterfaceDecl>(named) ||
      isa<clang::ObjCProtocolDecl>(named) ||
      isa<clang::ObjCCategoryDecl>(named)) {
    clang::DeclContext *dc = cast<clang::DeclContext>(named);
    for (auto member : dc->decls()) {
      if (auto namedMember = dyn_cast<clang::NamedDecl>(member))
        addEntryToLookupTable(table, namedMember, nameImporter);
    }
  }
}

/// Returns the nearest parent of \p module that is marked \c explicit in its
/// module map. If \p module is itself explicit, it is returned; if no module
/// in the parent chain is explicit, the top-level module is returned.
static const clang::Module *
getExplicitParentModule(const clang::Module *module) {
  while (!module->IsExplicit && module->Parent)
    module = module->Parent;
  return module;
}

void importer::addMacrosToLookupTable(SwiftLookupTable &table,
                                      NameImporter &nameImporter) {
  auto &pp = nameImporter.getClangPreprocessor();
  auto *tu = nameImporter.getClangContext().getTranslationUnitDecl();
  bool isModule = pp.getLangOpts().isCompilingModule();
  for (const auto &macro : pp.macros(false)) {
    auto maybeAddMacro = [&](clang::MacroInfo *info,
                             clang::ModuleMacro *moduleMacro) {
      // If this is a #undef, return.
      if (!info)
        return;

      // If we hit a builtin macro, we're done.
      if (info->isBuiltinMacro())
        return;

      // If we hit a macro with invalid or predefined location, we're done.
      auto loc = info->getDefinitionLoc();
      if (loc.isInvalid())
        return;
      if (pp.getSourceManager().getFileID(loc) == pp.getPredefinesFileID())
        return;

      // If we're in a module, we really need moduleMacro to be valid.
      if (isModule && !moduleMacro) {
#ifndef NDEBUG
        // Refetch this just for the assertion.
        clang::MacroDirective *MD = pp.getLocalMacroDirective(macro.first);
        assert(isa<clang::VisibilityMacroDirective>(MD));
#endif

        // FIXME: "public" visibility macros should actually be added to the 
        // table.
        return;
      }

      // Add this entry.
      auto name = nameImporter.importMacroName(macro.first, info);
      if (name.empty())
        return;
      if (moduleMacro)
        table.addEntry(name, moduleMacro, tu);
      else
        table.addEntry(name, info, tu);
    };

    ArrayRef<clang::ModuleMacro *> moduleMacros =
        macro.second.getActiveModuleMacros(pp, macro.first);
    if (moduleMacros.empty()) {
      // Handle the bridging header case.
      clang::MacroDirective *MD = pp.getLocalMacroDirective(macro.first);
      if (!MD)
        continue;

      maybeAddMacro(MD->getMacroInfo(), nullptr);

    } else {
      clang::Module *currentModule = pp.getCurrentModule();
      SmallVector<clang::ModuleMacro *, 8> worklist;
      llvm::copy_if(moduleMacros, std::back_inserter(worklist),
                    [currentModule](const clang::ModuleMacro *next) -> bool {
        return next->getOwningModule()->isSubModuleOf(currentModule);
      });

      while (!worklist.empty()) {
        clang::ModuleMacro *moduleMacro = worklist.pop_back_val();
        maybeAddMacro(moduleMacro->getMacroInfo(), moduleMacro);

        // Also visit overridden macros that are in a different explicit
        // submodule. This isn't a perfect way to tell if these two macros are
        // supposed to be independent, but it's close enough in practice.
        clang::Module *owningModule = moduleMacro->getOwningModule();
        auto *explicitParent = getExplicitParentModule(owningModule);
        llvm::copy_if(moduleMacro->overrides(), std::back_inserter(worklist),
                      [&](const clang::ModuleMacro *next) -> bool {
          const clang::Module *nextModule =
              getExplicitParentModule(next->getOwningModule());
          if (!nextModule->isSubModuleOf(currentModule))
            return false;
          return nextModule != explicitParent;
        });
      }
    }
  }
}

void importer::finalizeLookupTable(SwiftLookupTable &table,
                                   NameImporter &nameImporter) {
  // Resolve any unresolved entries.
  SmallVector<SwiftLookupTable::SingleEntry, 4> unresolved;
  if (table.resolveUnresolvedEntries(unresolved)) {
    // Complain about unresolved entries that remain.
    for (auto entry : unresolved) {
      auto decl = entry.get<clang::NamedDecl *>();
      auto swiftName = decl->getAttr<clang::SwiftNameAttr>();

      nameImporter.getContext().Diags.diagnose(
          SourceLoc(), diag::unresolvable_clang_decl, decl->getNameAsString(),
          swiftName->getName());
    }
  }
}

void SwiftLookupTableWriter::populateTable(SwiftLookupTable &table,
                                           NameImporter &nameImporter) {
  auto &sema = nameImporter.getClangSema();
  for (auto decl : sema.Context.getTranslationUnitDecl()->noload_decls()) {
    // Skip anything from an AST file.
    if (decl->isFromASTFile())
      continue;

    // Skip non-named declarations.
    auto named = dyn_cast<clang::NamedDecl>(decl);
    if (!named)
      continue;

    // Add this entry to the lookup table.
    addEntryToLookupTable(table, named, nameImporter);
  }

  // Add macros to the lookup table.
  addMacrosToLookupTable(table, nameImporter);

  // Finalize the lookup table, which may fail.
  finalizeLookupTable(table, nameImporter);
};

std::unique_ptr<clang::ModuleFileExtensionWriter>
SwiftNameLookupExtension::createExtensionWriter(clang::ASTWriter &writer) {
  return std::unique_ptr<clang::ModuleFileExtensionWriter>(
      new SwiftLookupTableWriter(this, writer, swiftCtx, availability,
                                 inferImportAsMember));
}

std::unique_ptr<clang::ModuleFileExtensionReader>
SwiftNameLookupExtension::createExtensionReader(
    const clang::ModuleFileExtensionMetadata &metadata,
    clang::ASTReader &reader, clang::serialization::ModuleFile &mod,
    const llvm::BitstreamCursor &stream) {
  // Make sure we have a compatible block. Since these values are part
  // of the hash, it should never be wrong.
  assert(metadata.BlockName == "swift.lookup");
  assert(metadata.MajorVersion == SWIFT_LOOKUP_TABLE_VERSION_MAJOR);
  assert(metadata.MinorVersion == SWIFT_LOOKUP_TABLE_VERSION_MINOR);

  std::function<void()> onRemove = [](){};
  std::unique_ptr<SwiftLookupTable> *target = nullptr;

  if (mod.Kind == clang::serialization::MK_PCH) {
    // PCH imports unconditionally overwrite the provided pchLookupTable.
    target = &pchLookupTable;
  } else {
    // Check whether we already have an entry in the set of lookup tables.
    target = &lookupTables[mod.ModuleName];
    if (*target) return nullptr;

    // Local function used to remove this entry when the reader goes away.
    std::string moduleName = mod.ModuleName;
    onRemove = [this, moduleName]() {
      lookupTables.erase(moduleName);
    };
  }

  // Create the reader.
  auto tableReader = SwiftLookupTableReader::create(this, reader, mod, onRemove,
                                                    stream);
  if (!tableReader) return nullptr;

  // Create the lookup table.
  target->reset(new SwiftLookupTable(tableReader.get()));

  // Return the new reader.
  return std::move(tableReader);
}

