#include <expected>
#include <rgf/hook/held_patch.hpp>
#include <system_error>
#include <utility>

namespace rgf::hook {

void HeldPatch::Follow(bool wanted) noexcept {
  if (wanted == patch_.has_value()) {
    return;
  }
  if (!wanted) {
    patch_.reset();
    return;
  }

  std::expected<CodePatch, std::error_code> applied{source_()};
  if (applied) {
    patch_.emplace(std::move(*applied));
  }
}

}  // namespace rgf::hook
