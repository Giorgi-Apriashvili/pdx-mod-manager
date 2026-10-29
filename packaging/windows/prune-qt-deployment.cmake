# Install-time script (Windows), run right after Qt's deployment script for
# pmm-app (src/app/CMakeLists.txt). Removes what windeployqt copies but the
# application never loads (see docs/packaging.md, "Qt deployment"):
#
# - icuuc.dll / icuin.dll: Qt's Windows builds use the ICU that is part of
#   Windows 10 1703+; windeployqt copies the system's own copy from System32.
#   Versioned names (icuuc74.dll, ...) belong to a Qt built with its own ICU and
#   are kept.
# - The Qt Quick Controls styles other than Basic: main() selects the Basic
#   style, so the other styles' QML modules and libraries are never loaded.
#   They are deployed only because a Qt Quick Dialogs module imports
#   QtQuick.Controls, whose qmldir lists every style as an optional import.
#
# Uses the folders of the Qt deployment that ran before it, with Qt's defaults.
if(NOT QT_DEPLOY_PREFIX)
  set(QT_DEPLOY_PREFIX "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
endif()
if(NOT QT_DEPLOY_BIN_DIR)
  set(QT_DEPLOY_BIN_DIR "bin")
endif()
if(NOT QT_DEPLOY_QML_DIR)
  set(QT_DEPLOY_QML_DIR "qml")
endif()

set(_pmm_bin "${QT_DEPLOY_PREFIX}/${QT_DEPLOY_BIN_DIR}")
set(_pmm_qml "${QT_DEPLOY_PREFIX}/${QT_DEPLOY_QML_DIR}")
if(NOT EXISTS "${_pmm_bin}/Qt6Core.dll")
  message(STATUS "pmm: no Qt deployment in ${_pmm_bin}; nothing to prune")
  return()
endif()

set(_pmm_unused "${_pmm_bin}/icuuc.dll" "${_pmm_bin}/icuin.dll" "${_pmm_qml}/QtQuick/NativeStyle")
foreach(_pmm_style IN ITEMS Fusion Imagine Material Universal FluentWinUI3 Windows macOS iOS)
  list(APPEND _pmm_unused
    "${_pmm_qml}/QtQuick/Controls/${_pmm_style}"
    "${_pmm_bin}/Qt6QuickControls2${_pmm_style}.dll"
    "${_pmm_bin}/Qt6QuickControls2${_pmm_style}StyleImpl.dll")
endforeach()

foreach(_pmm_path IN LISTS _pmm_unused)
  if(EXISTS "${_pmm_path}")
    file(REMOVE_RECURSE "${_pmm_path}")
    file(RELATIVE_PATH _pmm_rel "${QT_DEPLOY_PREFIX}" "${_pmm_path}")
    message(STATUS "pmm: removed unused ${_pmm_rel}")
  endif()
endforeach()

if(NOT EXISTS "${_pmm_qml}/QtQuick/Controls/Basic/qmldir")
  message(FATAL_ERROR "pmm: the Basic Qt Quick Controls style is missing from ${_pmm_qml}")
endif()
