# Layered libraries; dependencies only point downwards:
#   app -> {games, providers, sharing, persistence} -> platform -> core
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(sharing)
add_subdirectory(persistence)
add_subdirectory(games)
add_subdirectory(providers)
add_subdirectory(services)

if(PMM_BUILD_APP)
  add_subdirectory(app)
endif()
