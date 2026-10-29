add_library(pmm_sharing STATIC
  codec.cpp
  manifest.cpp
  share_code.cpp
  utf8.cpp)
target_link_libraries(pmm_sharing
  PUBLIC pmm_core
  PRIVATE nlohmann_json::nlohmann_json PMM::zlib)
pmm_configure_target(pmm_sharing)
add_library(PMM::sharing ALIAS pmm_sharing)
