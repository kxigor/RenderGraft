cmake_minimum_required(VERSION 3.25)

project(RenderGraft LANGUAGES CXX)

# =========================================================
# 0. STANDALONE vs SUBPROJECT
# =========================================================
# When consumed via add_subdirectory / FetchContent, dev-time machinery (DLL,
# tests, QA checks, sanitizers, coverage) stays disabled by default and the
# parent project keeps control of global settings.

if(PROJECT_IS_TOP_LEVEL)
  set(RGF_DEV_DEFAULT ON)
else()
  set(RGF_DEV_DEFAULT OFF)
endif()

# The DLL shim is Windows-only; only default it ON when we are top-level AND on
# Windows so a Linux/macOS consumer of the libraries does not trip its guard.
set(RGF_DLL_DEFAULT OFF)
if(RGF_DEV_DEFAULT AND WIN32)
  set(RGF_DLL_DEFAULT ON)
endif()

# =========================================================
# 1. OPTIONS (prefixed RGF_ to keep cache names unique)
# =========================================================
option(RGF_BUILD_DLL          "Build the injectable per-title shims"  ${RGF_DLL_DEFAULT})
option(RGF_BUILD_INJECTOR     "Build the adapter injection launchers" ${RGF_DLL_DEFAULT})
option(RGF_BUILD_TESTS        "Build the GTest test suite"           ${RGF_DEV_DEFAULT})
option(RGF_ENABLE_FORMAT_CHECK "Register clang-format as CTest"      ${RGF_DEV_DEFAULT})
option(RGF_ENABLE_TIDY_CHECK   "Register clang-tidy as CTest"        ${RGF_DEV_DEFAULT})
option(RGF_ENABLE_COVERAGE     "Enable gcov coverage targets"        OFF)

# Format/tidy/coverage only make sense alongside tests.
if(NOT RGF_BUILD_TESTS)
  set(RGF_ENABLE_FORMAT_CHECK OFF CACHE BOOL "Disabled: requires RGF_BUILD_TESTS=ON" FORCE)
  set(RGF_ENABLE_TIDY_CHECK   OFF CACHE BOOL "Disabled: requires RGF_BUILD_TESTS=ON" FORCE)
  set(RGF_ENABLE_COVERAGE     OFF CACHE BOOL "Disabled: requires RGF_BUILD_TESTS=ON" FORCE)
endif()

# =========================================================
# 2. TOP-LEVEL-ONLY GLOBAL SETTINGS
# =========================================================
if(PROJECT_IS_TOP_LEVEL)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
  endif()
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# A preset that targets one architecture states its pointer size here, so that
# configuring it from the wrong MSYS2 shell is an error rather than a build of
# the other architecture into a directory named for this one.
if(RGF_TARGET_POINTER_SIZE AND
   NOT CMAKE_SIZEOF_VOID_P EQUAL RGF_TARGET_POINTER_SIZE)
  math(EXPR RGF_WANTED_BITS "${RGF_TARGET_POINTER_SIZE} * 8")
  math(EXPR RGF_FOUND_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
  message(FATAL_ERROR
    "This preset targets ${RGF_WANTED_BITS}-bit but ${CMAKE_CXX_COMPILER} "
    "produces ${RGF_FOUND_BITS}-bit code.\n"
    "Open the matching MSYS2 shell (MINGW32 for 32-bit, UCRT64 for 64-bit) "
    "and configure again."
  )
endif()

# =========================================================
# 3. DEV-ONLY MACHINERY (skipped for subproject consumers)
# =========================================================
# Included before the subdirectories so the project_options / project_sanitizers
# / project_coverage interface targets exist when src/ and dll/ link them.
if(RGF_BUILD_DLL OR RGF_BUILD_TESTS OR RGF_ENABLE_COVERAGE)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  include(ProjectOptions)
  include(ProjectSanitizers)
  include(ProjectCoverage)
endif()

# =========================================================
# 4. SUBDIRECTORIES
# =========================================================
if(WIN32)
  add_subdirectory(third_party)  # Dear ImGui submodule + Win32/D3D11 backends
endif()

add_subdirectory(src)          # the rgf::* libraries (always)

if(RGF_BUILD_DLL)
  add_subdirectory(dll)        # one shim per title; today only aimbot_fc3.dll
endif()

if(RGF_BUILD_INJECTOR)
  add_subdirectory(injector)
endif()

if(RGF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
