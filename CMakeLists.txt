cmake_minimum_required(VERSION 3.25)

project(ParadoxModManager
  VERSION 0.1.0
  DESCRIPTION "Compact cross-platform mod manager for Paradox games"
  HOMEPAGE_URL "https://example.invalid/pdx-mod-manager"
  LANGUAGES C CXX)

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
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Concurrent)
  qt_standard_project_setup(REQUIRES 6.5)
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
