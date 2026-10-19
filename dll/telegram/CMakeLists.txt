if(NOT WIN32)
  message(FATAL_ERROR "The Telegram module targets Windows. Configure under MSYS2 UCRT64 (or MSVC).")
endif()

add_library(rgf_telegram SHARED
  dllmain.cpp
  diagnostics.cpp
  diagnostics.hpp
  gui_dispatch.cpp
  gui_dispatch.hpp
  internal/build.hpp
  raster_marker.cpp
  raster_marker.hpp
  render_probe.cpp
  render_probe.hpp
  render_target.cpp
  render_target.hpp
  widget_refresh.cpp
  widget_refresh.hpp
)

set_target_properties(rgf_telegram PROPERTIES
  PREFIX ""
  OUTPUT_NAME "rgf_telegram"
)

target_compile_features(rgf_telegram PRIVATE cxx_std_23)

target_link_libraries(rgf_telegram PRIVATE rgf::module rgf::hook rgf::qt rgf::win psapi)

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
