#include <gtest/gtest.h>

#include <rgf/remote/error.hpp>
#include <system_error>

TEST(RemoteErrorUnit, ReportsAnUnknownCodeAsUnknown) {
  const std::error_code kUnknown{99, rgf::remote::ErrorCategory()};

  EXPECT_EQ(kUnknown.message(), "unknown rgf::remote error");
}
