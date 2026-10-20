add_gtest(telegram_unit
  SOURCES
    raster_marker_unit.cpp
    render_target_unit.cpp
    ${PROJECT_SOURCE_DIR}/dll/telegram/raster_marker.cpp
    ${PROJECT_SOURCE_DIR}/dll/telegram/render_target.cpp
  LIBRARIES
    rgf::win rgf::hook rgf::qt project_options project_sanitizers project_coverage
  INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/dll/telegram
  PROPERTIES TIMEOUT 30
)
