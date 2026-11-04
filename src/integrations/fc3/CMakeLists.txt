add_library(rgf_integrations_fc3 STATIC
  module.cpp
  camera.cpp
  kind.cpp
  class_registry.cpp
  components.cpp
  entity.cpp
  agent.cpp
  pawn.cpp
  bones.cpp
  census.cpp
  update.cpp
  snapshot.cpp
  error.cpp
  weapon.cpp
  trace.cpp
  reach.cpp
)
add_library(rgf::integrations::fc3 ALIAS rgf_integrations_fc3)

target_compile_features(rgf_integrations_fc3 PUBLIC cxx_std_23)

target_include_directories(rgf_integrations_fc3
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_integrations_fc3 PUBLIC rgf::process rgf::hook rgf::math)

if(TARGET project_options)
  target_link_libraries(rgf_integrations_fc3 PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_integrations_fc3 PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_integrations_fc3 PRIVATE project_coverage)
endif()
