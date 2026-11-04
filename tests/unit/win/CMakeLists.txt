add_library(module_view_probe SHARED module_view_probe.cpp)
set_target_properties(module_view_probe PROPERTIES PREFIX "")
target_link_libraries(module_view_probe PRIVATE
  project_options project_sanitizers project_coverage
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(module_view_probe PRIVATE
    -static -static-libgcc -static-libstdc++ -Wl,--exclude-all-symbols
  )
endif()

add_gtest(win_unit
  SOURCES
    scoped_handle_unit.cpp
    remote_memory_unit.cpp
    com_ptr_unit.cpp
    protect_guard_unit.cpp
    error_unit.cpp
    module_view_unit.cpp
    window_unit.cpp
    window_dispatch_unit.cpp
  LIBRARIES rgf::win project_options project_sanitizers project_coverage
  INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/tests/unit
  COMPILE_DEFINITIONS
    RGF_MODULE_VIEW_PROBE_PATH=L\"$<TARGET_FILE:module_view_probe>\"
  PROPERTIES TIMEOUT 30
)

add_dependencies(win_unit module_view_probe)
