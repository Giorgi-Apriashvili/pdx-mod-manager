# Qt-free application services: orchestrate discovery, playsets, sharing,
# acquisition and launch for the Qt app (docs/architecture-services.md).
add_library(pmm_services STATIC
  acquisition_coordinator.cpp
  app_paths.cpp
  app_update.cpp
  diagnostics.cpp
  game_service.cpp
  internal/util.cpp
  launch_service.cpp
  playset_service.cpp
  playset_view.cpp
  service_context.cpp
  share_service.cpp)
target_link_libraries(pmm_services
  PUBLIC pmm_core pmm_platform pmm_sharing pmm_persistence pmm_games pmm_providers
  PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(pmm_services PRIVATE
  PMM_GITHUB_REPOSITORY="${PMM_GITHUB_REPOSITORY}")
pmm_configure_target(pmm_services)
add_library(PMM::services ALIAS pmm_services)
