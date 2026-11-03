#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <system_error>

namespace rgf::integrations::telegram::internal {

// PeerData's native typed PeerId in Telegram Desktop 7.3.3, MSVC x64.
inline constexpr std::size_t kPeerIdOffset{0x8};

[[nodiscard]] std::expected<std::uint64_t, std::error_code> ReadPeerId(
    std::uint64_t peer) noexcept;

}  // namespace rgf::integrations::telegram::internal
