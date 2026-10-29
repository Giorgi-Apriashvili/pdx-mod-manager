add_library(pmm_core STATIC
  catalog.cpp
  log.cpp
  model.cpp
  result.cpp
  sha256.cpp
  dependencies.cpp
  import_plan.cpp
  playset_ops.cpp
  readiness.cpp)
target_include_directories(pmm_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(pmm_core PUBLIC cxx_std_20)
pmm_configure_target(pmm_core)
add_library(PMM::core ALIAS pmm_core)
