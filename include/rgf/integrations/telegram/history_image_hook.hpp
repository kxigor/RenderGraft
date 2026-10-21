#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/integrations/telegram/internal/history.hpp>
#include <rgf/qt/raster_canvas.hpp>
#include <system_error>

namespace rgf::integrations::telegram {

class HistoryImageHook {
 public:
  using ImageCallback = std::move_only_function<void(
      const qt::RasterCanvas&, const qt::internal::RectF&) const noexcept>;

  [[nodiscard]] static std::expected<std::unique_ptr<HistoryImageHook>,
                                     std::error_code>
  Create(hook::Vtable historyTable, void** rasterTable, DWORD guiThreadId,
         ImageCallback onImage);

  HistoryImageHook(const HistoryImageHook&) = delete;
  HistoryImageHook& operator=(const HistoryImageHook&) = delete;
  HistoryImageHook(HistoryImageHook&&) = delete;
  HistoryImageHook& operator=(HistoryImageHook&&) = delete;

  ~HistoryImageHook();

  [[nodiscard]] std::expected<void, std::error_code> Enable() noexcept;
  [[nodiscard]] std::expected<void, std::error_code> Disable() noexcept;
  [[nodiscard]] bool IsDrawing() const noexcept;
  [[nodiscard]] bool IsDetached() const noexcept;
  [[nodiscard]] bool CanRelease() const noexcept;
  [[nodiscard]] std::uint64_t PaintCalls() const noexcept;
  [[nodiscard]] std::uint64_t ImageCalls() const noexcept;
  [[nodiscard]] std::uint64_t SkippedPaints() const noexcept;
  [[nodiscard]] std::error_code PaintError() const noexcept;

 private:
  HistoryImageHook(hook::VmtHook paintHook, void** rasterTable,
                   DWORD guiThreadId, ImageCallback onImage) noexcept;

  [[nodiscard]] static std::expected<hook::VmtHook, std::error_code>
  PreparePaintHook(hook::Vtable historyTable) noexcept;
  [[nodiscard]] static HistoryImageHook& RequireCallbackTarget() noexcept;
  static void PaintDetour(qt::internal::Widget* widget,
                          qt::internal::PaintEvent* event) noexcept;
  void PaintHistory(qt::internal::Widget* widget,
                    qt::internal::PaintEvent* event) noexcept;
  [[nodiscard]] static bool OnImage(const qt::RasterCanvas& canvas,
                                    const qt::internal::RectF& bounds) noexcept;

  hook::VmtHook paintHook_;
  const internal::HistoryPaintFn kOriginalPaint_;
  void** const kRasterTable_;
  const DWORD kGuiThreadId_;
  const ImageCallback kOnImage_;
  std::atomic<bool> isAttached_{false};
  std::atomic<bool> foreignThreadObserved_{false};
  unsigned int drawDepth_{};
  std::uint64_t paintCalls_{};
  std::uint64_t imageCalls_{};
  std::uint64_t skippedPaints_{};
  std::error_code paintError_;
};

}  // namespace rgf::integrations::telegram
