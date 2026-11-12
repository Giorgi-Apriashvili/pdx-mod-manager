# Application-layer tests (label pmm_app_tests): models, controllers, the
# services worker, the Steam helper host against the real pmm-steam-helper and
# the fake Steam API library, and the Web API host against a local stub.
find_package(Qt6 REQUIRED COMPONENTS Test)
set(CMAKE_AUTOMOC ON)

add_executable(pmm_app_tests
  test_main.cpp
  test_support.hpp
  metadata_host_tests.cpp
  models_tests.cpp
  performance_tests.cpp
  qml_tests.cpp
  runner_tests.cpp
  runtime_tests.cpp
  session_host_tests.cpp
  ui_text_tests.cpp
  update_auto_tests.cpp
  update_stub.hpp
  update_tests.cpp)
target_link_libraries(pmm_app_tests PRIVATE
  Catch2::Catch2 pmm_appgui pmm_uiplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test Qt6::Network
  nlohmann_json::nlohmann_json)
target_compile_definitions(pmm_app_tests PRIVATE
  PMM_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
  PMM_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/ui"
  PMM_ECHO_EXE="$<TARGET_FILE:pmm_platform_echo>")
add_dependencies(pmm_app_tests pmm_platform_echo)
pmm_configure_target(pmm_app_tests)

if(TARGET pmm-steam-helper AND TARGET pmm_fake_steam_api)
  target_compile_definitions(pmm_app_tests PRIVATE
    PMM_STEAM_HELPER_PATH="$<TARGET_FILE:pmm-steam-helper>"
    PMM_FAKE_STEAM_API_PATH="$<TARGET_FILE:pmm_fake_steam_api>")
  add_dependencies(pmm_app_tests pmm-steam-helper pmm_fake_steam_api)
endif()

# Qt's DLLs are found through PATH on Windows: DL_PATHS prepends the Qt bin
# folder (ENVIRONMENT_MODIFICATION) for test discovery and every test.
set(_pmm_qt_bin "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
catch_discover_tests(pmm_app_tests
  DISCOVERY_MODE PRE_TEST
  DL_PATHS "${_pmm_qt_bin}"
  PROPERTIES
    LABELS pmm_app_tests
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The QML tests against the fake backend (ui/dev/tests, label ui-preview): the
# run of `ui/dev/preview.sh --test`, without bash. Like preview.sh, a qmldir
# that lists the QML_FILES of ui/CMakeLists.txt points the module Pmm.Ui at the
# sources, so no build of the module is needed. Registered only when Qt's
# qmltestrunner is there.
find_program(PMM_QMLTESTRUNNER qmltestrunner HINTS "${_pmm_qt_bin}")
if(PMM_QMLTESTRUNNER)
  set(_pmm_preview_module "${CMAKE_BINARY_DIR}/ui-preview/Pmm/Ui")
  file(STRINGS "${PROJECT_SOURCE_DIR}/ui/CMakeLists.txt" _pmm_ui_lines)
  set(_pmm_qmldir "module Pmm.Ui\n")
  set(_pmm_in_files FALSE)
  foreach(_pmm_line IN LISTS _pmm_ui_lines)
    if(_pmm_line MATCHES "QML_FILES")
      set(_pmm_in_files TRUE)
    elseif(_pmm_in_files AND _pmm_line MATCHES "^\\)")
      set(_pmm_in_files FALSE)
    elseif(_pmm_in_files AND _pmm_line MATCHES "^[ \t]*([^ \t]+\\.(qml|js))[ \t]*$")
      set(_pmm_file "${CMAKE_MATCH_1}")
      get_filename_component(_pmm_type "${_pmm_file}" NAME_WE)
      file(RELATIVE_PATH _pmm_path "${_pmm_preview_module}" "${PROJECT_SOURCE_DIR}/ui/${_pmm_file}")
      if(_pmm_type STREQUAL "Theme")
        string(APPEND _pmm_qmldir "singleton ")
      endif()
      string(APPEND _pmm_qmldir "${_pmm_type} 1.0 ${_pmm_path}\n")
    endif()
  endforeach()
  file(CONFIGURE OUTPUT "${_pmm_preview_module}/qmldir" CONTENT "${_pmm_qmldir}" @ONLY)
  add_test(NAME pmm_ui_preview
    COMMAND "${PMM_QMLTESTRUNNER}"
      -import "${CMAKE_BINARY_DIR}/ui-preview"
      -import "${PROJECT_SOURCE_DIR}/ui/dev"
      -input "${PROJECT_SOURCE_DIR}/ui/dev/tests")
  set_tests_properties(pmm_ui_preview PROPERTIES
    LABELS ui-preview
    TIMEOUT 600
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_pmm_qt_bin}")
else()
  message(STATUS "qmltestrunner not found: the ui-preview tests are not registered")
endif()
