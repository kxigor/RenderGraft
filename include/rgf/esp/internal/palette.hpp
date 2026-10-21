#pragma once

#include <cstdint>
#include <rgf/integrations/fc3/internal/kind.hpp>

namespace rgf::esp {

// What the overlay does with a kind the engine already named: which colour it
// gets, and whether it is drawn at all. Both are presentation, which is why
// they are here and the naming is not.
[[nodiscard]] std::uint32_t ColourOf(
    const integrations::fc3::EntityInfo& entity,
    integrations::fc3::Kind kind) noexcept;

[[nodiscard]] bool Shown(integrations::fc3::Kind kind) noexcept;

}  // namespace rgf::esp
