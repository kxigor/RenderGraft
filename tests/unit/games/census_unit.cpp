#include <gtest/gtest.h>

#include <cstdint>
#include <rgf/games/fc3/fc3.hpp>
#include <rgf/games/fc3/internal/limits.hpp>
#include <support/fc3_world.hpp>
#include <tuple>

namespace {

using rgf::testing::Fc3World;

class Fc3CensusUnit : public Fc3World {};

TEST_F(Fc3CensusUnit, CountsGroupsAndTheirEntities) {
  const rgf::games::fc3::EntityCensus kCensus{
      Walk(At(rgf::testing::kScheduler))};

  EXPECT_EQ(kCensus.groups, 2U);
  EXPECT_EQ(kCensus.entities,
            rgf::testing::kEntitiesOne + rgf::testing::kEntitiesTwo);
  EXPECT_EQ(kCensus.skipped, 0U);
}

TEST_F(Fc3CensusUnit, EveryFrameIsCounted) {
  const std::uint32_t kBefore{rgf::games::fc3::LastCensus().frames};

  rgf::games::fc3::PublishCensus();
  rgf::games::fc3::PublishCensus();

  EXPECT_EQ(rgf::games::fc3::LastCensus().frames, kBefore + 2);
}

// One pass is not a whole world -- the engine walks it across several -- so a
// generation must add them up. Publishing each pass on its own is what made the
// overlay flicker.
TEST_F(Fc3CensusUnit, SeveralPassesInOneFrameAccumulate) {
  rgf::games::fc3::TakeCensus(At(rgf::testing::kScheduler));
  rgf::games::fc3::TakeCensus(At(rgf::testing::kScheduler));
  Settle(At(rgf::testing::kScheduler));

  const rgf::games::fc3::EntityCensus kCensus{rgf::games::fc3::LastCensus()};

  EXPECT_EQ(kCensus.calls, 2U);
  EXPECT_EQ(kCensus.groups, 4U);
  EXPECT_EQ(kCensus.entities,
            2 * (rgf::testing::kEntitiesOne + rgf::testing::kEntitiesTwo));
}

// A frame the engine did not walk keeps the last complete world rather than
// blanking: the reader owns its buffer until a new generation is ready.
TEST_F(Fc3CensusUnit, AFrameWithoutAWalkKeepsTheLastWorld) {
  std::ignore = Walk(At(rgf::testing::kScheduler));

  rgf::games::fc3::PublishCensus();

  const rgf::games::fc3::EntityCensus kCensus{rgf::games::fc3::LastCensus()};

  EXPECT_EQ(kCensus.entities,
            rgf::testing::kEntitiesOne + rgf::testing::kEntitiesTwo);
}

TEST_F(Fc3CensusUnit, AnUnreadableSchedulerCountsNothing) {
  const rgf::games::fc3::EntityCensus kCensus{Walk(0)};

  EXPECT_EQ(kCensus.groups, 0U);
  EXPECT_EQ(kCensus.entities, 0U);
}

TEST_F(Fc3CensusUnit, AGroupWithoutAnEntityGroupIsSkipped) {
  Poke(rgf::testing::kGroupTwo + rgf::games::fc3::kGroupEntityGroup, 0);

  const rgf::games::fc3::EntityCensus kCensus{
      Walk(At(rgf::testing::kScheduler))};

  EXPECT_EQ(kCensus.groups, 1U);
  EXPECT_EQ(kCensus.entities, rgf::testing::kEntitiesOne);
}

TEST_F(Fc3CensusUnit, AnEmptyGroupIsSkipped) {
  Poke(rgf::testing::kEntityGroupTwo + rgf::games::fc3::kEntityGroupCount, 0);

  const rgf::games::fc3::EntityCensus kCensus{
      Walk(At(rgf::testing::kScheduler))};

  EXPECT_EQ(kCensus.groups, 1U);
  EXPECT_EQ(kCensus.entities, rgf::testing::kEntitiesOne);
}

// Capped rather than refused: a scheduler with a wrong count still holds real
// groups at the front of its array.
TEST_F(Fc3CensusUnit, AnImplausibleGroupCountIsCapped) {
  constexpr std::uint32_t kAbsurd{100000};
  Poke(rgf::testing::kScheduler + rgf::games::fc3::kSchedulerGroupCount,
       kAbsurd);

  const rgf::games::fc3::EntityCensus kCensus{
      Walk(At(rgf::testing::kScheduler))};

  EXPECT_EQ(kCensus.skipped, kAbsurd - rgf::games::fc3::kMaxGroups);
}

}  // namespace
