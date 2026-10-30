# Third-party dependencies, pinned by version and SHA-256 (see docs/adr/0002-dependencies.md).
#
# All non-Qt libraries are fetched from their upstream release archives with
# FetchContent so that every build uses the exact same versions. Qt 6 is a
# toolchain-level prerequisite located through CMAKE_PREFIX_PATH (QT_ROOT_DIR in
# the presets); its version is pinned in CI and documented in the README.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# --- nlohmann/json 3.12.0 (MIT) ---------------------------------------------
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# --- zlib 1.3.1 (zlib license) ----------------------------------------------
# Built from source as a private static library. Z_PREFIX renames every symbol
# so it can never clash with a system libz loaded by Qt.
FetchContent_Declare(pmm_zlib_src
  URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
  URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SOURCE_SUBDIR _pmm_not_a_cmake_project)
FetchContent_MakeAvailable(pmm_zlib_src)
set(_zlib_dir "${pmm_zlib_src_SOURCE_DIR}")
add_library(pmm_zlib STATIC
  ${_zlib_dir}/adler32.c ${_zlib_dir}/compress.c ${_zlib_dir}/crc32.c ${_zlib_dir}/deflate.c
  ${_zlib_dir}/infback.c ${_zlib_dir}/inffast.c ${_zlib_dir}/inflate.c ${_zlib_dir}/inftrees.c
  ${_zlib_dir}/trees.c ${_zlib_dir}/uncompr.c ${_zlib_dir}/zutil.c)
target_include_directories(pmm_zlib SYSTEM PUBLIC "${_zlib_dir}")
target_compile_definitions(pmm_zlib PUBLIC Z_PREFIX)
target_compile_definitions(pmm_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
target_compile_options(pmm_zlib PRIVATE /W0)
add_library(PMM::zlib ALIAS pmm_zlib)

# --- SQLite 3.50.4 amalgamation (public domain) -------------------------------
FetchContent_Declare(pmm_sqlite_src
  URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
  URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SOURCE_SUBDIR _pmm_not_a_cmake_project)
FetchContent_MakeAvailable(pmm_sqlite_src)
add_library(pmm_sqlite3 STATIC "${pmm_sqlite_src_SOURCE_DIR}/sqlite3.c")
target_include_directories(pmm_sqlite3 SYSTEM PUBLIC "${pmm_sqlite_src_SOURCE_DIR}")
target_compile_definitions(pmm_sqlite3 PRIVATE
  SQLITE_DQS=0
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_OMIT_DEPRECATED
  SQLITE_USE_URI=0)
target_compile_options(pmm_sqlite3 PRIVATE /W0)
add_library(PMM::sqlite3 ALIAS pmm_sqlite3)

# --- Catch2 3.8.1 (BSL-1.0), tests only ---------------------------------------
if(PMM_BUILD_TESTS)
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.1.tar.gz
    URL_HASH SHA256=18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
  set(CATCH_INSTALL_DOCS OFF CACHE INTERNAL "")
  set(CATCH_INSTALL_EXTRAS OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
