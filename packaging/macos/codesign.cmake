# CPack pre-build script (macOS DragNDrop): signs the staged application bundle
# before the disk image is created. Enabled by configuring with
# -DPMM_MACOS_CODESIGN_IDENTITY=<identity> (see cmake/Packaging.cmake and
# docs/packaging.md); "-" signs ad hoc for local testing.
#
# Order matters: every nested binary first (--deep), then the Steam helper again
# with its own entitlements (it loads the game's libsteam_api.dylib, which is
# signed by another team), then the bundle itself so its seal covers the final
# nested signatures. Notarisation of the resulting .dmg is a separate step.
if(NOT CPACK_PMM_CODESIGN_IDENTITY)
  return()
endif()

find_program(PMM_CODESIGN codesign)
if(NOT PMM_CODESIGN)
  message(FATAL_ERROR "codesign: tool not found (Xcode command line tools required)")
endif()

# Locate the staged bundle (the staging layout differs between CPack versions).
file(GLOB_RECURSE _pmm_candidates LIST_DIRECTORIES true
  "${CPACK_TEMPORARY_DIRECTORY}/*${CPACK_PMM_APP_BUNDLE}")
set(_pmm_bundle "")
foreach(_candidate IN LISTS _pmm_candidates)
  get_filename_component(_name "${_candidate}" NAME)
  if(IS_DIRECTORY "${_candidate}" AND _name STREQUAL CPACK_PMM_APP_BUNDLE)
    string(LENGTH "${_candidate}" _len)
    if(NOT _pmm_bundle OR _len LESS _pmm_bundle_len)
      set(_pmm_bundle "${_candidate}")
      set(_pmm_bundle_len ${_len})
    endif()
  endif()
endforeach()
if(NOT _pmm_bundle)
  message(FATAL_ERROR "codesign: ${CPACK_PMM_APP_BUNDLE} not found below ${CPACK_TEMPORARY_DIRECTORY}")
endif()

set(_pmm_common --force --options runtime --sign "${CPACK_PMM_CODESIGN_IDENTITY}")
if(NOT CPACK_PMM_CODESIGN_IDENTITY STREQUAL "-")
  list(APPEND _pmm_common --timestamp) # ad hoc signatures cannot be timestamped
endif()

function(pmm_codesign)
  execute_process(COMMAND "${PMM_CODESIGN}" ${_pmm_common} ${ARGN} RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "codesign failed (${_rc}): ${ARGN}")
  endif()
endfunction()

set(_pmm_helper "${_pmm_bundle}/Contents/MacOS/${CPACK_PMM_HELPER_NAME}")
pmm_codesign(--deep "${_pmm_bundle}")
if(EXISTS "${_pmm_helper}")
  pmm_codesign(--entitlements "${CPACK_PMM_ENTITLEMENTS_DIR}/helper.entitlements" "${_pmm_helper}")
else()
  message(WARNING "codesign: ${_pmm_helper} is missing; Workshop acquisition will not work")
endif()
pmm_codesign(--entitlements "${CPACK_PMM_ENTITLEMENTS_DIR}/app.entitlements" "${_pmm_bundle}")

execute_process(COMMAND "${PMM_CODESIGN}" --verify --deep --strict --verbose=2 "${_pmm_bundle}"
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "codesign: verification of ${_pmm_bundle} failed (${_rc})")
endif()
message(STATUS "codesign: signed ${_pmm_bundle}")
