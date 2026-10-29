pmm_add_test(pmm_persistence_tests
  SOURCES
    acquisition_job_tests.cpp
    concurrency_tests.cpp
    database_tests.cpp
    fixture_tests.cpp
    installation_tests.cpp
    inventory_tests.cpp
    launch_history_tests.cpp
    playset_tests.cpp
    settings_tests.cpp
    sqlite_tests.cpp
    text_encoding_tests.cpp
  LIBS pmm_persistence)
