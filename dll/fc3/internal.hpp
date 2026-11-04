#pragma once

#include <cstdint>
#include <optional>
#include <rgf/aimbot/aimbot.hpp>
#include <rgf/integrations/fc3/fc3.hpp>
#include <rgf/math/vec3.hpp>

namespace rgf::dll {

[[nodiscard]] std::optional<float> PriorityOf(
    const integrations::fc3::EntityInfo& entity,
    const integrations::fc3::PawnInfo* self) noexcept;

[[nodiscard]] std::optional<aimbot::Target> Pick(
    const integrations::fc3::EntitySnapshot& snapshot,
    const integrations::fc3::CameraBasis& camera) noexcept;

[[nodiscard]] std::uintptr_t CameraState(
    const integrations::fc3::EntitySnapshot& snapshot,
    const math::Vec3& forward) noexcept;

void UpdateReach() noexcept;

void RunAssist() noexcept;

void SteadyAim() noexcept;

void ProbeTrace();

void LogStatus(std::uint64_t frames);

}  // namespace rgf::dll
