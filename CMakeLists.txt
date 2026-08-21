cmake_minimum_required(VERSION 4.4 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake 4.4 still gates import std support behind its experimental UUID. Keep
# this local to standalone Switch configuration; a superproject owns its own
# compiler/toolchain setup.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR
   AND CMAKE_VERSION VERSION_LESS 4.5
   AND NOT DEFINED CMAKE_EXPERIMENTAL_CXX_IMPORT_STD)
  set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
endif()

project(
  Switch
  VERSION 0.1.0
  DESCRIPTION "A modern C++26 testing framework."
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
  message(
    FATAL_ERROR
      "Switch 0.1.0 currently requires a Ninja generator for C++26 module "
      "support.")
endif()

if(NOT 26 IN_LIST CMAKE_CXX_COMPILER_IMPORT_STD)
  message(
    FATAL_ERROR
      "Switch requires toolchain support for C++26 'import std;'. "
      "When Switch is a subproject, enable CMake's import-std support before "
      "the top-level project() call.")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION
                                            VERSION_LESS 16)
  message(FATAL_ERROR"Switch requires GCC 16 or newer when built with GCC.")
endif()

# Dependency injection is intentionally target-first. A parent build such as
# Miracle's self-tests can provide Miracle::Miracle directly without Switch
# attempting to discover or populate another copy.
if(NOT TARGET Miracle::Miracle)
  find_package(Miracle 0.1.0 CONFIG QUIET)
endif()

# FETCHCONTENT_SOURCE_DIR_MIRACLE is the standard explicit source override for
# an editable local checkout. Keep it ahead of automatic sibling lookup.
if(NOT TARGET Miracle::Miracle AND FETCHCONTENT_SOURCE_DIR_MIRACLE)
  include(FetchContent)
  FetchContent_Declare(
    Miracle
    GIT_REPOSITORY https:/github.com/spwn02/Miracle.git
    GIT_TAG master
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Miracle)
endif()

# During extraction, and for developers who clone both repositories beside one
# another, prefer the natural sibling checkout before contacting the network.
set(switch_sibling_miracle_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Miracle")
if(NOT TARGET Miracle::Miracle AND EXISTS "${switch_sibling_miracle_dir}")
  add_subdirectory("${switch_sibling_miracle_dir}"
                   "${CMAKE_CURRENT_BINARY_DIR}/_deps/miracle")
endif()
unset(switch_sibling_miracle_dir)

if(NOT TARGET Miracle::Miracle)
  include(FetchContent)
  FetchContent_Declare(
    Miracle
    GIT_REPOSITORY https://github.com/spwn02/Miracle.git
    GIT_TAG master
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Miracle)
endif()

if(NOT TARGET Miracle::Miracle)
  message(
    FATAL_ERROR
      "Switch requires Miracle::Miracle. "
      # TODO: Add standalone Miracle dependency resolution; for now the parent
      # build must provide Miracle before adding Switch.
  )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(switch_fault_isolation_source src/private/posix/FaultIsolation.cxx)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(switch_fault_isolation_source src/private/windows/FaultIsolation.cxx)
else()
  set(switch_fault_isolation_source src/private/unsupported/FaultIsolation.cxx)
endif()

add_library(Switch STATIC)
add_library(Switch::Switch ALIAS Switch)

target_compile_features(Switch PUBLIC cxx_std_26)

set_target_properties(
  Switch
  PROPERTIES CXX_EXTENSIONS OFF
             CXX_STANDARD_REQUIRED ON
             CXX_SCAN_FOR_MODULES ON
             CXX_MODULE_STD ON)

target_sources(
  Switch
  PUBLIC FILE_SET
         switch_public_modules
         TYPE
         CXX_MODULES
         BASE_DIRS
         "${CMAKE_CURRENT_SOURCE_DIR}/src/public"
         FILES
         src/public/Switch.ixx
         src/public/Annotations.ixx
         src/public/Assertions.ixx
         src/public/Context.ixx
         src/public/Diagnostics.ixx
         src/public/Discovery.ixx
         src/public/Environment.ixx
         src/public/Execution.ixx
         src/public/Expressions.ixx
         src/public/Fixtures.ixx
         src/public/Json.ixx
         src/public/Metadata.ixx
         src/public/Policies.ixx
         src/public/Providers.ixx
         src/public/Render.ixx
         src/public/Reporting.ixx
         src/public/Resources.ixx
         src/public/Runner.ixx
         src/public/Session.ixx
         src/public/Task.ixx
  PRIVATE FILE_SET
          switch_internal_modules
          TYPE
          CXX_MODULES
          BASE_DIRS
          "${CMAKE_CURRENT_SOURCE_DIR}/src/internal"
          FILES
          src/internal/FaultIsolation.ixx
          src/internal/Worker.ixx
  PRIVATE src/private/Context.cxx
          src/private/Diagnostics.cxx
          src/private/Discovery.cxx
          src/private/Environment.cxx
          src/private/Execution.cxx
          src/private/Json.cxx
          src/private/Policies.cxx
          src/private/Providers.cxx
          src/private/Render.cxx
          src/private/Reporting.cxx
          src/private/Resources.cxx
          src/private/Runner.cxx
          src/private/Task.cxx
          src/private/Worker.cxx
          "${switch_fault_isolation_source}")

target_link_libraries(Switch PUBLIC Miracle::Miracle)

set(switch_private_warning_options
    -Wall
    -Wextra
    -Wpedantic
    -Wunused-local-typedefs
    -Wunused-result
    -Wwrite-strings
    -Wno-missing-designated-field-initializers)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(
    APPEND
    switch_private_warning_options
    -Wconversion-null
    -Woverlength-strings
    -Wpointer-arith
    -Wvarargs
    -Wvla)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  list(
    APPEND
    switch_private_warning_options
    -Woverlength-strings
    -Wpointer-arith
    -Wvarargs
    -Wvla
    -Wmodule-file-config-mismatch)
endif()

option(SWITCH_WARNINGS_AS_ERRORS
       "Treat warnings emitted while compiling Switch itself as errors" OFF)

if(SWITCH_WARNINGS_AS_ERRORS)
  list(APPEND switch_private_warning_options -Werror)
endif()

target_compile_options(Switch PRIVATE ${switch_private_warning_options})

set(switch_cmake_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Switch")

configure_package_config_file(
  cmake/SwitchConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/SwitchConfig.cmake"
  INSTALL_DESTINATION "${switch_cmake_install_dir}")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/SwitchConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SemanticVersion)

install(
  TARGETS Switch
  EXPORT SwitchTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
          FILE_SET switch_public_modules
          DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Switch")

install(
  EXPORT SwitchTargets
  FILE SwitchTargets.cmake
  NAMESPACE Switch::
  DESTINATION "${switch_cmake_install_dir}"
  CXX_MODULES_DIRECTORY modules)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SwitchConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/SwitchConfigVersion.cmake"
        DESTINATION "${switch_cmake_install_dir}")

option(SWITCH_BUILD_TESTS "Build Switch's self-test suite"
       ${PROJECT_IS_TOP_LEVEL})

if(SWITCH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
