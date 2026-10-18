add_library(afw_module STATIC entry.cpp)
add_library(afw::module ALIAS afw_module)

target_compile_features(afw_module PUBLIC cxx_std_23)

target_include_directories(afw_module
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(afw_module PRIVATE afw::win)

if(TARGET project_options)
  target_link_libraries(afw_module PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(afw_module PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(afw_module PRIVATE project_coverage)
endif()
