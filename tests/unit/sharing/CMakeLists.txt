pmm_add_test(pmm_sharing_tests
  SOURCES
    codec_tests.cpp
    manifest_tests.cpp
    share_code_tests.cpp
  LIBS pmm_sharing PMM::zlib)
