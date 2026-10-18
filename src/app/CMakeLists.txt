# =========================================================
# rgf::app  (STATIC)
# =========================================================
add_library(rgf_app STATIC
  app.cpp
  detour_guard.cpp
  fault.cpp
  log.cpp
  module.cpp
)
add_library(rgf::app ALIAS rgf_app)

target_compile_features(rgf_app PUBLIC cxx_std_23)

target_include_directories(rgf_app
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

# A game builds its own hooks with rgf::hook and hands them to ScopedHooks, so
# the hook library is part of what rgf::app's users compile against.
target_link_libraries(rgf_app PUBLIC rgf::hook)
target_link_libraries(rgf_app PRIVATE rgf::render rgf::menu rgf::config rgf::win)

if(TARGET project_options)
  target_link_libraries(rgf_app PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_app PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_app PRIVATE project_coverage)
endif()
