add_library(rgf_qt STATIC
  raster_canvas.cpp
  raster_text_hook.cpp
  widget_updater.cpp
)
add_library(rgf::qt ALIAS rgf_qt)

target_compile_features(rgf_qt PUBLIC cxx_std_23)

target_include_directories(rgf_qt
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

if(TARGET project_options)
  target_link_libraries(rgf_qt PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_qt PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_qt PRIVATE project_coverage)
endif()
