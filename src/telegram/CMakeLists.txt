add_library(rgf_telegram_core STATIC
  internal/build.hpp
  message_text_hook.cpp
  raster_marker.cpp
  render_probe.cpp
  render_target.cpp
  widget_refresh.cpp
)
add_library(rgf::telegram ALIAS rgf_telegram_core)

target_compile_features(rgf_telegram_core PUBLIC cxx_std_23)

target_include_directories(rgf_telegram_core
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_telegram_core PUBLIC rgf::hook rgf::qt rgf::win)

if(TARGET project_options)
  target_link_libraries(rgf_telegram_core PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_telegram_core PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_telegram_core PRIVATE project_coverage)
endif()
