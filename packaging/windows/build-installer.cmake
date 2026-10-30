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

# The repository keeps LF line endings; the license page gets CRLF text.
file(READ "${_stage}/LICENSE" _license)
string(REPLACE "\r\n" "\n" _license "${_license}")
string(REPLACE "\n" "\r\n" _license "${_license}")
set(_license_file "${PMM_BUILD_DIR}/packaging/installer-license.txt")
file(WRITE "${_license_file}" "${_license}")

file(TO_NATIVE_PATH "${_stage}" _stage_native)
file(TO_NATIVE_PATH "${_license_file}" _license_native)
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
