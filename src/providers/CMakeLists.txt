# Mod providers: Steam Workshop helper protocol, acquisition state machine,
# Web API metadata, manual fallback links and local-mod fingerprints.
add_library(pmm_providers STATIC
  acquisition.cpp
  fallback.cpp
  helper_protocol.cpp
  local_fingerprint.cpp
  web_metadata.cpp
  workshop_item.cpp)
target_link_libraries(pmm_providers
  PUBLIC pmm_core
  PRIVATE nlohmann_json::nlohmann_json)
pmm_configure_target(pmm_providers)
add_library(PMM::providers ALIAS pmm_providers)
