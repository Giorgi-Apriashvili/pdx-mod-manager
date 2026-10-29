# Placeholder until the module is implemented.
add_library(pmm_platform INTERFACE)
target_link_libraries(pmm_platform INTERFACE pmm_core)
add_library(PMM::platform ALIAS pmm_platform)
