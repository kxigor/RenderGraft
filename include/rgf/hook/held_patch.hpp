#pragma once

#include <expected>
#include <optional>
#include <rgf/hook/patch.hpp>
#include <system_error>

namespace rgf::hook {

class HeldPatch {
 public:
  using Source = std::expected<CodePatch, std::error_code> (*)() noexcept;

  explicit HeldPatch(Source source) noexcept : source_{source} {}

  HeldPatch(const HeldPatch&) = delete;
  HeldPatch& operator=(const HeldPatch&) = delete;

  HeldPatch(HeldPatch&&) = delete;
  HeldPatch& operator=(HeldPatch&&) = delete;

  ~HeldPatch() = default;

  void Follow(bool wanted) noexcept;

 private:
  Source source_;
  std::optional<CodePatch> patch_;
};

}  // namespace rgf::hook
