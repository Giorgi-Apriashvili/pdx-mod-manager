# Child process for the startDetached() tests: reports argv, cwd and an
# environment variable as JSON.
add_executable(pmm_platform_echo echo_main.cpp)
target_link_libraries(pmm_platform_echo PRIVATE pmm_platform nlohmann_json::nlohmann_json)
pmm_configure_target(pmm_platform_echo)

pmm_add_test(pmm_platform_tests
  SOURCES
    fs_tests.cpp
    paths_tests.cpp
    process_tests.cpp
    steam_tests.cpp
  LIBS pmm_platform nlohmann_json::nlohmann_json)
add_dependencies(pmm_platform_tests pmm_platform_echo)
target_compile_definitions(pmm_platform_tests PRIVATE
  PMM_ECHO_EXE="$<TARGET_FILE:pmm_platform_echo>")
if(WIN32)
  target_link_libraries(pmm_platform_tests PRIVATE shell32) # CommandLineToArgvW
endif()
