#include <gtest/gtest.h>

#include <rgf/config/config.hpp>

TEST(ConfigUnit, StartsWithTheEspOnAndNothingPending) {
  const rgf::config::Settings kDefaults{};

  EXPECT_TRUE(kDefaults.espEnabled);
  EXPECT_TRUE(kDefaults.menuVisible);
  EXPECT_FALSE(kDefaults.unloadRequested);
}

TEST(ConfigUnit, HandsOutOneSharedInstance) {
  rgf::config::Settings& settings{rgf::config::Current()};
  const bool kWas{settings.espEnabled.load()};

  settings.espEnabled.store(!kWas);
  EXPECT_EQ(rgf::config::Current().espEnabled.load(), !kWas);

  settings.espEnabled.store(kWas);
  EXPECT_EQ(rgf::config::Current().espEnabled.load(), kWas);
}

// The engine's update thread reads this toggle while the render thread writes
// it from the menu, so a plain bool would be a data race by the standard even
// where the hardware never tears it.
TEST(ConfigUnit, TheEspToggleIsSafeToShareBetweenThreads) {
  EXPECT_TRUE(rgf::config::Current().espEnabled.is_lock_free());
}
