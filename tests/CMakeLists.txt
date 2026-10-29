include(Catch)

# pmm_add_test(<name> SOURCES <files...> LIBS <targets...>)
# Builds a Catch2 test executable and registers each test case with CTest.
# PMM_FIXTURES_DIR points at tests/fixtures (read-only; copy before mutating).
function(pmm_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain ${ARG_LIBS})
  target_compile_definitions(${name} PRIVATE
    PMM_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
  pmm_configure_target(${name})
  # Clang 22+ reports Catch2's use of __COUNTER__ as a C2y extension under
  # -Wpedantic, which would break warnings-as-errors builds.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 22)
    target_compile_options(${name} PRIVATE -Wno-c2y-extensions)
  endif()
  catch_discover_tests(${name} DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS "${name}")
endfunction()

foreach(module core platform sharing persistence games providers)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit/${module}/CMakeLists.txt")
    add_subdirectory(unit/${module})
  endif()
endforeach()

if(PMM_BUILD_APP AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
  add_subdirectory(app)
endif()
