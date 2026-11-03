#pragma once

#include <bit>
#include <expected>
#include <memory>
#include <rgf/hook/hook.hpp>
#include <system_error>
#include <type_traits>

namespace rgf::hook {

class InlineHook {
 public:
  InlineHook() noexcept;

  template <SlotFunction Fn>
  [[nodiscard]] static std::expected<InlineHook, std::error_code> Create(
      Fn target, std::type_identity_t<Fn> detour) noexcept {
    return CreateRaw(std::bit_cast<void*>(target),
                     std::bit_cast<void*>(detour));
  }

  InlineHook(const InlineHook&) = delete;
  InlineHook& operator=(const InlineHook&) = delete;

  InlineHook(InlineHook&& other) noexcept;
  InlineHook& operator=(InlineHook&&) = delete;

  ~InlineHook();

  template <SlotFunction Fn>
  [[nodiscard]] Fn Original() const noexcept {
    return std::bit_cast<Fn>(OriginalRaw());
  }

  [[nodiscard]] std::expected<void, std::error_code> Enable() noexcept;
  [[nodiscard]] std::expected<void, std::error_code> Disable() noexcept;

 private:
  class Binding;

  explicit InlineHook(std::unique_ptr<Binding> binding) noexcept;

  [[nodiscard]] static std::expected<InlineHook, std::error_code> CreateRaw(
      void* target, void* detour) noexcept;
  [[nodiscard]] void* OriginalRaw() const noexcept;

  std::unique_ptr<Binding> binding_;
};

}  // namespace rgf::hook
