#include "peer.hpp"

#include <limits>
#include <rgf/process/memory.hpp>

namespace rgf::integrations::telegram::internal {

std::expected<std::uint64_t, std::error_code> ReadPeerId(
    std::uint64_t peer) noexcept {
  if (peer == 0 ||
      peer > std::numeric_limits<std::uintptr_t>::max() - kPeerIdOffset) {
    return std::unexpected{std::make_error_code(std::errc::bad_address)};
  }
  return process::ReadAt<std::uint64_t>(static_cast<std::uintptr_t>(peer) +
                                        kPeerIdOffset);
}

}  // namespace rgf::integrations::telegram::internal
