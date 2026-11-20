# Game adapters: Clausewitz descriptors, Victoria 3's metadata.json,
# launcher-settings.json, the shared launcher-v2 base, the dlc_load.json-based
# CK3, EU4, Hearts of Iron IV and Stellaris adapters, and the
# content_load.json-based Victoria 3 adapter.
add_library(pmm_games STATIC
  ck3.cpp
  content_load_adapter.cpp
  descriptor.cpp
  dlc_load_adapter.cpp
  eu4.cpp
  hoi4.cpp
  launcher_adapter.cpp
  launcher_settings.cpp
  mod_metadata.cpp
  registry.cpp
  stellaris.cpp
  supported_version.cpp
  vic3.cpp
  workshop_content.cpp)
target_link_libraries(pmm_games
  PUBLIC pmm_core pmm_platform
  PRIVATE nlohmann_json::nlohmann_json)
pmm_configure_target(pmm_games)
add_library(PMM::games ALIAS pmm_games)
