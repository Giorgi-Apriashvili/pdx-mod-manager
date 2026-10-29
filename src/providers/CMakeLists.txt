# Placeholder until the module is implemented.
add_library(pmm_providers INTERFACE)
target_link_libraries(pmm_providers INTERFACE pmm_core)
add_library(PMM::providers ALIAS pmm_providers)
