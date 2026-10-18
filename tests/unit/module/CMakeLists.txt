# Real DLL fixtures exercise callback lifetime and self-unload, including
# thread notifications with the injected DLL's static MinGW runtime.
function(add_module_probe target throws)
  add_library(${target} SHARED probe.cpp probe.def)
  set_target_properties(${target} PROPERTIES PREFIX "")
  target_compile_definitions(${target} PRIVATE AFW_MODULE_PROBE_THROWS=${throws})
  target_link_libraries(${target} PRIVATE
    afw::module project_options project_sanitizers project_coverage
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(${target} PRIVATE -Wl,--exclude-all-symbols)
    # A private static x86 DW2 unwinder conflicts with GTest's shared runtime.
    # Keep the normal unload fixture static; share the host runtime only for
    # the throwing x86 fixture. A fully static host can catch both normally.
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4 OR NOT throws)
      target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  endif()
endfunction()

add_module_probe(module_return_probe 0)
add_module_probe(module_throw_probe 1)

add_gtest(module_unit
  SOURCES entry_unit.cpp
  LIBRARIES afw::module project_options project_sanitizers project_coverage
  INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/tests/unit
  COMPILE_DEFINITIONS
    AFW_MODULE_RETURN_PROBE_PATH=L\"$<TARGET_FILE:module_return_probe>\"
    AFW_MODULE_THROW_PROBE_PATH=L\"$<TARGET_FILE:module_throw_probe>\"
  PROPERTIES TIMEOUT 45
)
add_dependencies(module_unit module_return_probe module_throw_probe)
