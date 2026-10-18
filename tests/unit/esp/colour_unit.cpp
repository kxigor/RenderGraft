#include <gtest/gtest.h>

#include <cstdint>
#include <rgf/esp/internal/palette.hpp>
#include <rgf/games/fc3/internal/kind.hpp>
#include <rgf/games/fc3/internal/structs.hpp>

namespace {

using rgf::games::fc3::AgentKind;
using rgf::games::fc3::Kind;
using rgf::games::fc3::PawnInfo;

[[nodiscard]] PawnInfo Pirate() {
  return PawnInfo{.kind = AgentKind::Pawn,
                  .army = 1,
                  .faction = rgf::games::fc3::kFactionPirate,
                  .agentType = rgf::games::fc3::kAgentUnknown,
                  .ownArmyBit = 0x02,
                  .hostileMask = 0x03,
                  .reverseHostileMask = 0x00,
                  .dead = false};
}

}  // namespace

// Death outranks the kind, so a body is never mistaken for a standing threat at
// a glance -- which is the whole reason the overlay colours it separately.
TEST(EspColourUnit, TheDeadKeepTheirOwnColourWhateverTheyWere) {
  rgf::games::fc3::EntityInfo entity{};
  entity.pawn = Pirate();
  entity.pawn->dead = true;

  const std::uint32_t kDead{rgf::esp::ColourOf(entity, Kind::Enemy)};
  entity.pawn->dead = false;

  EXPECT_NE(kDead, rgf::esp::ColourOf(entity, Kind::Enemy));
}

// Every kind the engine can name has to answer, including the ones the overlay
// does not draw: a missing case would fall through to the prop colour and paint
// a man green.
TEST(EspColourUnit, EveryKindHasAColour) {
  const rgf::games::fc3::EntityInfo kEntity{};

  for (const Kind kKind :
       {Kind::Self, Kind::Enemy, Kind::Ally, Kind::Civilian, Kind::Predator,
        Kind::Animal, Kind::Bird, Kind::Vehicle, Kind::Prop}) {
    EXPECT_NE(rgf::esp::ColourOf(kEntity, kKind), 0U);
  }
}
