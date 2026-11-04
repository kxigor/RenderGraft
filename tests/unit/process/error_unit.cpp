#include <gtest/gtest.h>

#include <rgf/process/error.hpp>
#include <system_error>

TEST(ProcessErrorUnit, ReportsAnUnknownCodeAsUnknown) {
  const std::error_code kUnknown{99, rgf::process::ErrorCategory()};

  EXPECT_EQ(kUnknown.message(), "unknown rgf::process error");
}
