# =========================================================
# rgf::games::doom3  (STATIC)
# =========================================================
# Doom 3 (id Tech 4) specific bindings. The renderer is OpenGL, so this title is
# what forces rgf::render to stop assuming a DXGI swap chain. Its engine source
# was published under the GPL, which makes every offset checkable against real
# code rather than only against a disassembly.
# Skeleton for now. One target per game so you link only the title you target.

add_library(rgf_games_doom3 STATIC
  doom3.cpp
)
add_library(rgf::games::doom3 ALIAS rgf_games_doom3)

target_compile_features(rgf_games_doom3 PUBLIC cxx_std_23)

target_include_directories(rgf_games_doom3
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_games_doom3 PUBLIC rgf::process rgf::hook rgf::math)

if(TARGET project_options)
  target_link_libraries(rgf_games_doom3 PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_games_doom3 PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_games_doom3 PRIVATE project_coverage)
endif()
