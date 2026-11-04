add_gtest(aimbot_unit
  SOURCES
    aimbot_unit.cpp
  LIBRARIES rgf::aimbot project_options project_sanitizers project_coverage
  INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/tests/unit
  PROPERTIES TIMEOUT 30
)
