# Script behind the `installer` target (cmake/Packaging.cmake): stages the build
# with `cmake --install` and compiles packaging/windows/installer.iss from it.
# Output: <build dir>/dist/ParadoxModManager-<version>-windows-x86_64-setup.exe
#
# Inno Setup 6 is looked up on PATH and in its standard install folders; set the
# ISCC environment variable to use a specific ISCC.exe.
foreach(_var IN ITEMS PMM_SOURCE_DIR PMM_BUILD_DIR PMM_VERSION PMM_CONFIG)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "build-installer: ${_var} is not set; build the 'installer' target")
  endif()
endforeach()

# The license page is written by hand, so its MIT section must not drift from
# LICENSE. Read like an RTF reader does: line breaks in the file are not text (a
# missing space at one would join two words), \par ends a paragraph, and any other
# control word and its one delimiting space disappear. Anything else left over, an
# escaped character for example, counts as a difference.
set(_license_page "${PMM_SOURCE_DIR}/packaging/windows/license-page.rtf")
file(READ "${_license_page}" _page)
set(_mit_begin "{\\*\\pmmmitlicensebegin}")
set(_mit_end "{\\*\\pmmmitlicenseend}")
string(FIND "${_page}" "${_mit_begin}" _begin)
string(FIND "${_page}" "${_mit_end}" _end)
if(_begin LESS 0 OR _end LESS _begin)
  message(FATAL_ERROR "build-installer: ${_license_page} has lost its MIT section markers "
    "${_mit_begin} and ${_mit_end}")
endif()
string(LENGTH "${_mit_begin}" _marker_length)
math(EXPR _begin "${_begin} + ${_marker_length}")
math(EXPR _length "${_end} - ${_begin}")
string(SUBSTRING "${_page}" ${_begin} ${_length} _page_mit)
string(REGEX REPLACE "[\r\n]" "" _page_mit "${_page_mit}")
string(REGEX REPLACE "\\\\par([^a-z])" " \\1" _page_mit "${_page_mit}")
string(REGEX REPLACE "\\\\[a-z]+-?[0-9]* ?" "" _page_mit "${_page_mit}")
string(REGEX REPLACE "[{}]" "" _page_mit "${_page_mit}")
string(REGEX REPLACE " +" " " _page_mit "${_page_mit}")
string(STRIP "${_page_mit}" _page_mit)

file(READ "${PMM_SOURCE_DIR}/LICENSE" _license)
string(REGEX REPLACE "[ \t\r\n]+" " " _license "${_license}")
string(STRIP "${_license}" _license)

if(NOT "${_page_mit}" STREQUAL "${_license}")
  message(FATAL_ERROR "build-installer: the MIT section of ${_license_page} does not match "
    "LICENSE word for word. Correct the page; both, without line breaks:\n"
    "page:    ${_page_mit}\nLICENSE: ${_license}")
endif()

set(_pf86 "ProgramFiles(x86)")
if(DEFINED ENV{ISCC})
  set(_iscc "$ENV{ISCC}")
else()
  find_program(_iscc ISCC NO_CACHE
    PATHS "$ENV{${_pf86}}/Inno Setup 6" "$ENV{ProgramFiles}/Inno Setup 6"
    "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6")
endif()
if(NOT _iscc OR NOT EXISTS "${_iscc}")
  message(FATAL_ERROR "build-installer: ISCC.exe not found. Install Inno Setup 6 "
    "(https://jrsoftware.org/isdl.php or `choco install innosetup`) or set the ISCC "
    "environment variable to its full path.")
endif()

set(_stage "${PMM_BUILD_DIR}/installer-stage")
set(_output "${PMM_BUILD_DIR}/dist")
file(REMOVE_RECURSE "${_stage}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${PMM_BUILD_DIR}" --prefix "${_stage}"
  --config "${PMM_CONFIG}"
  OUTPUT_QUIET
  COMMAND_ERROR_IS_FATAL ANY)

foreach(_file IN ITEMS bin/pmm-app.exe bin/pmm-steam-helper.exe bin/Qt6Core.dll LICENSE)
  if(NOT EXISTS "${_stage}/${_file}")
    message(FATAL_ERROR "build-installer: ${_file} is missing from ${_stage}")
  endif()
endforeach()

file(TO_NATIVE_PATH "${_stage}" _stage_native)
file(TO_NATIVE_PATH "${_license_page}" _license_native)
file(TO_NATIVE_PATH "${_output}" _output_native)
execute_process(
  COMMAND "${_iscc}" /Q "/DAppVersion=${PMM_VERSION}" "/DStageDir=${_stage_native}"
  "/DLicensePath=${_license_native}" "/O${_output_native}"
  "${PMM_SOURCE_DIR}/packaging/windows/installer.iss"
  COMMAND_ERROR_IS_FATAL ANY)

set(_installer "${_output}/ParadoxModManager-${PMM_VERSION}-windows-x86_64-setup.exe")
if(NOT EXISTS "${_installer}")
  message(FATAL_ERROR "build-installer: ISCC finished but ${_installer} does not exist")
endif()
message(STATUS "Installer: ${_installer}")
