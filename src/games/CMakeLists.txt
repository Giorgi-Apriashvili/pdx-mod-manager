# Placeholder until the module is implemented.
add_library(pmm_games INTERFACE)
target_link_libraries(pmm_games INTERFACE pmm_core)
add_library(PMM::games ALIAS pmm_games)
