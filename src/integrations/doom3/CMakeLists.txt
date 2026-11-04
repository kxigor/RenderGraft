add_library(rgf_integrations_doom3 STATIC
  doom3.cpp
)
add_library(rgf::integrations::doom3 ALIAS rgf_integrations_doom3)

target_compile_features(rgf_integrations_doom3 PUBLIC cxx_std_23)

target_include_directories(rgf_integrations_doom3
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_integrations_doom3 PUBLIC rgf::process rgf::hook rgf::math)

if(TARGET project_options)
  target_link_libraries(rgf_integrations_doom3 PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_integrations_doom3 PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_integrations_doom3 PRIVATE project_coverage)
endif()
