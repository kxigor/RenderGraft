# =========================================================
# rgf::config  (STATIC)
# =========================================================
# Settings shared by rgf::menu and rgf::esp: feature toggles, colours, distance
# limits. It exists so neither of those two has to own the other's state. Plain
# data with no Win32 dependency, so it is built on every platform.

add_library(rgf_config STATIC
  config.cpp
)
add_library(rgf::config ALIAS rgf_config)

target_compile_features(rgf_config PUBLIC cxx_std_23)

target_include_directories(rgf_config
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

if(TARGET project_options)
  target_link_libraries(rgf_config PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_config PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_config PRIVATE project_coverage)
endif()
