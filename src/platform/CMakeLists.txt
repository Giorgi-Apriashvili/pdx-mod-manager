add_library(pmm_platform STATIC
  fs.cpp
  os_util.cpp
  paths.cpp
  process.cpp)
target_link_libraries(pmm_platform PUBLIC pmm_core)
if(WIN32)
  # shell32/ole32/uuid: Known Folders.
  target_link_libraries(pmm_platform PRIVATE shell32 ole32 uuid)
else()
  # A detached thread reaps spawned children.
  find_package(Threads REQUIRED)
  target_link_libraries(pmm_platform PRIVATE Threads::Threads)
endif()
pmm_configure_target(pmm_platform)
add_library(PMM::platform ALIAS pmm_platform)
