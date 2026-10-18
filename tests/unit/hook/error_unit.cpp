#include <gtest/gtest.h>

#include <rgf/hook/error.hpp>
#include <system_error>

TEST(HookErrorUnit, ReportsAnUnknownCodeAsUnknown) {
  const std::error_code kUnknown{99, rgf::hook::ErrorCategory()};

  EXPECT_EQ(kUnknown.message(), "unknown rgf::hook error");
}
