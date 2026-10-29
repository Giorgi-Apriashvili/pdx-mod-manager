# Placeholder until the module is implemented.
add_library(pmm_persistence INTERFACE)
target_link_libraries(pmm_persistence INTERFACE pmm_core)
add_library(PMM::persistence ALIAS pmm_persistence)
