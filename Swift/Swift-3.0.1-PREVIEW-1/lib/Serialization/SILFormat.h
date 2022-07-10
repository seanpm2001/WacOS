//===--- SILFormat.h - The internals of serialized SILs ---------*- C++ -*-===//
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
///
/// \file Contains various constants and helper types to deal with serialized
/// SILs.
///
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SERIALIZATION_SILFORMAT_H
#define SWIFT_SERIALIZATION_SILFORMAT_H

#include "swift/Serialization/ModuleFormat.h"

namespace swift {
namespace serialization {

using ValueID = DeclID;
using ValueIDField = DeclIDField;

using SILInstOpCodeField = BCFixed<8>;
using SILTypeCategoryField = BCFixed<2>;

enum SILStringEncoding : uint8_t {
  SIL_UTF8,
  SIL_UTF16,
  SIL_OBJC_SELECTOR,
};

enum SILLinkageEncoding : uint8_t {
  SIL_LINKAGE_PUBLIC,
  SIL_LINKAGE_HIDDEN,
  SIL_LINKAGE_SHARED,
  SIL_LINKAGE_PRIVATE,
  SIL_LINKAGE_PUBLIC_EXTERNAL,
  SIL_LINKAGE_HIDDEN_EXTERNAL,
  SIL_LINKAGE_SHARED_EXTERNAL,
  SIL_LINKAGE_PRIVATE_EXTERNAL,
};
using SILLinkageField = BCFixed<3>;

enum CheckedCastKindEncoding : uint8_t {
  SIL_CHECKED_CAST_ARCHETYPE_TO_ARCHETYPE,
  SIL_CHECKED_CAST_ARCHETYPE_TO_CONCRETE,
  SIL_CHECKED_CAST_ARRAY_DOWNCAST,
  SIL_CHECKED_CAST_ARRAY_DOWNCAST_BRIDGED,
  SIL_CHECKED_CAST_DICTIONARY_DOWNCAST,
  SIL_CHECKED_CAST_DICTIONARY_DOWNCAST_BRIDGED,
  SIL_CHECKED_CAST_DOWNCAST,
  SIL_CHECKED_CAST_IDENTICAL,
  SIL_CHECKED_CAST_EXISTENTIAL_TO_ARCHETYPE,
  SIL_CHECKED_CAST_EXISTENTIAL_TO_CONCRETE,
  SIL_CHECKED_CAST_SUPER_TO_ARCHETYPE,
  SIL_CHECKED_CAST_CONCRETE_TO_ARCHETYPE,
  SIL_CHECKED_CAST_CONCRETE_TO_UNRELATED_EXISTENTIAL,
};

enum CastConsumptionKindEncoding : uint8_t {
  SIL_CAST_CONSUMPTION_TAKE_ALWAYS,
  SIL_CAST_CONSUMPTION_TAKE_ON_SUCCESS,
  SIL_CAST_CONSUMPTION_COPY_ON_SUCCESS,
};

// Constants for packing an encoded CheckedCastKind and
// CastConsumptionKind together.
enum {
  // Must be large enough to store all the CheckedCastKindEncodings
  SIL_CAST_CONSUMPTION_BIT_OFFSET = 4,
  SIL_CHECKED_CAST_MASK =
    (1 << SIL_CAST_CONSUMPTION_BIT_OFFSET) - 1
};

/// The record types within the "sil-index" block.
///
/// \sa SIL_INDEX_BLOCK_ID
namespace sil_index_block {
  // These IDs must \em not be renumbered or reordered without incrementing
  // VERSION_MAJOR.
  enum RecordKind {
    SIL_FUNC_NAMES = 1,
    SIL_FUNC_OFFSETS,
    SIL_VTABLE_NAMES,
    SIL_VTABLE_OFFSETS,
    SIL_GLOBALVAR_NAMES,
    SIL_GLOBALVAR_OFFSETS,
    SIL_WITNESS_TABLE_NAMES,
    SIL_WITNESS_TABLE_OFFSETS,
    SIL_DEFAULT_WITNESS_TABLE_NAMES,
    SIL_DEFAULT_WITNESS_TABLE_OFFSETS
  };

  using ListLayout = BCGenericRecordLayout<
    BCFixed<4>,  // record ID
    BCVBR<16>,  // table offset within the blob
    BCBlob      // map from identifier strings to IDs.
  >;

  using OffsetLayout = BCGenericRecordLayout<
    BCFixed<4>,  // record ID
    BCArray<BitOffsetField>
  >;
}

/// The record types within the "sil" block.
///
/// \sa SIL_BLOCK_ID
namespace sil_block {
  // These IDs must \em not be renumbered or reordered without incrementing
  // VERSION_MAJOR.
  enum RecordKind : uint8_t {
    // To avoid overlapping with BOUND_GENERIC_SUBSTITUTION, we start from +1.
    SIL_FUNCTION = decls_block::BOUND_GENERIC_SUBSTITUTION + 1,
    SIL_BASIC_BLOCK,
    SIL_ONE_VALUE_ONE_OPERAND,
    SIL_ONE_TYPE,
    SIL_ONE_OPERAND,
    SIL_ONE_TYPE_ONE_OPERAND,
    SIL_ONE_TYPE_VALUES,
    SIL_TWO_OPERANDS,
    SIL_INST_APPLY,
    SIL_INST_NO_OPERAND,
    SIL_VTABLE,
    SIL_VTABLE_ENTRY,
    SIL_GLOBALVAR,
    SIL_INST_CAST, // It has a cast kind instead of an attribute.
    SIL_INIT_EXISTENTIAL,
    SIL_WITNESS_TABLE,
    SIL_WITNESS_METHOD_ENTRY,
    SIL_WITNESS_BASE_ENTRY,
    SIL_WITNESS_ASSOC_PROTOCOL,
    SIL_WITNESS_ASSOC_ENTRY,
    SIL_DEFAULT_WITNESS_TABLE,
    SIL_DEFAULT_WITNESS_TABLE_ENTRY,
    SIL_DEFAULT_WITNESS_TABLE_NO_ENTRY,
    SIL_GENERIC_OUTER_PARAMS,
    SIL_INST_WITNESS_METHOD,
    SIL_SPECIALIZE_ATTR,

    // We also share these layouts from the decls block. Their enumerators must
    // not overlap with ours.
    BOUND_GENERIC_SUBSTITUTION = decls_block::BOUND_GENERIC_SUBSTITUTION,
    ABSTRACT_PROTOCOL_CONFORMANCE = decls_block::ABSTRACT_PROTOCOL_CONFORMANCE,
    NORMAL_PROTOCOL_CONFORMANCE = decls_block::NORMAL_PROTOCOL_CONFORMANCE,
    SPECIALIZED_PROTOCOL_CONFORMANCE
      = decls_block::SPECIALIZED_PROTOCOL_CONFORMANCE,
    INHERITED_PROTOCOL_CONFORMANCE
      = decls_block::INHERITED_PROTOCOL_CONFORMANCE,
    GENERIC_PARAM_LIST = decls_block::GENERIC_PARAM_LIST,
    GENERIC_PARAM = decls_block::GENERIC_PARAM,
    GENERIC_REQUIREMENT = decls_block::GENERIC_REQUIREMENT,
    LAST_GENERIC_REQUIREMENT = decls_block::LAST_GENERIC_REQUIREMENT,
  };

  using SILInstNoOperandLayout = BCRecordLayout<
    SIL_INST_NO_OPERAND,
    SILInstOpCodeField
  >;

  using VTableLayout = BCRecordLayout<
    SIL_VTABLE,
    DeclIDField   // Class Decl
  >;

  using VTableEntryLayout = BCRecordLayout<
    SIL_VTABLE_ENTRY,
    DeclIDField,  // SILFunction name
    BCArray<ValueIDField> // SILDeclRef
  >;

  using WitnessTableLayout = BCRecordLayout<
    SIL_WITNESS_TABLE,
    SILLinkageField,     // Linkage
    BCFixed<1>,          // Is this a declaration. We represent this separately
                         // from whether or not we have entries since we can
                         // have empty witness tables.
    BCFixed<1>           // IsFragile.
    // Conformance follows
    // Witness table entries will be serialized after.
  >;

  using WitnessMethodEntryLayout = BCRecordLayout<
    SIL_WITNESS_METHOD_ENTRY,
    DeclIDField,  // SILFunction name
    BCArray<ValueIDField> // SILDeclRef
  >;

  using WitnessBaseEntryLayout = BCRecordLayout<
    SIL_WITNESS_BASE_ENTRY,
    DeclIDField  // ID of protocol decl
    // Trailed by the conformance itself.
  >;

  using WitnessAssocProtocolLayout = BCRecordLayout<
    SIL_WITNESS_ASSOC_PROTOCOL,
    DeclIDField, // ID of AssociatedTypeDecl
    DeclIDField  // ID of ProtocolDecl
    // Trailed by the conformance itself if appropriate.
  >;

  using WitnessAssocEntryLayout = BCRecordLayout<
    SIL_WITNESS_ASSOC_ENTRY,
    DeclIDField,  // ID of AssociatedTypeDecl
    TypeIDField
  >;

  using DefaultWitnessTableLayout = BCRecordLayout<
    SIL_DEFAULT_WITNESS_TABLE,
    DeclIDField,  // ID of ProtocolDecl
    SILLinkageField  // Linkage
    // Default witness table entries will be serialized after.
  >;

  using DefaultWitnessTableEntryLayout = BCRecordLayout<
    SIL_DEFAULT_WITNESS_TABLE_ENTRY,
    DeclIDField,  // SILFunction name
    BCArray<ValueIDField> // SILDeclRef
  >;

  using DefaultWitnessTableNoEntryLayout = BCRecordLayout<
    SIL_DEFAULT_WITNESS_TABLE_NO_ENTRY
  >;

  using SILGlobalVarLayout = BCRecordLayout<
    SIL_GLOBALVAR,
    SILLinkageField,
    BCFixed<1>,          // fragile
    BCFixed<1>,          // Is this a declaration.
    BCFixed<1>,          // Is this a let variable.
    TypeIDField,
    DeclIDField
  >;

  using SILFunctionLayout =
      BCRecordLayout<SIL_FUNCTION, SILLinkageField,
                     BCFixed<1>, // transparent
                     BCFixed<1>, // fragile
                     BCFixed<2>, // thunk/reabstraction_thunk
                     BCFixed<1>, // global_init
                     BCFixed<2>, // inlineStrategy
                     BCFixed<2>, // side effect info.
                     BCFixed<2>, // number of specialize attributes
                     TypeIDField,// SILFunctionType
                     DeclIDField,// ClangNode owner
                     BCArray<IdentifierIDField> // Semantics Attribute
                     // followed by specialize attributes
                     // followed by generic param list, if any
                     >;

  using SILSpecializeAttrLayout =
      BCRecordLayout<SIL_SPECIALIZE_ATTR,
                     BCFixed<5> // number of substitutions
                     // followed by bound generic substitutions
                     >;

  // Has an optional argument list where each argument is a typed valueref.
  using SILBasicBlockLayout = BCRecordLayout<
    SIL_BASIC_BLOCK,
    BCArray<DeclIDField> // The array contains type-value pairs.
  >;

  // SIL instructions with one valueref and one typed valueref.
  // (store)
  using SILOneValueOneOperandLayout = BCRecordLayout<
    SIL_ONE_VALUE_ONE_OPERAND,
    SILInstOpCodeField,
    BCFixed<2>,          // Optional attributes
    ValueIDField,
    TypeIDField,
    SILTypeCategoryField,
    ValueIDField
  >;

  // SIL instructions with one type and one typed valueref.
  using SILOneTypeOneOperandLayout = BCRecordLayout<
    SIL_ONE_TYPE_ONE_OPERAND,
    SILInstOpCodeField,
    BCFixed<2>,          // Optional attributes
    TypeIDField,
    SILTypeCategoryField,
    TypeIDField,
    SILTypeCategoryField,
    ValueIDField
  >;

  // SIL instructions that construct existential values.
  using SILInitExistentialLayout = BCRecordLayout<
    SIL_INIT_EXISTENTIAL,
    SILInstOpCodeField,   // opcode
    TypeIDField,          // result type
    SILTypeCategoryField, // result type category
    TypeIDField,          // operand type
    SILTypeCategoryField, // operand type category
    ValueIDField,         // operand id
    TypeIDField,          // formal concrete type
    BCVBR<5>              // # of protocol conformances
    // Trailed by protocol conformance info (if any)
  >;

  // SIL Cast instructions with a cast kind, one type and one typed valueref.
  using SILInstCastLayout = BCRecordLayout<
    SIL_INST_CAST,
    SILInstOpCodeField,
    BCFixed<4>,          // Cast kind
    TypeIDField,
    SILTypeCategoryField,
    TypeIDField,
    SILTypeCategoryField,
    ValueIDField
  >;

  // SIL instructions with one type and a list of values.
  using SILOneTypeValuesLayout = BCRecordLayout<
    SIL_ONE_TYPE_VALUES,
    SILInstOpCodeField,
    TypeIDField,
    SILTypeCategoryField,
    BCArray<ValueIDField>
  >;

  enum ApplyKind : unsigned {
    SIL_APPLY = 0,
    SIL_PARTIAL_APPLY,
    SIL_BUILTIN,
    SIL_TRY_APPLY,
    SIL_NON_THROWING_APPLY
  };
  
  using SILInstApplyLayout = BCRecordLayout<
    SIL_INST_APPLY,
    BCFixed<3>,           // ApplyKind
    BCFixed<32>,          // num substitutions
    TypeIDField,          // callee unsubstituted type
    TypeIDField,          // callee substituted type
    ValueIDField,         // callee value
    BCArray<ValueIDField> // a list of arguments
  >;

  // SIL instructions with one type. (alloc_stack)
  using SILOneTypeLayout = BCRecordLayout<
    SIL_ONE_TYPE,
    SILInstOpCodeField,
    TypeIDField,
    SILTypeCategoryField
  >;

  // SIL instructions with one typed valueref. (dealloc_stack, return)
  using SILOneOperandLayout = BCRecordLayout<
    SIL_ONE_OPERAND,
    SILInstOpCodeField,
    BCFixed<3>,          // Optional attributes
    TypeIDField,
    SILTypeCategoryField,
    ValueIDField
  >;

  // SIL instructions with two typed values.
  using SILTwoOperandsLayout = BCRecordLayout<
    SIL_TWO_OPERANDS,
    SILInstOpCodeField,
    BCFixed<2>,          // Optional attributes
    TypeIDField,
    SILTypeCategoryField,
    ValueIDField,
    TypeIDField,
    SILTypeCategoryField,
    ValueIDField
  >;

  using SILGenericOuterParamsLayout = BCRecordLayout<
    SIL_GENERIC_OUTER_PARAMS,
    DeclIDField // The decl id of the outer param if any.
  >;

  using SILInstWitnessMethodLayout = BCRecordLayout<
    SIL_INST_WITNESS_METHOD,
    TypeIDField,           // result type
    SILTypeCategoryField,
    BCFixed<1>,            // volatile?
    TypeIDField,           // lookup type
    SILTypeCategoryField,
    TypeIDField,           // Optional
    SILTypeCategoryField,  // opened
    ValueIDField,          // existential
    BCArray<ValueIDField>  // SILDeclRef
    // may be trailed by an inline protocol conformance
  >;
}

} // end namespace serialization
} // end namespace swift

#endif
