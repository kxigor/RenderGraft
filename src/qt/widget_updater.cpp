#include <windows.h>

#include <bit>
#include <cstdint>
#include <rgf/core/cast.hpp>
#include <rgf/qt/widget_updater.hpp>
#include <rgf/win/window.hpp>
#include <span>

namespace rgf::qt {

namespace {

[[nodiscard]] std::expected<internal::Widget*, std::error_code> FindWidget(
    const internal::WidgetMapper* mapper, std::uintptr_t windowId) noexcept {
  if (mapper == nullptr || mapper->data == nullptr || mapper->data->size == 0) {
    return std::unexpected{
        std::error_code{ERROR_NOT_FOUND, std::system_category()}};
  }
  const auto& data{*mapper->data};
  if (data.spans == nullptr ||
      data.numBuckets < internal::kWidgetHashSpanBucketCount ||
      !std::has_single_bit(data.numBuckets) || data.size > data.numBuckets) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_DATA, std::system_category()}};
  }
  for (const auto& span :
       std::span{data.spans,
                 data.numBuckets / internal::kWidgetHashSpanBucketCount}) {
    for (const std::uint8_t kEntry : span.offsets) {
      if (kEntry == internal::kWidgetHashUnusedEntry) {
        continue;
      }
      if (span.entries == nullptr ||
          span.allocated > internal::kWidgetHashSpanBucketCount ||
          kEntry >= span.allocated) {
        return std::unexpected{
            std::error_code{ERROR_INVALID_DATA, std::system_category()}};
      }
      const auto& node{span.entries[kEntry]};
      if (node.windowId == windowId && node.widget != nullptr) {
        return node.widget;
      }
    }
  }
  return std::unexpected{
      std::error_code{ERROR_NOT_FOUND, std::system_category()}};
}

}  // namespace

WidgetUpdater::WidgetUpdater(internal::WidgetMapper* const* mapper,
                             internal::UpdateWidgetFn updateWidget) noexcept
    : kMapper_{mapper}, kUpdateWidget_{updateWidget} {}

std::expected<void, std::error_code> WidgetUpdater::Update(
    HWND window) const noexcept {
  const auto kGuiThread{win::WindowThreadId(window)};
  if (!kGuiThread) {
    return std::unexpected{kGuiThread.error()};
  }
  if (*kGuiThread != GetCurrentThreadId()) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  const std::uintptr_t kWindowId{core::PointerCast<std::uintptr_t>(window)};
  const auto kWidget{FindWidget(*kMapper_, kWindowId)};
  if (!kWidget) {
    return std::unexpected{kWidget.error()};
  }
  kUpdateWidget_(*kWidget);
  return {};
}

}  // namespace rgf::qt
