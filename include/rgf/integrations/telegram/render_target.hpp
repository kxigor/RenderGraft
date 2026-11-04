#pragma once

#include <cstdint>
#include <expected>
#include <rgf/hook/vtable.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/widget_updater.hpp>
#include <system_error>

namespace rgf::integrations::telegram {

class RenderTarget {
 public:
  [[nodiscard]] static std::expected<RenderTarget, std::error_code> Locate();

  RenderTarget(const RenderTarget&) = default;
  RenderTarget& operator=(const RenderTarget&) = delete;

  RenderTarget(RenderTarget&&) = default;
  RenderTarget& operator=(RenderTarget&&) = delete;

  ~RenderTarget() = default;

  [[nodiscard]] hook::Vtable MessageTable() const noexcept;
  [[nodiscard]] hook::Vtable HistoryTable() const noexcept;
  [[nodiscard]] void** RasterTable() const noexcept;
  [[nodiscard]] qt::internal::GetPaintEngineFn PaintEngine() const noexcept;
  [[nodiscard]] qt::WidgetUpdater WidgetUpdates() const noexcept;

 private:
  explicit RenderTarget(std::uintptr_t base) noexcept;

  std::uintptr_t base_;
};

}  // namespace rgf::integrations::telegram
