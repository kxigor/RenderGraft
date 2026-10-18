#include <gtest/gtest.h>

#include <cstdint>
#include <rgf/games/fc3/fc3.hpp>
#include <rgf/games/fc3/internal/layout.hpp>
#include <rgf/math/vec3.hpp>
#include <support/fc3_world.hpp>
#include <tuple>

namespace {

using rgf::testing::Fc3World;
using rgf::testing::kAgentOne;
using rgf::testing::kEntityArrayOne;
using rgf::testing::kEntityArrayTwo;
using rgf::testing::kEntityOne;
using rgf::testing::kEntityTwo;
using rgf::testing::kScheduler;

// Everything here drives the half of the census the ESP actually draws from.
// Before these, no test ever put a non-null pointer in an entity array, so the
// whole snapshot path ran in no test at all.
class Fc3SnapshotUnit : public Fc3World {
 protected:
  void SetUp() override {
    Fc3World::SetUp();
    if (page_ == nullptr) {
      return;
    }

    PlaceEntity(kEntityOne, 100.0F, 200.0F, 0.6F, 1.7F);
    PlaceEntity(kEntityTwo, 300.0F, 400.0F, 2.0F, 5.0F);

    Poke(kEntityArrayOne, At(kEntityOne));
    Poke(kEntityArrayTwo, At(kEntityTwo));
  }

  [[nodiscard]] const rgf::games::fc3::EntitySnapshot& WalkAndRead() {
    std::ignore = Walk(At(kScheduler));

    return rgf::games::fc3::LastSnapshot();
  }
};

TEST_F(Fc3SnapshotUnit, PublishesTheEntitiesTheWalkFound) {
  const rgf::games::fc3::EntitySnapshot& kSnapshot{WalkAndRead()};

  ASSERT_EQ(kSnapshot.count, 2U);
  EXPECT_EQ(kSnapshot.humanoids, 1U);
  EXPECT_EQ(kSnapshot.dropped, 0U);
}

TEST_F(Fc3SnapshotUnit, TellsAHumanoidShapedBoxFromAProp) {
  const rgf::games::fc3::EntitySnapshot& kSnapshot{WalkAndRead()};
  ASSERT_EQ(kSnapshot.count, 2U);

  EXPECT_TRUE(kSnapshot.items.at(0).humanoid);
  EXPECT_FALSE(kSnapshot.items.at(1).humanoid);
  EXPECT_EQ(kSnapshot.items.at(0).address, At(kEntityOne));
}

TEST_F(Fc3SnapshotUnit, CarriesThePositionThroughUnchanged) {
  const rgf::games::fc3::EntitySnapshot& kSnapshot{WalkAndRead()};
  ASSERT_EQ(kSnapshot.count, 2U);

  const rgf::math::Vec3 kPosition{
      kSnapshot.items.at(0).transform.Translation()};
  EXPECT_FLOAT_EQ(kPosition.x, 100.0F);
  EXPECT_FLOAT_EQ(kPosition.y, 200.0F);
}

TEST_F(Fc3SnapshotUnit, GivesAHumanoidItsPawnRecordWhenTheFieldsAreInRange) {
  Poke(kAgentOne + rgf::games::fc3::kAgentArmy, 2);
  Poke(kAgentOne + rgf::games::fc3::kAgentFaction, 1);
  Poke(kAgentOne + rgf::games::fc3::kAgentType, 3);

  const rgf::games::fc3::EntitySnapshot& kSnapshot{WalkAndRead()};
  ASSERT_EQ(kSnapshot.count, 2U);
  ASSERT_TRUE(kSnapshot.items.at(0).pawn.has_value());
  EXPECT_EQ(kSnapshot.items.at(0).pawn->army, 2);
  EXPECT_EQ(kSnapshot.items.at(0).pawn->agentType, 3U);
  EXPECT_EQ(kSnapshot.pawns, 1U);
}

// ownArmyBit == 1 << army is a real relation, but only for a pawn placed on the
// map: CBasePawnEntity_RegisterProperties attaches OnPostLoad_InitArmyMasks as
// a post-load callback, so a spawned pawn never runs it. Gating on it dropped
// every spawned NPC in the game, which is why it must not decide this.
TEST_F(Fc3SnapshotUnit, KeepsThePawnRecordWhenTheArmyBitDisagrees) {
  Poke(kAgentOne + rgf::games::fc3::kAgentArmy, 2);
  Poke(kAgentOne + rgf::games::fc3::kPawnAgentArmyBlock, 1U);

  const rgf::games::fc3::EntitySnapshot& kSnapshot{WalkAndRead()};
  ASSERT_EQ(kSnapshot.count, 2U);
  ASSERT_TRUE(kSnapshot.items.at(0).pawn.has_value());
  EXPECT_EQ(kSnapshot.items.at(0).pawn->army, 2);
  EXPECT_EQ(kSnapshot.pawns, 1U);
}

// What the record exists for is the agent's class, and that is known before any
// field is read. A field that reads back nonsense costs that field, not the
// record: dropping the whole thing is how an entity ended up classified by the
// size of its bounding box instead.
TEST_F(Fc3SnapshotUnit, FallsBackOnAFieldRatherThanDroppingTheRecord) {
  Poke(kAgentOne + rgf::games::fc3::kAgentType, 9);
  Poke(kAgentOne + rgf::games::fc3::kAgentArmy, 99);
  Poke(kAgentOne + rgf::games::fc3::kAgentFaction, 77);

  const rgf::games::fc3::EntitySnapshot& kSnapshot{WalkAndRead()};
  ASSERT_EQ(kSnapshot.count, 2U);
  ASSERT_TRUE(kSnapshot.items.at(0).pawn.has_value());

  const rgf::games::fc3::PawnInfo& kPawn{*kSnapshot.items.at(0).pawn};
  EXPECT_EQ(kPawn.agentType, rgf::games::fc3::kAgentUnknown);
  EXPECT_EQ(kPawn.army, rgf::games::fc3::kNoArmy);
  EXPECT_EQ(kPawn.faction, rgf::games::fc3::kFactionUnknown);
}

// The second entity is built without an agent at all, so it stands for
// everything in the world that is scenery: no record, and nothing downstream
// may invent a kind for it.
TEST_F(Fc3SnapshotUnit, WithholdsThePawnRecordWhenThereIsNoAgent) {
  const rgf::games::fc3::EntitySnapshot& kSnapshot{WalkAndRead()};

  ASSERT_EQ(kSnapshot.count, 2U);
  EXPECT_FALSE(kSnapshot.items.at(1).pawn.has_value());
  EXPECT_EQ(kSnapshot.pawns, 1U);
}

// The handoff itself: a walk must not change what the reader is looking at
// until the frame that publishes it. Before the triple buffer, the walk wrote
// into the very array the reader was iterating.
TEST_F(Fc3SnapshotUnit, AWalkDoesNotDisturbTheSnapshotAlreadyPublished) {
  std::ignore = Walk(At(kScheduler));
  ASSERT_EQ(rgf::games::fc3::LastSnapshot().count, 2U);

  Poke(kEntityArrayOne, 0);
  rgf::games::fc3::PublishCensus();
  rgf::games::fc3::TakeCensus(At(kScheduler));

  EXPECT_EQ(rgf::games::fc3::LastSnapshot().count, 2U);

  Settle(At(kScheduler));

  EXPECT_EQ(rgf::games::fc3::LastSnapshot().count, 1U);
}

// The reader holds a reference across a whole frame, so the buffer behind it
// must not be the one the next walk writes into.
TEST_F(Fc3SnapshotUnit, TheReaderKeepsItsBufferWhileTheNextWalkRuns) {
  std::ignore = Walk(At(kScheduler));

  const rgf::games::fc3::EntitySnapshot& kHeld{rgf::games::fc3::LastSnapshot()};
  ASSERT_EQ(kHeld.count, 2U);

  Poke(kEntityArrayOne, 0);
  Poke(kEntityArrayTwo, 0);
  rgf::games::fc3::TakeCensus(At(kScheduler));
  rgf::games::fc3::TakeCensus(At(kScheduler));

  EXPECT_EQ(kHeld.count, 2U);
  EXPECT_EQ(kHeld.items.at(0).address, At(kEntityOne));
}

}  // namespace
