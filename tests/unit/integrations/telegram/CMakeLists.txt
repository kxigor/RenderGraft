add_gtest(telegram_unit
  SOURCES
    history_image_hook_unit.cpp
    message_info_unit.cpp
    message_render_hook_unit.cpp
    raster_marker_unit.cpp
    render_target_unit.cpp
  LIBRARIES
    rgf::integrations::telegram project_options project_sanitizers project_coverage
  INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/integrations/telegram
  PROPERTIES TIMEOUT 30
)
