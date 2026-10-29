# SQLite-backed application state: database, migrations and repositories.
add_library(pmm_persistence STATIC
  acquisition_job_repository.cpp
  database.cpp
  installation_repository.cpp
  internal/mapping.cpp
  inventory_repository.cpp
  launch_history_repository.cpp
  migrations.cpp
  playset_repository.cpp
  settings_repository.cpp
  sqlite.cpp)
target_link_libraries(pmm_persistence
  PUBLIC pmm_core
  PRIVATE pmm_sqlite3 nlohmann_json::nlohmann_json)
pmm_configure_target(pmm_persistence)
add_library(PMM::persistence ALIAS pmm_persistence)
