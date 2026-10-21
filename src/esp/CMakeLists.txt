# =========================================================
# rgf::esp  (STATIC)
# =========================================================
# Projects entities read through rgf::integrations::fc3 into screen space and draws
# them over the frame. A consumer of rgf::render and a sibling of rgf::menu, so
# it runs with settings alone and needs no user interface present. Windows-only,
# so it is added from src/ only under WIN32.
#
# This library is TIED TO ONE TITLE despite the neutral name: it links
# rgf::integrations::fc3 below and names EntityInfo, Kind, CameraBasis and Diagnostics
# in its own internal/ headers. It needs more of an entity than rgf::aimbot does
# -- the transform and the box for the outlines, the head and spine for the aim
# box, the kind for the colour -- so untying it from a title is the larger of
# the two jobs, not the smaller. It is free of the graphics backend, though:
# only <imgui.h> draw lists, never imgui_impl_*, so a title on OpenGL or Vulkan
# would not change a line here.

add_library(rgf_esp STATIC
  palette.cpp
  draw_entities.cpp
  draw_overlay.cpp
  panel_world.cpp
  panel_entities.cpp
  panel_pools.cpp
  esp.cpp
)
add_library(rgf::esp ALIAS rgf_esp)

target_compile_features(rgf_esp PUBLIC cxx_std_23)

target_include_directories(rgf_esp
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_esp PUBLIC rgf::config rgf::math)
target_link_libraries(rgf_esp PRIVATE rgf::render rgf::integrations::fc3 imgui)

if(TARGET project_options)
  target_link_libraries(rgf_esp PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_esp PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_esp PRIVATE project_coverage)
endif()
