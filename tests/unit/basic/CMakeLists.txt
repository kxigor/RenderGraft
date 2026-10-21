add_gtest(basic_unit
  SOURCES basic_unit.cpp
  LIBRARIES rgf::process rgf::integrations::fc3 rgf::aimbot project_options project_sanitizers project_coverage
)
