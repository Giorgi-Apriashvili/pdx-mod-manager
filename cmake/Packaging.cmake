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
# Packages: Windows ZIP (+ NSIS when makensis is found), macOS DragNDrop (.dmg),
# Linux TGZ. File names: ParadoxModManager-<version>-<os>-<arch>.
include_guard(GLOBAL)

if(NOT PROJECT_IS_TOP_LEVEL)
  return()
endif()

include(GNUInstallDirs)

set(PMM_APP_TARGET pmm-app)
set(PMM_HELPER_TARGET pmm-steam-helper)
set(PMM_PACKAGE_VENDOR "Giorgi Apriashvili")

# --- Install layout -----------------------------------------------------------
# The helper must sit next to the application executable: bin/ on Windows and
# Linux, <bundle>.app/Contents/MacOS on macOS (signed with its own entitlements,
# see packaging/macos/).
set(_pmm_bundle_name "")
if(APPLE AND TARGET ${PMM_APP_TARGET})
  get_target_property(_pmm_is_bundle ${PMM_APP_TARGET} MACOSX_BUNDLE)
  if(_pmm_is_bundle)
    get_target_property(_pmm_output_name ${PMM_APP_TARGET} OUTPUT_NAME)
    if(NOT _pmm_output_name)
      set(_pmm_output_name ${PMM_APP_TARGET})
    endif()
    set(_pmm_bundle_name "${_pmm_output_name}.app")
  endif()
endif()

if(_pmm_bundle_name)
  set(PMM_INSTALL_HELPER_DIR "${_pmm_bundle_name}/Contents/MacOS")
else()
  set(PMM_INSTALL_HELPER_DIR "${CMAKE_INSTALL_BINDIR}")
endif()

# Windows and macOS packages keep the documents at the package root; Linux
# follows the FHS layout of the TGZ (share/doc/ParadoxModManager).
if(WIN32 OR APPLE)
  set(PMM_INSTALL_DOC_DIR ".")
  set(PMM_INSTALL_LICENSES_DIR "licenses")
else()
  set(PMM_INSTALL_DOC_DIR "${CMAKE_INSTALL_DOCDIR}")
  set(PMM_INSTALL_LICENSES_DIR "${CMAKE_INSTALL_DOCDIR}/licenses")
endif()

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
  if(_pmm_bundle_name)
    # Users usually copy only the bundle out of the disk image, so the notices
    # travel inside it as well.
    install(FILES ${_pmm_notice_files} DESTINATION "${_pmm_bundle_name}/Contents/Resources/doc")
    install(FILES ${_pmm_qt_licenses}
      DESTINATION "${_pmm_bundle_name}/Contents/Resources/doc/licenses")
  endif()
  if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/paradox-mod-manager.desktop"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
  endif()
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
if(WIN32)
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
endif()

# --- Package metadata ---------------------------------------------------------
if(WIN32)
  set(_pmm_os windows)
elseif(APPLE)
  set(_pmm_os macos)
else()
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" _pmm_os)
endif()

if(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
  set(_pmm_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
  list(LENGTH CMAKE_OSX_ARCHITECTURES _pmm_arch_count)
  if(_pmm_arch_count GREATER 1)
    set(_pmm_arch universal)
  else()
    set(_pmm_arch "${CMAKE_OSX_ARCHITECTURES}")
  endif()
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

# Installers display the license; NSIS and DragNDrop expect a .txt file.
configure_file("${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_BINARY_DIR}/packaging/LICENSE.txt" COPYONLY)
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_BINARY_DIR}/packaging/LICENSE.txt")

if(TARGET ${PMM_APP_TARGET})
  set(CPACK_PACKAGE_EXECUTABLES "${PMM_APP_TARGET}" "Paradox Mod Manager")
endif()

# --- Generators -----------------------------------------------------------------
if(WIN32)
  set(CPACK_GENERATOR ZIP)
  set(_pmm_pf86 "ProgramFiles(x86)")
  find_program(PMM_MAKENSIS_EXECUTABLE makensis
    PATHS "$ENV{ProgramFiles}/NSIS" "$ENV{${_pmm_pf86}}/NSIS"
    DOC "NSIS compiler; enables the NSIS installer package when found")
  if(PMM_MAKENSIS_EXECUTABLE)
    list(APPEND CPACK_GENERATOR NSIS)
    set(CPACK_NSIS_EXECUTABLE "${PMM_MAKENSIS_EXECUTABLE}")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_PACKAGE_NAME "Paradox Mod Manager")
    set(CPACK_NSIS_DISPLAY_NAME "Paradox Mod Manager ${PROJECT_VERSION}")
    set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
  endif()
elseif(APPLE)
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_DMG_VOLUME_NAME "Paradox Mod Manager")
  set(CPACK_DMG_FORMAT UDZO)
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
  # Optional signing of the staged bundle before the disk image is built.
  set(PMM_MACOS_CODESIGN_IDENTITY "" CACHE STRING
    "codesign identity for packages, e.g. \"Developer ID Application: Name (TEAMID)\"; empty: unsigned")
  if(PMM_MACOS_CODESIGN_IDENTITY AND _pmm_bundle_name)
    set(CPACK_PMM_CODESIGN_IDENTITY "${PMM_MACOS_CODESIGN_IDENTITY}")
    set(CPACK_PMM_APP_BUNDLE "${_pmm_bundle_name}")
    set(CPACK_PMM_HELPER_NAME "${PMM_HELPER_TARGET}")
    set(CPACK_PMM_ENTITLEMENTS_DIR "${PROJECT_SOURCE_DIR}/packaging/macos")
    set(CPACK_PRE_BUILD_SCRIPTS "${PROJECT_SOURCE_DIR}/packaging/macos/codesign.cmake")
  endif()
else()
  set(CPACK_GENERATOR TGZ)
endif()

# Source packages are produced with `git archive`; keep CPack's source target
# from sweeping up build trees and local state if someone runs it anyway.
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES
  "/\\.git/" "/\\.git$" "/\\.claude/" "/\\.vs/" "/\\.vscode/" "/\\.idea/" "/\\.cache/"
  "/build/" "/out/" "/install/" "/dist/" "/CMakeUserPresets\\.json$")

include(CPack)
