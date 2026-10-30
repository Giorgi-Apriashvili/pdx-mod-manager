cmake_minimum_required(VERSION 3.25)

# On Windows default to MSVC, which matches the msvc2022_64 Qt binaries, unless
# a compiler was chosen explicitly (-DCMAKE_CXX_COMPILER, CC/CXX). Otherwise a
# clang on PATH could be picked up when the VS environment is not loaded.
if(CMAKE_HOST_WIN32 AND NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
  set(CMAKE_C_COMPILER cl)
  set(CMAKE_CXX_COMPILER cl)
endif()

project(ParadoxModManager
  VERSION 0.1.0
  DESCRIPTION "Compact cross-platform mod manager for Paradox games"
  HOMEPAGE_URL "https://github.com/Giorgi-Apriashvili/pdx-mod-manager"
  LANGUAGES C CXX)

# The repository the application checks for releases (docs/updates.md); the
# one place its slug is defined is HOMEPAGE_URL above.
string(REPLACE "https://github.com/" "" PMM_GITHUB_REPOSITORY "${PROJECT_HOMEPAGE_URL}")

# The share-code protocol is versioned independently of the application.
set(PMM_SHARE_PROTOCOL_VERSION 1)

option(PMM_BUILD_APP "Build the Qt Quick desktop application (requires Qt 6)" ON)
option(PMM_BUILD_TESTS "Build the test suites" ON)
option(PMM_BUILD_STEAM_HELPER "Build the Steam Workshop helper executable" ON)
option(PMM_ENABLE_SANITIZERS "Enable AddressSanitizer/UBSan where supported" OFF)
option(PMM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

if(PMM_BUILD_APP)
  # Local convenience: with no Qt location given (QT_ROOT_DIR, CMAKE_PREFIX_PATH
  # or Qt6_DIR), use the newest standard installation and say which one.
  if(NOT DEFINED Qt6_DIR AND "${CMAKE_PREFIX_PATH}" STREQUAL "")
    file(GLOB _pmm_qt_candidates LIST_DIRECTORIES true
      "C:/Qt/6.*/msvc*_64" "$ENV{HOME}/Qt/6.*/gcc_64" "$ENV{HOME}/Qt/6.*/macos")
    if(_pmm_qt_candidates)
      list(SORT _pmm_qt_candidates COMPARE NATURAL ORDER DESCENDING)
      list(GET _pmm_qt_candidates 0 _pmm_qt_root)
      message(STATUS "QT_ROOT_DIR not set; using Qt found at ${_pmm_qt_root}")
      list(APPEND CMAKE_PREFIX_PATH "${_pmm_qt_root}")
    endif()
  endif()
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Concurrent)
  qt_standard_project_setup(REQUIRES 6.5)
  # qt_standard_project_setup turns AUTOMOC/AUTOUIC on for every target; the
  # Qt-free libraries must not get them. Qt targets enable AUTOMOC themselves
  # (qt_add_qml_module does; src/app sets CMAKE_AUTOMOC in its own scope).
  set(CMAKE_AUTOMOC OFF)
  set(CMAKE_AUTOUIC OFF)
  add_subdirectory(ui)
endif()

add_subdirectory(src)

if(PMM_BUILD_STEAM_HELPER)
  add_subdirectory(tools/steam-helper)
endif()

if(PMM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(Packaging)
