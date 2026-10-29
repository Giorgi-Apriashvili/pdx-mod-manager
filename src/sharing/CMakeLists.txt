# Placeholder until the module is implemented.
add_library(pmm_sharing INTERFACE)
target_link_libraries(pmm_sharing INTERFACE pmm_core)
add_library(PMM::sharing ALIAS pmm_sharing)
