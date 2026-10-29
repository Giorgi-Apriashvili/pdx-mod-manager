# Game adapters: Clausewitz descriptors, launcher-settings.json and
# supported_version checks.
add_library(pmm_games STATIC
  descriptor.cpp
  launcher_settings.cpp
  supported_version.cpp)
target_link_libraries(pmm_games
  PUBLIC pmm_core pmm_platform
  PRIVATE nlohmann_json::nlohmann_json)
pmm_configure_target(pmm_games)
add_library(PMM::games ALIAS pmm_games)
