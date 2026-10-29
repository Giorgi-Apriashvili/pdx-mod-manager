pmm_add_test(pmm_providers_tests
  SOURCES
    acquisition_tests.cpp
    helper_process_tests.cpp
    helper_protocol_tests.cpp
    local_fingerprint_tests.cpp
    path_text_tests.cpp
    web_metadata_tests.cpp
    workshop_item_tests.cpp
  LIBS pmm_providers)

# End-to-end helper tests: the real pmm-steam-helper drives a fake Steam API
# library built from fake_steam_api.cpp (our own code, never a Valve binary).
if(TARGET pmm-steam-helper)
  add_library(pmm_fake_steam_api SHARED fake_steam_api.cpp)
  pmm_configure_target(pmm_fake_steam_api)
  find_package(Threads REQUIRED)
  target_link_libraries(pmm_providers_tests PRIVATE Threads::Threads)
  target_compile_definitions(pmm_providers_tests PRIVATE
    PMM_STEAM_HELPER_PATH="$<TARGET_FILE:pmm-steam-helper>"
    PMM_FAKE_STEAM_API_PATH="$<TARGET_FILE:pmm_fake_steam_api>")
  add_dependencies(pmm_providers_tests pmm-steam-helper pmm_fake_steam_api)
endif()
