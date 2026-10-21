if(NOT WIN32)
  message(FATAL_ERROR "The Telegram module targets Windows. Configure under MSYS2 UCRT64 (or MSVC).")
endif()

add_library(rgf_telegram SHARED
  dllmain.cpp
  diagnostics.cpp
  diagnostics.hpp
)

set_target_properties(rgf_telegram PROPERTIES
  PREFIX ""
  OUTPUT_NAME "rgf_telegram"
)

target_compile_features(rgf_telegram PRIVATE cxx_std_23)

set(RGF_TELEGRAM_MASKED_USER_ID "0" CACHE STRING "Telegram user ID whose message text is masked (0 means unconfigured)")
string(LENGTH "${RGF_TELEGRAM_MASKED_USER_ID}" maskedUserIdLength)
if(NOT RGF_TELEGRAM_MASKED_USER_ID MATCHES "^(0|[1-9][0-9]*)$"
    OR maskedUserIdLength GREATER 15
    OR RGF_TELEGRAM_MASKED_USER_ID GREATER 281474976710655)
  message(FATAL_ERROR "RGF_TELEGRAM_MASKED_USER_ID must be a decimal user ID from 1 to 281474976710655, or 0 when unconfigured.")
endif()
target_compile_definitions(rgf_telegram PRIVATE
  RGF_TELEGRAM_MASKED_USER_ID=${RGF_TELEGRAM_MASKED_USER_ID}ULL
)

target_link_libraries(rgf_telegram PRIVATE rgf::module rgf::integrations::telegram rgf::win psapi)

if(TARGET project_options)
  target_link_libraries(rgf_telegram PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_telegram PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_telegram PRIVATE project_coverage)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(rgf_telegram PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
