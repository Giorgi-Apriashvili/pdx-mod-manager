pmm_add_test(pmm_core_tests
  SOURCES
    catalog_tests.cpp
    log_tests.cpp
    sha256_tests.cpp
    dependencies_tests.cpp
    import_plan_tests.cpp
    playset_ops_tests.cpp
    readiness_tests.cpp
  LIBS pmm_core)
