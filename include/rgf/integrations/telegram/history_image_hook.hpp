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
#include <span>
#include <system_error>
#include <vector>

namespace rgf::integrations::telegram {

class HistoryImageHook {
 public:
  using ImageCallback = std::move_only_function<bool(
      const qt::RasterCanvas&, const qt::internal::RectF&) const noexcept>;

  [[nodiscard]] static std::expected<std::unique_ptr<HistoryImageHook>,
                                     std::error_code>
  Create(std::span<const hook::Vtable> historyTables, void** rasterTable,
         DWORD guiThreadId, ImageCallback onImage);

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
  class Binding {
   public:
    Binding(hook::Vtable table, hook::VmtHook paintHook) noexcept;
    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;
    Binding(Binding&& other) noexcept;
    Binding& operator=(Binding&&) = delete;
    ~Binding() = default;

    [[nodiscard]] std::expected<void, std::error_code> Enable() noexcept;
    [[nodiscard]] std::expected<void, std::error_code> Disable() noexcept;
    [[nodiscard]] bool Matches(void** table) const noexcept;
    [[nodiscard]] internal::HistoryPaintFn Original() const noexcept;

   private:
    hook::VmtHook paintHook_;
    const hook::Vtable kTable_;
    const internal::HistoryPaintFn kOriginal_;
    bool isEnabled_{};
  };

  HistoryImageHook(std::vector<Binding> paintHooks, void** rasterTable,
                   DWORD guiThreadId, ImageCallback onImage) noexcept;

  [[nodiscard]] static std::expected<hook::VmtHook, std::error_code>
  PreparePaintHook(hook::Vtable historyTable) noexcept;
  [[nodiscard]] static std::expected<std::vector<Binding>, std::error_code>
  PreparePaintHooks(std::span<const hook::Vtable> historyTables);
  [[nodiscard]] internal::HistoryPaintFn OriginalFor(
      const qt::internal::Widget& widget) const noexcept;
  [[nodiscard]] static HistoryImageHook& RequireCallbackTarget() noexcept;
  static void PaintDetour(qt::internal::Widget* widget,
                          qt::internal::PaintEvent* event) noexcept;
  void PaintHistory(qt::internal::Widget* widget,
                    qt::internal::PaintEvent* event) noexcept;
  [[nodiscard]] static bool OnImage(const qt::RasterCanvas& canvas,
                                    const qt::internal::RectF& bounds) noexcept;

  std::vector<Binding> paintHooks_;
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
