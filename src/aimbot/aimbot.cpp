#include <optional>
#include <rgf/aimbot/aimbot.hpp>
#include <rgf/games/fc3/fc3.hpp>
#include <rgf/math/direction.hpp>
#include <rgf/math/ray.hpp>
#include <rgf/math/vec3.hpp>

namespace rgf::aimbot {

math::Vec3 AimPoint(const games::fc3::EntityInfo& entity) noexcept {
  if (entity.head) {
    return *entity.head;
  }
  if (entity.body) {
    return *entity.body;
  }

  return entity.transform.TransformPoint(entity.box.Centre());
}

std::optional<Target> CandidateOf(const games::fc3::EntityInfo& entity,
                                  const math::Ray& view, const Choice& choice,
                                  RankFn rank,
                                  const games::fc3::PawnInfo* self) noexcept {
  const std::optional<Ranking> kRanking{rank(entity, self)};
  if (!kRanking) {
    return std::nullopt;
  }

  const math::Vec3 kPoint{AimPoint(entity)};
  const math::Vec3 kOffset{kPoint - view.origin};
  const float kDistance{kOffset.Length()};
  if (!choice.InRange(kDistance)) {
    return std::nullopt;
  }

  const math::Angle kError{math::AngleBetween(view.direction, kOffset)};
  if (!choice.InCone(kError)) {
    return std::nullopt;
  }

  return Target{/*entity   = */ entity.address,
                /*point    = */ kPoint,
                /*error    = */ kError,
                /*distance = */ kDistance,
                /*ranking  = */ *kRanking};
}

std::optional<Target> ChooseTarget(const games::fc3::EntitySnapshot& snapshot,
                                   const math::Ray& view, const Choice& choice,
                                   RankFn rank,
                                   const games::fc3::PawnInfo* self) noexcept {
  std::optional<Target> best;
  float bestScore{};

  for (const games::fc3::EntityInfo& kEntity : games::fc3::Entities(snapshot)) {
    const std::optional<Target> kCandidate{
        CandidateOf(kEntity, view, choice, rank, self)};
    if (!kCandidate) {
      continue;
    }

    const float kScore{choice.ScoreOf(*kCandidate)};
    if (best && bestScore >= kScore) {
      continue;
    }

    bestScore = kScore;
    best = kCandidate;
  }

  return best;
}

}  // namespace rgf::aimbot
