#pragma once

#include <cstdint>
#include <rgf/games/fc3/internal/kind.hpp>

namespace rgf::esp {

// What the overlay does with a kind the engine already named: which colour it
// gets, and whether it is drawn at all. Both are presentation, which is why
// they are here and the naming is not.
[[nodiscard]] std::uint32_t ColourOf(const games::fc3::EntityInfo& entity,
                                     games::fc3::Kind kind) noexcept;

[[nodiscard]] bool Shown(games::fc3::Kind kind) noexcept;

}  // namespace rgf::esp
