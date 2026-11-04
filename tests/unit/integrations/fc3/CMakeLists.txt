add_gtest(fc3_unit
  SOURCES
    census_unit.cpp
    snapshot_unit.cpp
    handoff_unit.cpp
    bones_unit.cpp
    entity_unit.cpp
    kind_unit.cpp
    error_unit.cpp
  LIBRARIES rgf::integrations::fc3 project_options project_sanitizers project_coverage
  INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/tests/unit
  PROPERTIES TIMEOUT 30
)
