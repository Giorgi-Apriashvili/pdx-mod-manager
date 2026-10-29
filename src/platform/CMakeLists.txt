add_library(pmm_platform STATIC
  file_transaction.cpp
  fs.cpp
  os_util.cpp
  paths.cpp
  process.cpp
  steam.cpp)
target_link_libraries(pmm_platform
  PUBLIC pmm_core
  PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  # shell32/ole32/uuid: Known Folders; advapi32: registry (Steam discovery).
  target_link_libraries(pmm_platform PRIVATE shell32 ole32 uuid advapi32)
else()
  # A detached thread reaps spawned children.
  find_package(Threads REQUIRED)
  target_link_libraries(pmm_platform PRIVATE Threads::Threads)
endif()
pmm_configure_target(pmm_platform)
add_library(PMM::platform ALIAS pmm_platform)
