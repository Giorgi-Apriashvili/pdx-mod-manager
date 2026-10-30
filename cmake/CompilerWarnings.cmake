# AddressSanitizer covers the whole build, third-party targets included: the
# MSVC standard library's container annotations must match in every object
# (LNK2038 otherwise). /RTC cannot be combined with it and /INCREMENTAL is
# ignored with a linker warning.
if(PMM_ENABLE_SANITIZERS)
  add_compile_options(/fsanitize=address)
  add_link_options(/INCREMENTAL:NO)
  foreach(_pmm_flags CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
    string(REPLACE "/RTC1" "" ${_pmm_flags} "${${_pmm_flags}}")
  endforeach()
endif()

# UndefinedBehaviorSanitizer with clang-cl. Its runtime libraries are built
# against the static release C runtime, and CMake calls the linker directly, so
# they have to be named here; /IGNORE:4217 is about their imports of that runtime.
if(PMM_ENABLE_UBSAN)
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
    OUTPUT_VARIABLE _pmm_clang_resources
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
  add_compile_options(-fsanitize=undefined -fno-sanitize-recover=undefined)
  add_link_options(/IGNORE:4217
    "${_pmm_clang_resources}/lib/windows/clang_rt.ubsan_standalone-x86_64.lib"
    "${_pmm_clang_resources}/lib/windows/clang_rt.ubsan_standalone_cxx-x86_64.lib")
endif()

# Applies the project's warning and encoding policy to a target.
function(pmm_configure_target target)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl: /W4 is -Wall -Wextra. These keep catching what a second compiler
    # finds and MSVC does not.
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus
      -Wextra -Wshadow -Wnull-dereference -Wmissing-field-initializers -Wold-style-cast)
  else()
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
      /w14242 /w14254 /w14263 /w14265 /w14287 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549
      /w14555 /w14619 /w14640 /w14826 /w14905 /w14906 /w14928)
  endif()
  target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  if(PMM_WARNINGS_AS_ERRORS)
    target_compile_options(${target} PRIVATE /WX)
  endif()
endfunction()
