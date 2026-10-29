# Qt application layer (docs/app.md):
#   pmm_appcore  composition root, services worker thread, Steam helper and Web
#                API hosts, GUI-ready snapshots (Qt Core + Network only)
#   pmm_appgui   QML-facing models and controllers (docs/ui-contract.md)
#   pmm-app      the desktop executable (QML module Pmm.Ui from ui/)
set(CMAKE_AUTOMOC ON)

add_library(pmm_appcore STATIC
  core/app_runtime.cpp
  core/app_runtime.hpp
  core/file_log_sink.cpp
  core/file_log_sink.hpp
  core/metadata_host.cpp
  core/metadata_host.hpp
  core/qt_util.cpp
  core/qt_util.hpp
  core/service_runner.cpp
  core/service_runner.hpp
  core/session_host.cpp
  core/session_host.hpp
  core/view_data.cpp
  core/view_data.hpp)
target_link_libraries(pmm_appcore PUBLIC pmm_services Qt6::Core Qt6::Network)
pmm_configure_target(pmm_appcore)

add_library(pmm_appgui STATIC
  gui/acquisition_controller.cpp
  gui/acquisition_controller.hpp
  gui/app_settings.cpp
  gui/app_settings.hpp
  gui/backend.cpp
  gui/backend.hpp
  gui/games_model.cpp
  gui/games_model.hpp
  gui/inventory_model.cpp
  gui/inventory_model.hpp
  gui/jobs_model.cpp
  gui/jobs_model.hpp
  gui/launch_controller.cpp
  gui/launch_controller.hpp
  gui/mod_details.cpp
  gui/mod_details.hpp
  gui/mod_filter_model.cpp
  gui/mod_filter_model.hpp
  gui/mod_list_model.cpp
  gui/mod_list_model.hpp
  gui/notification_center.cpp
  gui/notification_center.hpp
  gui/playsets_model.cpp
  gui/playsets_model.hpp
  gui/share_controller.cpp
  gui/share_controller.hpp)
target_link_libraries(pmm_appgui PUBLIC pmm_appcore Qt6::Gui Qt6::Qml)
target_compile_definitions(pmm_appgui PUBLIC
  PMM_APP_VERSION="${PROJECT_VERSION}"
  PMM_SHARE_PROTOCOL_VERSION=${PMM_SHARE_PROTOCOL_VERSION})
pmm_configure_target(pmm_appgui)

qt_add_executable(pmm-app WIN32 MACOSX_BUNDLE
  gui/main.cpp
  gui/verification.cpp
  gui/verification.hpp)
target_link_libraries(pmm-app PRIVATE pmm_appgui pmm_uiplugin Qt6::Quick Qt6::QuickControls2)
pmm_configure_target(pmm-app)
# Next to pmm-steam-helper, which the app looks for in its own folder.
set_target_properties(pmm-app PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.pmm.ParadoxModManager"
  MACOSX_BUNDLE_BUNDLE_NAME "Paradox Mod Manager"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
if(APPLE AND TARGET pmm-steam-helper)
  # Inside the bundle the helper sits next to the executable (Contents/MacOS).
  add_custom_command(TARGET pmm-app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:pmm-steam-helper>"
            "$<TARGET_FILE_DIR:pmm-app>/"
    VERBATIM)
  add_dependencies(pmm-app pmm-steam-helper)
endif()

# Packaging (docs/packaging.md): cmake/Packaging.cmake installs the helper next
# to the executable and the MSVC runtime, Qt's script deploys Qt itself. Pmm.Ui
# is a static module linked into pmm-app, so the deployment's import scan is
# pointed at its QML folders (not ui/dev) to find the Qt Quick modules to ship.
set_target_properties(pmm-app PROPERTIES QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS
  "${PROJECT_SOURCE_DIR}/ui/controls;${PROJECT_SOURCE_DIR}/ui/panes;${PROJECT_SOURCE_DIR}/ui/dialogs")
install(TARGETS pmm-app
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin)
qt_generate_deploy_qml_app_script(TARGET pmm-app
  OUTPUT_SCRIPT pmm_deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR
  NO_COMPILER_RUNTIME
  DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM)
install(SCRIPT "${pmm_deploy_script}")
