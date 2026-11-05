include(FetchContent)

function(rgf_add_minhook)
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  FetchContent_Declare(rgf_minhook
    URL https://codeload.github.com/TsudaKageyu/minhook/tar.gz/refs/tags/v1.3.4
    URL_HASH SHA256=1aebeae4ca898330c507860acc2fca2eb335fe446a3a2b8444c3bf8b2660a14e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
  )
  FetchContent_MakeAvailable(rgf_minhook)
endfunction()

rgf_add_minhook()
