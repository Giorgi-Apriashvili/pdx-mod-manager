# Game adapters: Clausewitz descriptors, launcher-settings.json, and the
# dlc_load.json-based EU4 and Stellaris adapters.
add_library(pmm_games STATIC
  descriptor.cpp
  dlc_load_adapter.cpp
  eu4.cpp
  launcher_settings.cpp
  registry.cpp
  stellaris.cpp
  supported_version.cpp)
target_link_libraries(pmm_games
  PUBLIC pmm_core pmm_platform
  PRIVATE nlohmann_json::nlohmann_json)
pmm_configure_target(pmm_games)
add_library(PMM::games ALIAS pmm_games)
