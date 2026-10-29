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
  ui_text_tests.cpp)
target_link_libraries(pmm_app_tests PRIVATE
  Catch2::Catch2 pmm_appgui pmm_uiplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test Qt6::Network)
target_compile_definitions(pmm_app_tests PRIVATE
  PMM_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
  PMM_UI_SOURCE_DIR="${PROJECT_SOURCE_DIR}/ui")
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
