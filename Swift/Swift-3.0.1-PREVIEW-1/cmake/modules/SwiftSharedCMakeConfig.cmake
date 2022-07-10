include(CMakeParseArguments)

# Use ${cmake_2_8_12_KEYWORD} instead of KEYWORD in target_link_libraries().
# These variables are used by LLVM's CMake code.
set(cmake_2_8_12_INTERFACE INTERFACE)
set(cmake_2_8_12_PRIVATE PRIVATE)

# Backwards compatible USES_TERMINAL, cargo culted from llvm's cmake configs.
if(CMAKE_VERSION VERSION_LESS 3.1.20141117)
  set(cmake_3_2_USES_TERMINAL)
else()
  set(cmake_3_2_USES_TERMINAL USES_TERMINAL)
endif()

include(SwiftXcodeSupport)

macro(swift_common_standalone_build_config_llvm product is_cross_compiling)
  option(LLVM_ENABLE_WARNINGS "Enable compiler warnings." ON)

  precondition_translate_flag(${product}_PATH_TO_LLVM_SOURCE PATH_TO_LLVM_SOURCE)
  precondition_translate_flag(${product}_PATH_TO_LLVM_BUILD PATH_TO_LLVM_BUILD)

  set(SWIFT_LLVM_CMAKE_PATHS
      "${PATH_TO_LLVM_BUILD}/share/llvm/cmake"
      "${PATH_TO_LLVM_BUILD}/lib/cmake/llvm")

  # Add all LLVM CMake paths to our cmake module path.
  foreach(path ${SWIFT_LLVM_CMAKE_PATHS})
    list(APPEND CMAKE_MODULE_PATH ${path})
  endforeach()

  # If we already have a cached value for LLVM_ENABLE_ASSERTIONS, save the value.
  if (DEFINED LLVM_ENABLE_ASSERTIONS)
    set(LLVM_ENABLE_ASSERTIONS_saved "${LLVM_ENABLE_ASSERTIONS}")
  endif()

  # Then we import LLVMConfig. This is going to override whatever cached value
  # we have for LLVM_ENABLE_ASSERTIONS.
  find_package(LLVM REQUIRED CONFIG
    HINTS "${PATH_TO_LLVM_BUILD}" NO_DEFAULT_PATH)

  # If we did not have a cached value for LLVM_ENABLE_ASSERTIONS, set
  # LLVM_ENABLE_ASSERTIONS_saved to be the ENABLE_ASSERTIONS value from LLVM so
  # we follow LLVMConfig.cmake's value by default if nothing is provided.
  if (NOT DEFINED LLVM_ENABLE_ASSERTIONS_saved)
    set(LLVM_ENABLE_ASSERTIONS_saved "${LLVM_ENABLE_ASSERTIONS}")
  endif()

  # Then set LLVM_ENABLE_ASSERTIONS with a default value of
  # LLVM_ENABLE_ASSERTIONS_saved.
  #
  # The effect of this is that if LLVM_ENABLE_ASSERTION did not have a cached
  # value, then LLVM_ENABLE_ASSERTIONS_saved is set to LLVM's value, so we get a
  # default value from LLVM.
  set(LLVM_ENABLE_ASSERTIONS "${LLVM_ENABLE_ASSERTIONS_saved}"
    CACHE BOOL "Enable assertions")
  mark_as_advanced(LLVM_ENABLE_ASSERTIONS)

  precondition(LLVM_TOOLS_BINARY_DIR)
  escape_path_for_xcode("${LLVM_BUILD_TYPE}" "${LLVM_TOOLS_BINARY_DIR}" LLVM_TOOLS_BINARY_DIR)
  precondition_translate_flag(LLVM_BUILD_LIBRARY_DIR LLVM_LIBRARY_DIR)
  escape_path_for_xcode("${LLVM_BUILD_TYPE}" "${LLVM_LIBRARY_DIR}" LLVM_LIBRARY_DIR)
  precondition_translate_flag(LLVM_BUILD_MAIN_INCLUDE_DIR LLVM_MAIN_INCLUDE_DIR)
  precondition_translate_flag(LLVM_BUILD_BINARY_DIR LLVM_BINARY_DIR)
  precondition_translate_flag(LLVM_BUILD_MAIN_SRC_DIR LLVM_MAIN_SRC_DIR)
  precondition(LLVM_LIBRARY_DIRS)
  escape_path_for_xcode("${LLVM_BUILD_TYPE}" "${LLVM_LIBRARY_DIRS}" LLVM_LIBRARY_DIRS)

  # This could be computed using ${CMAKE_CFG_INTDIR} if we want to link Swift
  # against a matching LLVM build configuration.  However, we usually want to be
  # flexible and allow linking a debug Swift against optimized LLVM.
  set(LLVM_RUNTIME_OUTPUT_INTDIR "${LLVM_BINARY_DIR}")
  set(LLVM_BINARY_OUTPUT_INTDIR "${LLVM_TOOLS_BINARY_DIR}")
  set(LLVM_LIBRARY_OUTPUT_INTDIR "${LLVM_LIBRARY_DIR}")

  if (XCODE)
    fix_imported_targets_for_xcode("${LLVM_EXPORTED_TARGETS}")
  endif()

  if(NOT ${is_cross_compiling})
    set(${product}_NATIVE_LLVM_TOOLS_PATH "${LLVM_TOOLS_BINARY_DIR}")
  endif()

  find_program(SWIFT_TABLEGEN_EXE "llvm-tblgen" "${${product}_NATIVE_LLVM_TOOLS_PATH}"
    NO_DEFAULT_PATH)
  if ("${SWIFT_TABLEGEN_EXE}" STREQUAL "SWIFT_TABLEGEN_EXE-NOTFOUND")
    message(FATAL_ERROR "Failed to find tablegen in ${${product}_NATIVE_LLVM_TOOLS_PATH}")
  endif()

  include(AddLLVM)
  include(AddSwiftTableGen) # This imports TableGen from LLVM.
  include(HandleLLVMOptions)

  # HACK: this ugly tweaking is to prevent the propagation of the flag from LLVM
  # into swift.  The use of this flag pollutes all targets, and we are not able
  # to remove it on a per-target basis which breaks cross-compilation.
  string(REGEX REPLACE "-Wl,-z,defs" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

  set(PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}")
  string(REGEX REPLACE "([0-9]+)\\.[0-9]+(\\.[0-9]+)?" "\\1" PACKAGE_VERSION_MAJOR
    ${PACKAGE_VERSION})
  string(REGEX REPLACE "[0-9]+\\.([0-9]+)(\\.[0-9]+)?" "\\1" PACKAGE_VERSION_MINOR
    ${PACKAGE_VERSION})

  set(SWIFT_LIBCLANG_LIBRARY_VERSION
    "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}" CACHE STRING
    "Version number that will be placed into the libclang library , in the form XX.YY")

  foreach (INCLUDE_DIR ${LLVM_INCLUDE_DIRS})
    escape_path_for_xcode("${LLVM_BUILD_TYPE}" "${INCLUDE_DIR}" INCLUDE_DIR)
    include_directories(${INCLUDE_DIR})
  endforeach ()

  # *NOTE* if we want to support separate Clang builds as well as separate LLVM
  # builds, the clang build directory needs to be added here.
  link_directories("${LLVM_LIBRARY_DIR}")

  set(LIT_ARGS_DEFAULT "-sv")
  if(XCODE)
    set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
  endif()
  set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

  set(LLVM_INCLUDE_TESTS TRUE)
  set(LLVM_INCLUDE_DOCS TRUE)

  option(LLVM_ENABLE_DOXYGEN "Enable doxygen support" FALSE)
  if (LLVM_ENABLE_DOXYGEN)
    find_package(Doxygen REQUIRED)
  endif()
endmacro()

macro(swift_common_standalone_build_config_clang product is_cross_compiling)
  set(${product}_PATH_TO_CLANG_SOURCE "${PATH_TO_LLVM_SOURCE}/tools/clang"
      CACHE PATH "Path to Clang source code.")
  set(${product}_PATH_TO_CLANG_BUILD "${PATH_TO_LLVM_BUILD}" CACHE PATH
    "Path to the directory where Clang was built or installed.")

  set(PATH_TO_CLANG_SOURCE "${${product}_PATH_TO_CLANG_SOURCE}")
  set(PATH_TO_CLANG_BUILD "${${product}_PATH_TO_CLANG_BUILD}")

  # Add all Clang CMake paths to our cmake module path.
  set(SWIFT_CLANG_CMAKE_PATHS
    "${PATH_TO_CLANG_BUILD}/share/clang/cmake"
    "${PATH_TO_CLANG_BUILD}/lib/cmake/clang")
  foreach(path ${SWIFT_CLANG_CMAKE_PATHS})
    list(APPEND CMAKE_MODULE_PATH ${path})
  endforeach()

  # Then include Clang.
  find_package(Clang REQUIRED CONFIG
    HINTS "${PATH_TO_CLANG_BUILD}" NO_DEFAULT_PATH)

  if(NOT EXISTS "${PATH_TO_CLANG_SOURCE}/include/clang/AST/Decl.h")
    message(FATAL_ERROR "Please set ${product}_PATH_TO_CLANG_SOURCE to the root directory of Clang's source code.")
  endif()
  get_filename_component(CLANG_MAIN_SRC_DIR "${PATH_TO_CLANG_SOURCE}" ABSOLUTE)

  if(NOT EXISTS "${PATH_TO_CLANG_BUILD}/tools/clang/include/clang/Basic/Version.inc")
    message(FATAL_ERROR "Please set ${product}_PATH_TO_CLANG_BUILD to a directory containing a Clang build.")
  endif()
  set(CLANG_BUILD_INCLUDE_DIR "${PATH_TO_CLANG_BUILD}/tools/clang/include")

  if (NOT ${is_cross_compiling})
    set(${product}_NATIVE_CLANG_TOOLS_PATH "${LLVM_TOOLS_BINARY_DIR}")
  endif()

  set(CLANG_MAIN_INCLUDE_DIR "${CLANG_MAIN_SRC_DIR}/include")

  if (XCODE)
    fix_imported_targets_for_xcode("${CLANG_EXPORTED_TARGETS}")
  endif()

  include_directories("${CLANG_BUILD_INCLUDE_DIR}"
                      "${CLANG_MAIN_INCLUDE_DIR}")
endmacro()

macro(swift_common_standalone_build_config_cmark product)
  set(${product}_PATH_TO_CMARK_SOURCE "${${product}_PATH_TO_CMARK_SOURCE}"
    CACHE PATH "Path to CMark source code.")
  set(${product}_PATH_TO_CMARK_BUILD "${${product}_PATH_TO_CMARK_BUILD}"
    CACHE PATH "Path to the directory where CMark was built.")
  set(${product}_CMARK_LIBRARY_DIR "${${product}_CMARK_LIBRARY_DIR}" CACHE PATH
    "Path to the directory where CMark was installed.")
  get_filename_component(PATH_TO_CMARK_BUILD "${${product}_PATH_TO_CMARK_BUILD}"
    ABSOLUTE)
  get_filename_component(CMARK_MAIN_SRC_DIR "${${product}_PATH_TO_CMARK_SOURCE}"
    ABSOLUTE)
  get_filename_component(CMARK_LIBRARY_DIR "${${product}_CMARK_LIBRARY_DIR}"
    ABSOLUTE)
  set(CMARK_MAIN_INCLUDE_DIR "${CMARK_MAIN_SRC_DIR}/src")
  set(CMARK_BUILD_INCLUDE_DIR "${PATH_TO_CMARK_BUILD}/src")
  include_directories("${CMARK_MAIN_INCLUDE_DIR}"
                      "${CMARK_BUILD_INCLUDE_DIR}")
endmacro()

# Common cmake project config for standalone builds.
#
# Parameters:
#   product
#     The product name, e.g. Swift or SourceKit. Used as prefix for some
#     cmake variables.
#
#   is_cross_compiling
#     Whether this is cross-compiling host tools.
macro(swift_common_standalone_build_config product is_cross_compiling)
  swift_common_standalone_build_config_llvm(${product} ${is_cross_compiling})
  swift_common_standalone_build_config_clang(${product} ${is_cross_compiling})
  swift_common_standalone_build_config_cmark(${product})
endmacro()

# Common cmake project config for unified builds.
#
# Parameters:
#   product
#     The product name, e.g. Swift or SourceKit. Used as prefix for some
#     cmake variables.
macro(swift_common_unified_build_config product)
  set(PATH_TO_LLVM_SOURCE "${CMAKE_SOURCE_DIR}")
  set(PATH_TO_LLVM_BUILD "${CMAKE_BINARY_DIR}")
  set(${product}_PATH_TO_CLANG_BUILD "${CMAKE_BINARY_DIR}")
  set(PATH_TO_CLANG_BUILD "${CMAKE_BINARY_DIR}")
  set(CLANG_MAIN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/tools/clang/include")
  set(CLANG_BUILD_INCLUDE_DIR "${CMAKE_BINARY_DIR}/tools/clang/include")
  set(${product}_NATIVE_LLVM_TOOLS_PATH "${CMAKE_BINARY_DIR}/bin")
  set(${product}_NATIVE_CLANG_TOOLS_PATH "${CMAKE_BINARY_DIR}/bin")
  set(LLVM_PACKAGE_VERSION ${PACKAGE_VERSION})
  set(SWIFT_TABLEGEN_EXE llvm-tblgen)

  # If cmark was checked out into tools/cmark, expect to build it as
  # part of the unified build.
  if(EXISTS "${CMAKE_SOURCE_DIR}/tools/cmark/")
    set(${product}_PATH_TO_CMARK_SOURCE "${CMAKE_SOURCE_DIR}/tools/cmark")
    set(${product}_PATH_TO_CMARK_BUILD "${CMAKE_BINARY_DIR}/tools/cmark")
    set(${product}_CMARK_LIBRARY_DIR "${CMAKE_BINARY_DIR}/lib")

    get_filename_component(CMARK_MAIN_SRC_DIR "${${product}_PATH_TO_CMARK_SOURCE}"
      ABSOLUTE)
    get_filename_component(PATH_TO_CMARK_BUILD "${${product}_PATH_TO_CMARK_BUILD}"
      ABSOLUTE)
    get_filename_component(CMARK_LIBRARY_DIR "${${product}_CMARK_LIBRARY_DIR}"
      ABSOLUTE)

    set(CMARK_BUILD_INCLUDE_DIR "${PATH_TO_CMARK_BUILD}/src")
    set(CMARK_MAIN_INCLUDE_DIR "${CMARK_MAIN_SRC_DIR}/src")
  endif()

  include_directories(
      "${CLANG_BUILD_INCLUDE_DIR}"
      "${CLANG_MAIN_INCLUDE_DIR}"
      "${CMARK_MAIN_INCLUDE_DIR}"
      "${CMARK_BUILD_INCLUDE_DIR}")

  include(AddSwiftTableGen) # This imports TableGen from LLVM.

  check_cxx_compiler_flag("-Werror -Wnested-anon-types" CXX_SUPPORTS_NO_NESTED_ANON_TYPES_FLAG)
  if(CXX_SUPPORTS_NO_NESTED_ANON_TYPES_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types")
  endif()
endmacro()

# Common cmake project config for additional warnings.
#
macro(swift_common_cxx_warnings)
  check_cxx_compiler_flag("-Werror -Wdocumentation" CXX_SUPPORTS_DOCUMENTATION_FLAG)
  append_if(CXX_SUPPORTS_DOCUMENTATION_FLAG "-Wdocumentation" CMAKE_CXX_FLAGS)

  check_cxx_compiler_flag("-Werror -Wimplicit-fallthrough" CXX_SUPPORTS_IMPLICIT_FALLTHROUGH_FLAG)
  append_if(CXX_SUPPORTS_IMPLICIT_FALLTHROUGH_FLAG "-Wimplicit-fallthrough" CMAKE_CXX_FLAGS)

  # Check for -Wunreachable-code-aggressive instead of -Wunreachable-code, as that indicates
  # that we have the newer -Wunreachable-code implementation.
  check_cxx_compiler_flag("-Werror -Wunreachable-code-aggressive" CXX_SUPPORTS_UNREACHABLE_CODE_FLAG)
  append_if(CXX_SUPPORTS_UNREACHABLE_CODE_FLAG "-Wunreachable-code" CMAKE_CXX_FLAGS)

  check_cxx_compiler_flag("-Werror -Woverloaded-virtual" CXX_SUPPORTS_OVERLOADED_VIRTUAL)
  append_if(CXX_SUPPORTS_OVERLOADED_VIRTUAL "-Woverloaded-virtual" CMAKE_CXX_FLAGS)

  # Check for '-fapplication-extension'.  On OS X/iOS we wish to link all
  # dynamic libraries with this flag.
  check_cxx_compiler_flag("-fapplication-extension" CXX_SUPPORTS_FAPPLICATION_EXTENSION)
endmacro()

# Like 'llvm_config()', but uses libraries from the selected build
# configuration in LLVM.  ('llvm_config()' selects the same build configuration
# in LLVM as we have for Swift.)
function(swift_common_llvm_config target)
  set(link_components ${ARGN})

  if((SWIFT_BUILT_STANDALONE OR SOURCEKIT_BUILT_STANDALONE) AND NOT "${CMAKE_CFG_INTDIR}" STREQUAL ".")
    llvm_map_components_to_libnames(libnames ${link_components})

    get_target_property(target_type "${target}" TYPE)
    if("${target_type}" STREQUAL "STATIC_LIBRARY")
      target_link_libraries("${target}" INTERFACE ${libnames})
    elseif("${target_type}" STREQUAL "SHARED_LIBRARY" OR
           "${target_type}" STREQUAL "MODULE_LIBRARY")
      target_link_libraries("${target}" PRIVATE ${libnames})
    else()
      # HACK: Otherwise (for example, for executables), use a plain signature,
      # because LLVM CMake does that already.
      target_link_libraries("${target}" ${libnames})
    endif()
  else()
    # If Swift was not built standalone, dispatch to 'llvm_config()'.
    llvm_config("${target}" ${ARGN})
  endif()
endfunction()
