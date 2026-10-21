#pragma once

#include <cstdint>
#include <optional>
#include <rgf/integrations/fc3/internal/structs.hpp>

namespace rgf::integrations::fc3 {

[[nodiscard]] std::optional<int> TraceSegment(
    const math::Vec3& from, const math::Vec3& target) noexcept;

struct TraceHit {
  math::Vec3 point{};
};

struct TraceAnswer {
  std::uint32_t hits{};
  TraceHit first{};
};

[[nodiscard]] std::optional<TraceAnswer> TraceLook(int query) noexcept;

void ObserveReach(std::uintptr_t entity, const math::Vec3& eye,
                  const math::Vec3& point, std::uint64_t frame) noexcept;

[[nodiscard]] float ReachOf(std::uintptr_t entity) noexcept;

}  // namespace rgf::integrations::fc3
