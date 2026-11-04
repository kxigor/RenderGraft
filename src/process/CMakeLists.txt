# =========================================================
# rgf::process  (STATIC)
# =========================================================
# Convenient access to the host process from inside it: module bases and reads
# that report an unreadable address instead of faulting. Windows-only, so it is
# added from src/ only under WIN32.

add_library(rgf_process STATIC
  memory.cpp
  scan.cpp
  error.cpp
)
add_library(rgf::process ALIAS rgf_process)

target_compile_features(rgf_process PUBLIC cxx_std_23)

# Public headers live in the top-level include/ (sibling of src/).
target_include_directories(rgf_process
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

# Dev-time interface targets only exist when top-level dev machinery is enabled;
# guard on TARGET so the library still configures as a subproject dependency.
if(TARGET project_options)
  target_link_libraries(rgf_process PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_process PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_process PRIVATE project_coverage)
endif()
