#pragma once

#include <cstddef>
#include <expected>
#include <rgf/hook/hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <system_error>

namespace rgf::hook {

class VmtHook final : public Hook {
 public:
  VmtHook() noexcept = default;

  [[nodiscard]] static std::expected<VmtHook, std::error_code> Create(
      Vtable table, std::size_t index) noexcept;

  VmtHook(const VmtHook&) = delete;
  VmtHook& operator=(const VmtHook&) = delete;

  VmtHook(VmtHook&& other) noexcept;
  VmtHook& operator=(VmtHook&&) = delete;

  ~VmtHook() override;

  [[nodiscard]] std::expected<void, std::error_code> Enable() noexcept override;
  [[nodiscard]] std::expected<void, std::error_code> Disable() noexcept
      override;

 private:
  VmtHook(void** slot, void* original) noexcept;

  [[nodiscard]] std::expected<void, std::error_code> SetDetourRaw(
      void* detour) noexcept override;

  [[nodiscard]] void* OriginalRaw() const noexcept override;

  [[nodiscard]] std::expected<void, std::error_code> SetEnabled(
      bool isEnabled) noexcept;

  bool isEnabled_{false};
  void* detour_{nullptr};

  void** slot_{nullptr};
  void* original_{nullptr};
};

}  // namespace rgf::hook
