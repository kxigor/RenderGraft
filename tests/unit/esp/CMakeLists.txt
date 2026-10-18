add_gtest(esp_unit
  SOURCES
    colour_unit.cpp
  LIBRARIES rgf::esp project_options project_sanitizers project_coverage
  INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/tests/unit
  PROPERTIES TIMEOUT 30
)
