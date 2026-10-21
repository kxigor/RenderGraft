add_gtest(qt_unit
  SOURCES
    raster_image_hook_unit.cpp
    raster_text_hook_unit.cpp
    widget_paint_engine_unit.cpp
    widget_updater_unit.cpp
  LIBRARIES
    rgf::qt project_options project_sanitizers project_coverage
)
