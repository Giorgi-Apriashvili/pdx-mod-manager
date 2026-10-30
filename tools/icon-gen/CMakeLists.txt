# Regenerates the application icon from packaging/icons (docs/packaging.md). Only
# with -DPMM_BUILD_ICON_TOOL=ON, never part of a normal build, never installed: the
# application does not link Qt Svg because of this tool.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Svg)

add_executable(pmm-icon-gen EXCLUDE_FROM_ALL main.cpp)
target_link_libraries(pmm-icon-gen PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)
pmm_configure_target(pmm-icon-gen)

# Overwrites the generated files in the source tree. Qt's DLLs have to be found when
# the tool starts.
add_custom_target(icons
  COMMAND "${CMAKE_COMMAND}" -E env
    "--modify" "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>" --
    "$<TARGET_FILE:pmm-icon-gen>"
    "${PROJECT_SOURCE_DIR}/packaging/icons"
    "${PROJECT_SOURCE_DIR}/packaging/windows"
  COMMENT "Regenerating the application icon from packaging/icons"
  VERBATIM)
add_dependencies(icons pmm-icon-gen)
