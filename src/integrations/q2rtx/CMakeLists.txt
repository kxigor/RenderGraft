add_library(rgf_integrations_q2rtx STATIC
  q2rtx.cpp
)
add_library(rgf::integrations::q2rtx ALIAS rgf_integrations_q2rtx)

target_compile_features(rgf_integrations_q2rtx PUBLIC cxx_std_23)

target_include_directories(rgf_integrations_q2rtx
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_integrations_q2rtx PUBLIC rgf::process rgf::hook rgf::math)

if(TARGET project_options)
  target_link_libraries(rgf_integrations_q2rtx PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_integrations_q2rtx PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_integrations_q2rtx PRIVATE project_coverage)
endif()
