#include <exception>
#include <new>
#include <rgf/integrations/telegram/history_image_hook.hpp>
#include <rgf/qt/raster_image_hook.hpp>
#include <rgf/qt/widget_paint_engine.hpp>
#include <tuple>
#include <utility>

#include "internal/build.hpp"

namespace rgf::integrations::telegram {

namespace {

[[nodiscard]] std::atomic<HistoryImageHook*>& CallbackTargetStorage() noexcept {
  static std::atomic<HistoryImageHook*> target{nullptr};
  return target;
}

}  // namespace

std::expected<std::unique_ptr<HistoryImageHook>, std::error_code>
HistoryImageHook::Create(hook::Vtable historyTable, void** rasterTable,
                         DWORD guiThreadId, ImageCallback onImage) {
  if (historyTable.Get() == nullptr || rasterTable == nullptr ||
      guiThreadId == 0 || !onImage) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  if (CallbackTargetStorage().load(std::memory_order_acquire) != nullptr) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  auto paintHook{PreparePaintHook(historyTable)};
  if (!paintHook) {
    return std::unexpected{paintHook.error()};
  }
  std::unique_ptr<HistoryImageHook> imageHook{
      new (std::nothrow) HistoryImageHook{std::move(*paintHook), rasterTable,
                                          guiThreadId, std::move(onImage)}};
  if (!imageHook) {
    return std::unexpected{std::make_error_code(std::errc::not_enough_memory)};
  }
  return imageHook;
}

HistoryImageHook::HistoryImageHook(hook::VmtHook paintHook, void** rasterTable,
                                   DWORD guiThreadId,
                                   ImageCallback onImage) noexcept
    : paintHook_{std::move(paintHook)},
      kOriginalPaint_{paintHook_.Original<internal::HistoryPaintFn>()},
      kRasterTable_{rasterTable},
      kGuiThreadId_{guiThreadId},
      kOnImage_{std::move(onImage)} {}

HistoryImageHook::~HistoryImageHook() {
  if (!CanRelease()) {
    std::terminate();
  }
  auto* previousTarget{this};
  std::ignore = CallbackTargetStorage().compare_exchange_strong(
      previousTarget, nullptr, std::memory_order_acq_rel,
      std::memory_order_acquire);
}

std::expected<void, std::error_code> HistoryImageHook::Enable() noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  auto* previousTarget{static_cast<HistoryImageHook*>(nullptr)};
  if (!CallbackTargetStorage().compare_exchange_strong(
          previousTarget, this, std::memory_order_acq_rel,
          std::memory_order_acquire) &&
      previousTarget != this) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  const auto kEnabled{paintHook_.Enable()};
  if (!kEnabled) {
    return std::unexpected{kEnabled.error()};
  }
  isAttached_.store(true, std::memory_order_release);
  return {};
}

std::expected<void, std::error_code> HistoryImageHook::Disable() noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  if (IsDrawing()) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  if (IsDetached()) {
    return {};
  }
  const auto kDisabled{paintHook_.Disable()};
  if (!kDisabled) {
    return std::unexpected{kDisabled.error()};
  }
  isAttached_.store(false, std::memory_order_release);
  return {};
}

void HistoryImageHook::PaintDetour(qt::internal::Widget* widget,
                                   qt::internal::PaintEvent* event) noexcept {
  RequireCallbackTarget().PaintHistory(widget, event);
}

void HistoryImageHook::PaintHistory(qt::internal::Widget* widget,
                                    qt::internal::PaintEvent* event) noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    foreignThreadObserved_.store(true, std::memory_order_relaxed);
    kOriginalPaint_(widget, event);
    return;
  }
  ++drawDepth_;
  ++paintCalls_;
  const auto kEngine{qt::WidgetPaintEngine(*widget)};
  {
    const qt::RasterImageHook kImages{kEngine ? *kEngine : nullptr,
                                      kRasterTable_, &OnImage};
    if (!kImages.IsActive()) {
      ++skippedPaints_;
      paintError_ = kEngine ? std::make_error_code(std::errc::not_supported)
                            : kEngine.error();
    }
    kOriginalPaint_(widget, event);
  }
  --drawDepth_;
}

bool HistoryImageHook::OnImage(const qt::RasterCanvas& canvas,
                               const qt::internal::RectF& bounds) noexcept {
  auto& target{RequireCallbackTarget()};
  ++target.imageCalls_;
  target.kOnImage_(canvas, bounds);
  return true;
}

bool HistoryImageHook::IsDrawing() const noexcept { return drawDepth_ != 0; }

bool HistoryImageHook::IsDetached() const noexcept {
  return !isAttached_.load(std::memory_order_acquire);
}

bool HistoryImageHook::CanRelease() const noexcept {
  return IsDetached() && !IsDrawing() &&
         !foreignThreadObserved_.load(std::memory_order_relaxed);
}

std::uint64_t HistoryImageHook::PaintCalls() const noexcept {
  return paintCalls_;
}

std::uint64_t HistoryImageHook::ImageCalls() const noexcept {
  return imageCalls_;
}

std::uint64_t HistoryImageHook::SkippedPaints() const noexcept {
  return skippedPaints_;
}

std::error_code HistoryImageHook::PaintError() const noexcept {
  return paintError_;
}

std::expected<hook::VmtHook, std::error_code>
HistoryImageHook::PreparePaintHook(hook::Vtable historyTable) noexcept {
  auto paintHook{
      hook::VmtHook::Create(historyTable, internal::kHistoryPaintSlot)};
  if (!paintHook) {
    return std::unexpected{paintHook.error()};
  }
  const auto kDetourSet{paintHook->SetDetour(&PaintDetour)};
  if (!kDetourSet) {
    return std::unexpected{kDetourSet.error()};
  }
  return paintHook;
}

HistoryImageHook& HistoryImageHook::RequireCallbackTarget() noexcept {
  auto* target{CallbackTargetStorage().load(std::memory_order_acquire)};
  if (target == nullptr) {
    std::terminate();
  }
  return *target;
}

}  // namespace rgf::integrations::telegram
