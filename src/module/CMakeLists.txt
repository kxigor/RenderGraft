add_library(rgf_module STATIC entry.cpp)
add_library(rgf::module ALIAS rgf_module)

target_compile_features(rgf_module PUBLIC cxx_std_23)

target_include_directories(rgf_module
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rgf_module PRIVATE rgf::win)

if(TARGET project_options)
  target_link_libraries(rgf_module PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_module PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_module PRIVATE project_coverage)
endif()
