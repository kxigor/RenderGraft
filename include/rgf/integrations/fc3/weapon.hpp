#pragma once

#include <expected>
#include <rgf/hook/patch.hpp>
#include <system_error>

namespace rgf::integrations::fc3 {

[[nodiscard]] std::expected<hook::CodePatch, std::error_code>
SuppressRecoil() noexcept;

[[nodiscard]] std::expected<hook::CodePatch, std::error_code>
SuppressSpread() noexcept;

[[nodiscard]] std::expected<hook::CodePatch, std::error_code>
SuppressSway() noexcept;

[[nodiscard]] std::expected<hook::CodePatch, std::error_code>
IgnoreSpentRounds() noexcept;

[[nodiscard]] std::expected<hook::CodePatch, std::error_code>
IgnoreEmptyMagazine() noexcept;

}  // namespace rgf::integrations::fc3
