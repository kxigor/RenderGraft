#pragma once

#include <windows.h>

#include <expected>
#include <rgf/qt/internal/abi.hpp>
#include <system_error>

namespace rgf::qt {

class WidgetUpdater {
 public:
  WidgetUpdater(internal::WidgetMapper* const* mapper,
                internal::UpdateWidgetFn updateWidget) noexcept;

  WidgetUpdater(const WidgetUpdater&) = delete;
  WidgetUpdater& operator=(const WidgetUpdater&) = delete;

  WidgetUpdater(WidgetUpdater&&) = delete;
  WidgetUpdater& operator=(WidgetUpdater&&) = delete;

  ~WidgetUpdater() = default;

  [[nodiscard]] std::expected<void, std::error_code> Update(
      HWND window) const noexcept;

 private:
  internal::WidgetMapper* const* const kMapper_;
  const internal::UpdateWidgetFn kUpdateWidget_;
};

}  // namespace rgf::qt
