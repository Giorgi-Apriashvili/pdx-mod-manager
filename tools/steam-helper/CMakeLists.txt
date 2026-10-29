# pmm-steam-helper: one process per game that loads the game's own Steam API
# library and answers JSON-lines requests (see docs/steam-workshop.md). No
# Steamworks SDK headers or binaries are used or shipped.
find_package(Threads REQUIRED)

add_executable(pmm-steam-helper
  main.cpp
  steam_api_loader.cpp
  steam_api_loader.hpp)
target_link_libraries(pmm-steam-helper PRIVATE pmm_providers Threads::Threads ${CMAKE_DL_LIBS})
pmm_configure_target(pmm-steam-helper)
# The app and CLI look for the helper next to their own executable, in the build
# tree as in packages, so all three share one output directory.
set_target_properties(pmm-steam-helper PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
