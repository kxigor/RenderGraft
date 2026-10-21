add_gtest(app_unit
  SOURCES
    scoped_hooks_unit.cpp
  LIBRARIES rgf::app project_options project_sanitizers project_coverage
  INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/tests/unit
  PROPERTIES TIMEOUT 30
)
