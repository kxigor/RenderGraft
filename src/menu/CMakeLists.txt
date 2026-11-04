# =========================================================
# rgf::menu  (STATIC)
# =========================================================
# The overlay's user interface, drawn on top of rgf::config. A consumer of
# rgf::render and a sibling of rgf::esp -- the two never reference each other.
# Windows-only, so it is added from src/ only under WIN32.

add_library(rgf_menu STATIC
  menu.cpp
)
add_library(rgf::menu ALIAS rgf_menu)

target_compile_features(rgf_menu PUBLIC cxx_std_23)

target_include_directories(rgf_menu
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_menu PUBLIC rgf::config)
target_link_libraries(rgf_menu PRIVATE rgf::render imgui)

if(TARGET project_options)
  target_link_libraries(rgf_menu PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_menu PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_menu PRIVATE project_coverage)
endif()
