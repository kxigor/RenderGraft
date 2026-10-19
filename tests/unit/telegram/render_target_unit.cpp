#include <gtest/gtest.h>

#include <system_error>

#include "render_target.hpp"

TEST(TelegramRenderTargetUnit, RejectsAnExecutableOutsideTheSupportedBuild) {
  const auto kTarget{rgf::telegram::RenderTarget::Locate()};

  ASSERT_FALSE(kTarget.has_value());
  EXPECT_EQ(kTarget.error(), std::make_error_code(std::errc::not_supported));
}
