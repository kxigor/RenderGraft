#pragma once

#include <cstdint>
#include <expected>
#include <rgf/qt/internal/abi.hpp>
#include <system_error>

namespace rgf::qt {

class WidgetUpdater {
 public:
  WidgetUpdater(internal::WidgetMapper* const* mapper,
                internal::FindWidgetNode findNode,
                internal::UpdateWidget updateWidget) noexcept;

  WidgetUpdater(const WidgetUpdater&) = delete;
  WidgetUpdater& operator=(const WidgetUpdater&) = delete;

  WidgetUpdater(WidgetUpdater&&) = delete;
  WidgetUpdater& operator=(WidgetUpdater&&) = delete;

  ~WidgetUpdater() = default;

  [[nodiscard]] std::expected<void, std::error_code> Update(
      std::uintptr_t windowId) const noexcept;

 private:
  internal::WidgetMapper* const* const kMapper_;
  const internal::FindWidgetNode kFindNode_;
  const internal::UpdateWidget kUpdateWidget_;
};

}  // namespace rgf::qt
