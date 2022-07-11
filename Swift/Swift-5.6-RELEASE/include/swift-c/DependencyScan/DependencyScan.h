//===--- DependencyScan.h - C API for Swift Dependency Scanning ---*- C -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This C API is primarily intended to serve as the Swift Driver's
// dependency scanning facility (https://github.com/apple/swift-driver).
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_C_DEPENDENCY_SCAN_H
#define SWIFT_C_DEPENDENCY_SCAN_H

#include "DependencyScanMacros.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// The version constants for the SwiftDependencyScan C API.
/// SWIFTSCAN_VERSION_MINOR should increase when there are API additions.
/// SWIFTSCAN_VERSION_MAJOR is intended for "major" source/ABI breaking changes.
#define SWIFTSCAN_VERSION_MAJOR 0
#define SWIFTSCAN_VERSION_MINOR 2

SWIFTSCAN_BEGIN_DECLS

//=== Public Scanner Data Types -------------------------------------------===//

/**
 * A character string used to pass around dependency scan result metadata.
 * Lifetime of the string is strictly tied to the object whose field it
 * represents. When the owning object is released, string memory is freed.
 */
typedef struct {
  const void *data;
  size_t length;
} swiftscan_string_ref_t;

typedef struct {
  swiftscan_string_ref_t *strings;
  size_t count;
} swiftscan_string_set_t;

typedef enum {
  // This dependency info encodes two ModuleDependencyKind types:
  // SwiftInterface and SwiftSource.
  SWIFTSCAN_DEPENDENCY_INFO_SWIFT_TEXTUAL = 0,
  SWIFTSCAN_DEPENDENCY_INFO_SWIFT_BINARY = 1,
  SWIFTSCAN_DEPENDENCY_INFO_SWIFT_PLACEHOLDER = 2,
  SWIFTSCAN_DEPENDENCY_INFO_CLANG = 3
} swiftscan_dependency_info_kind_t;

/// Opaque container of the details specific to a given module dependency.
typedef struct swiftscan_module_details_s *swiftscan_module_details_t;

/// Opaque container to a dependency info of a given module.
typedef struct swiftscan_dependency_info_s *swiftscan_dependency_info_t;

/// Opaque container to an overall result of a dependency scan.
typedef struct swiftscan_dependency_graph_s *swiftscan_dependency_graph_t;

/// Opaque container to contain the result of a dependency prescan.
typedef struct swiftscan_import_set_s *swiftscan_import_set_t;

/// Full Dependency Graph (Result)
typedef struct {
  swiftscan_dependency_info_t *modules;
  size_t count;
} swiftscan_dependency_set_t;

//=== Batch Scan Input Specification --------------------------------------===//

/// Opaque container to a container of batch scan entry information.
typedef struct swiftscan_batch_scan_entry_s *swiftscan_batch_scan_entry_t;

typedef struct {
  swiftscan_batch_scan_entry_t *modules;
  size_t count;
} swiftscan_batch_scan_input_t;

typedef struct {
  swiftscan_dependency_graph_t *results;
  size_t count;
} swiftscan_batch_scan_result_t;

//=== Scanner Invocation Specification ------------------------------------===//

/// Opaque container of all relevant context required to launch a dependency
/// scan (command line arguments, working directory, etc.)
typedef struct swiftscan_scan_invocation_s *swiftscan_scan_invocation_t;

//=== Dependency Result Functions -----------------------------------------===//

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_dependency_graph_get_main_module_name(
    swiftscan_dependency_graph_t result);

SWIFTSCAN_PUBLIC swiftscan_dependency_set_t *
swiftscan_dependency_graph_get_dependencies(
    swiftscan_dependency_graph_t result);

//=== Dependency Module Info Functions ------------------------------------===//

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_module_info_get_module_name(swiftscan_dependency_info_t info);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_module_info_get_module_path(swiftscan_dependency_info_t info);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_module_info_get_source_files(swiftscan_dependency_info_t info);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_module_info_get_direct_dependencies(swiftscan_dependency_info_t info);

SWIFTSCAN_PUBLIC swiftscan_module_details_t
swiftscan_module_info_get_details(swiftscan_dependency_info_t info);

//=== Dependency Module Info Details Functions ----------------------------===//

SWIFTSCAN_PUBLIC swiftscan_dependency_info_kind_t
swiftscan_module_detail_get_kind(swiftscan_module_details_t details);

//=== Swift Textual Module Details query APIs -----------------------------===//
SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_textual_detail_get_module_interface_path(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_swift_textual_detail_get_compiled_module_candidates(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_textual_detail_get_bridging_header_path(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_swift_textual_detail_get_bridging_source_files(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_swift_textual_detail_get_bridging_module_dependencies(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_swift_textual_detail_get_command_line(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_swift_textual_detail_get_extra_pcm_args(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_textual_detail_get_context_hash(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC bool swiftscan_swift_textual_detail_get_is_framework(
    swiftscan_module_details_t details);

//=== Swift Binary Module Details query APIs ------------------------------===//

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_binary_detail_get_compiled_module_path(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_binary_detail_get_module_doc_path(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_binary_detail_get_module_source_info_path(
    swiftscan_module_details_t details);

//=== Swift Placeholder Module Details query APIs -------------------------===//

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_placeholder_detail_get_compiled_module_path(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_placeholder_detail_get_module_doc_path(
    swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_swift_placeholder_detail_get_module_source_info_path(
    swiftscan_module_details_t details);

//=== Clang Module Details query APIs -------------------------------------===//

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_clang_detail_get_module_map_path(swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_clang_detail_get_context_hash(swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_clang_detail_get_command_line(swiftscan_module_details_t details);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_clang_detail_get_captured_pcm_args(swiftscan_module_details_t details);

//=== Batch Scan Input Functions ------------------------------------------===//

/// Create an \c swiftscan_batch_scan_input_t instance.
/// The returned \c swiftscan_batch_scan_input_t is owned by the caller and must be disposed
/// of using \c swiftscan_batch_scan_input_dispose .
SWIFTSCAN_PUBLIC swiftscan_batch_scan_input_t *
swiftscan_batch_scan_input_create();

SWIFTSCAN_PUBLIC void
swiftscan_batch_scan_input_set_modules(swiftscan_batch_scan_input_t *input,
                                       int count,
                                       swiftscan_batch_scan_entry_t *modules);

//=== Batch Scan Entry Functions ------------------------------------------===//

/// Create an \c swiftscan_batch_scan_entry_t instance.
/// The returned \c swiftscan_batch_scan_entry_t is owned by the caller and must be disposed
/// of using \c swiftscan_batch_scan_entry_dispose .
SWIFTSCAN_PUBLIC swiftscan_batch_scan_entry_t
swiftscan_batch_scan_entry_create();

SWIFTSCAN_PUBLIC void
swiftscan_batch_scan_entry_set_module_name(swiftscan_batch_scan_entry_t entry,
                                           const char *name);

SWIFTSCAN_PUBLIC void
swiftscan_batch_scan_entry_set_arguments(swiftscan_batch_scan_entry_t entry,
                                         const char *arguments);

SWIFTSCAN_PUBLIC void
swiftscan_batch_scan_entry_set_is_swift(swiftscan_batch_scan_entry_t entry,
                                        bool is_swift);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_batch_scan_entry_get_module_name(swiftscan_batch_scan_entry_t entry);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_batch_scan_entry_get_arguments(swiftscan_batch_scan_entry_t entry);

SWIFTSCAN_PUBLIC bool
swiftscan_batch_scan_entry_get_is_swift(swiftscan_batch_scan_entry_t entry);

//=== Prescan Result Functions --------------------------------------------===//

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_import_set_get_imports(swiftscan_import_set_t result);

//=== Scanner Invocation Functions ----------------------------------------===//

/// Create an \c swiftscan_scan_invocation_t instance.
/// The returned \c swiftscan_scan_invocation_t is owned by the caller and must be disposed
/// of using \c swiftscan_scan_invocation_dispose .
SWIFTSCAN_PUBLIC swiftscan_scan_invocation_t swiftscan_scan_invocation_create();

SWIFTSCAN_PUBLIC void swiftscan_scan_invocation_set_working_directory(
    swiftscan_scan_invocation_t invocation, const char *working_directory);

SWIFTSCAN_PUBLIC void
swiftscan_scan_invocation_set_argv(swiftscan_scan_invocation_t invocation,
                                   int argc, const char **argv);

SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_scan_invocation_get_working_directory(
    swiftscan_scan_invocation_t invocation);

SWIFTSCAN_PUBLIC int
swiftscan_scan_invocation_get_argc(swiftscan_scan_invocation_t invocation);

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_scan_invocation_get_argv(swiftscan_scan_invocation_t invocation);

//=== Cleanup Functions ---------------------------------------------------===//

SWIFTSCAN_PUBLIC void
swiftscan_string_set_dispose(swiftscan_string_set_t *set);

SWIFTSCAN_PUBLIC void
swiftscan_dependency_graph_dispose(swiftscan_dependency_graph_t result);

SWIFTSCAN_PUBLIC void
swiftscan_import_set_dispose(swiftscan_import_set_t result);

SWIFTSCAN_PUBLIC void
swiftscan_batch_scan_entry_dispose(swiftscan_batch_scan_entry_t entry);

SWIFTSCAN_PUBLIC void
swiftscan_batch_scan_input_dispose(swiftscan_batch_scan_input_t *input);

SWIFTSCAN_PUBLIC void
swiftscan_batch_scan_result_dispose(swiftscan_batch_scan_result_t *result);

SWIFTSCAN_PUBLIC void
swiftscan_scan_invocation_dispose(swiftscan_scan_invocation_t invocation);

//=== Feature-Query Functions ---------------------------------------------===//
SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_compiler_supported_arguments_query();

SWIFTSCAN_PUBLIC swiftscan_string_set_t *
swiftscan_compiler_supported_features_query();

//=== Target-Info Functions -----------------------------------------------===//
SWIFTSCAN_PUBLIC swiftscan_string_ref_t
swiftscan_compiler_target_info_query(swiftscan_scan_invocation_t invocation);

//=== Scanner Functions ---------------------------------------------------===//

/// Container of the configuration state and shared cache for dependency
/// scanning.
typedef void *swiftscan_scanner_t;

/// Create an \c swiftscan_scanner_t instance.
/// The returned \c swiftscan_scanner_t is owned by the caller and must be disposed
/// of using \c swiftscan_scanner_dispose .
SWIFTSCAN_PUBLIC swiftscan_scanner_t swiftscan_scanner_create(void);
SWIFTSCAN_PUBLIC void swiftscan_scanner_dispose(swiftscan_scanner_t);

/// Invoke a dependency scan using arguments specified in the \c
/// swiftscan_scan_invocation_t argument. The returned \c
/// swiftscan_dependency_graph_t is owned by the caller and must be disposed of
/// using \c swiftscan_dependency_graph_dispose .
SWIFTSCAN_PUBLIC swiftscan_dependency_graph_t swiftscan_dependency_graph_create(
    swiftscan_scanner_t scanner, swiftscan_scan_invocation_t invocation);

/// Invoke the scan for an input batch of modules specified in the
/// \c swiftscan_batch_scan_input_t argument. The returned
/// \c swiftscan_batch_scan_result_t is owned by the caller and must be disposed
/// of using \c swiftscan_batch_scan_result_dispose .
SWIFTSCAN_PUBLIC swiftscan_batch_scan_result_t *
swiftscan_batch_scan_result_create(swiftscan_scanner_t scanner,
                                   swiftscan_batch_scan_input_t *batch_input,
                                   swiftscan_scan_invocation_t invocation);

/// Invoke the import prescan using arguments specified in the \c
/// swiftscan_scan_invocation_t argument. The returned \c swiftscan_import_set_t
/// is owned by the caller and must be disposed of using \c
/// swiftscan_import_set_dispose .
SWIFTSCAN_PUBLIC swiftscan_import_set_t swiftscan_import_set_create(
    swiftscan_scanner_t scanner, swiftscan_scan_invocation_t invocation);

//=== Scanner Cache Operations --------------------------------------------===//
// The following operations expose an implementation detail of the dependency
// scanner: its module dependencies cache. This is done in order
// to allow clients to perform incremental dependency scans by having the
// scanner's state be serializable and re-usable.

/// For the specified \c scanner instance, serialize its state to the specified file-system \c path .
SWIFTSCAN_PUBLIC void
swiftscan_scanner_cache_serialize(swiftscan_scanner_t scanner,
                                  const char * path);

/// For the specified \c scanner instance, load in scanner state from a file at
/// the specified file-system \c path .
SWIFTSCAN_PUBLIC bool
swiftscan_scanner_cache_load(swiftscan_scanner_t scanner,
                             const char * path);

/// For the specified \c scanner instance, reset its internal state, ensuring subsequent
/// scanning queries are done "from-scratch".
SWIFTSCAN_PUBLIC void
swiftscan_scanner_cache_reset(swiftscan_scanner_t scanner);

/// An entry point to invoke the compiler via a library call.
SWIFTSCAN_PUBLIC int invoke_swift_compiler(int argc, const char **argv);

//===----------------------------------------------------------------------===//

SWIFTSCAN_END_DECLS

#endif // SWIFT_C_DEPENDENCY_SCAN_H
