add_library(rgf_integrations_telegram STATIC
  internal/build.hpp
  history_image_hook.cpp
  message_info.cpp
  message_sender_filter.cpp
  message_text_hook.cpp
  raster_marker.cpp
  render_probe.cpp
  render_target.cpp
)
add_library(rgf::integrations::telegram ALIAS rgf_integrations_telegram)

target_compile_features(rgf_integrations_telegram PUBLIC cxx_std_23)

target_include_directories(rgf_integrations_telegram
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_integrations_telegram PUBLIC rgf::hook rgf::qt rgf::win PRIVATE rgf::process)

if(TARGET project_options)
  target_link_libraries(rgf_integrations_telegram PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_integrations_telegram PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_integrations_telegram PRIVATE project_coverage)
endif()
