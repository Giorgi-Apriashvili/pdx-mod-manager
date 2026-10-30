# Install layout, Windows file metadata and CPack configuration (see
# docs/packaging.md).
#
# Included at the end of the top-level CMakeLists.txt, after every target is
# defined, so the rules below can depend on which targets exist. The Qt
# application (pmm-app) installs itself and runs Qt's deployment script from
# src/app; this file adds what ships next to it: the Steam helper, the
# documentation and the license texts. It works with or without pmm-app (for
# example with the core-debug preset, which packages the helper and docs).
#
# Package: a ZIP. File name: ParadoxModManager-<version>-windows-<arch>. The
# installer is the separate `installer` target at the end of this file.
include_guard(GLOBAL)

if(NOT PROJECT_IS_TOP_LEVEL)
  return()
endif()

include(GNUInstallDirs)

set(PMM_APP_TARGET pmm-app)
set(PMM_HELPER_TARGET pmm-steam-helper)
set(PMM_PACKAGE_VENDOR "Giorgi Apriashvili")

# --- Install layout -----------------------------------------------------------
# The helper must sit next to the application executable in bin/.
set(PMM_INSTALL_HELPER_DIR "${CMAKE_INSTALL_BINDIR}")

# The documents stay at the package root.
set(PMM_INSTALL_DOC_DIR ".")
set(PMM_INSTALL_LICENSES_DIR "licenses")

if(TARGET ${PMM_HELPER_TARGET})
  install(TARGETS ${PMM_HELPER_TARGET} RUNTIME DESTINATION "${PMM_INSTALL_HELPER_DIR}")
endif()

set(_pmm_notice_files
  "${PROJECT_SOURCE_DIR}/LICENSE"
  "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
install(FILES ${_pmm_notice_files}
  "${PROJECT_SOURCE_DIR}/README.md"
  "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
  DESTINATION "${PMM_INSTALL_DOC_DIR}")

# Qt is distributed under the LGPLv3, which incorporates the GPLv3: both texts
# ship whenever the Qt application is packaged.
if(TARGET ${PMM_APP_TARGET})
  set(_pmm_qt_licenses
    "${PROJECT_SOURCE_DIR}/packaging/licenses/LGPL-3.0.txt"
    "${PROJECT_SOURCE_DIR}/packaging/licenses/GPL-3.0.txt")
  install(FILES ${_pmm_qt_licenses} DESTINATION "${PMM_INSTALL_LICENSES_DIR}")
endif()

# App-local Microsoft C++ runtime, so the ZIP runs without a separate VC++
# Redistributable installation. The Universal CRT is part of Windows 10+.
if(MSVC)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
  set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
  include(InstallRequiredSystemLibraries)
endif()

# --- Windows executables --------------------------------------------------------
# Every shipped executable gets a version resource (Explorer > Properties >
# Details). With MSVC it records only the PDB's file name instead of the
# absolute build-tree path (/PDBALTPATH; PDBs are not packaged), and the
# RelWithDebInfo build of the release preset is linked without incremental
# linking and with unreferenced code removed, which halves the executables.
set(PMM_RC_VERSION_NUMBERS
  "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(PMM_RC_VENDOR "${PMM_PACKAGE_VENDOR}")
foreach(_pmm_exe IN ITEMS ${PMM_APP_TARGET} ${PMM_HELPER_TARGET})
  if(NOT TARGET ${_pmm_exe})
    continue()
  endif()
  set(PMM_RC_NAME "${_pmm_exe}")
  if(_pmm_exe STREQUAL PMM_APP_TARGET)
    set(PMM_RC_DESCRIPTION "Paradox Mod Manager")
  else()
    set(PMM_RC_DESCRIPTION "Paradox Mod Manager Steam Workshop helper")
  endif()
  set(_pmm_rc "${PROJECT_BINARY_DIR}/packaging/${_pmm_exe}.rc")
  configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/version.rc.in" "${_pmm_rc}" @ONLY)
  target_sources(${_pmm_exe} PRIVATE "${_pmm_rc}")
  if(MSVC)
    target_link_options(${_pmm_exe} PRIVATE "/PDBALTPATH:%_PDB%"
      "$<$<CONFIG:RelWithDebInfo>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF>")
  endif()
endforeach()

# --- Package metadata ---------------------------------------------------------
set(_pmm_os windows)

if(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
  set(_pmm_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
else()
  set(_pmm_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
string(TOLOWER "${_pmm_arch}" _pmm_arch)
if(_pmm_arch MATCHES "^(amd64|x64|x86_64)$")
  set(_pmm_arch x86_64)
elseif(_pmm_arch MATCHES "^(arm64|aarch64)$")
  set(_pmm_arch arm64)
endif()

set(CPACK_PACKAGE_NAME "ParadoxModManager")
set(CPACK_PACKAGE_VENDOR "${PMM_PACKAGE_VENDOR}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ParadoxModManager")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${_pmm_os}-${_pmm_arch}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_STRIP_FILES OFF)

# --- Generators -----------------------------------------------------------------
set(CPACK_GENERATOR ZIP)

# Source packages are produced with `git archive`; keep CPack's source target
# from sweeping up build trees and local state if someone runs it anyway.
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES
  "/\\.git/" "/\\.git$" "/\\.claude/" "/\\.vs/" "/\\.vscode/" "/\\.idea/" "/\\.cache/"
  "/build/" "/out/" "/install/" "/dist/" "/CMakeUserPresets\\.json$")

include(CPack)

# --- Windows installer ----------------------------------------------------------
# Not a CPack generator: Inno Setup compiles packaging/windows/installer.iss from
# the staged install tree. `cmake --build <build dir> --target installer` builds
# what is installed and writes <build dir>/dist/<package name>-setup.exe.
if(TARGET ${PMM_APP_TARGET} AND TARGET ${PMM_HELPER_TARGET})
  add_custom_target(installer
    COMMAND "${CMAKE_COMMAND}"
      "-DPMM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DPMM_BUILD_DIR=${PROJECT_BINARY_DIR}"
      "-DPMM_VERSION=${PROJECT_VERSION}"
      "-DPMM_CONFIG=$<CONFIG>"
      -P "${PROJECT_SOURCE_DIR}/packaging/windows/build-installer.cmake"
    USES_TERMINAL
    VERBATIM)
  add_dependencies(installer ${PMM_APP_TARGET} ${PMM_HELPER_TARGET})
endif()
