#include <windows.h>

#include <cstdint>
#include <rgf/core/cast.hpp>
#include <rgf/qt/widget_updater.hpp>
#include <rgf/win/window.hpp>

namespace rgf::qt {

namespace {

[[nodiscard]] std::expected<internal::Widget*, std::error_code> FindWidget(
    const internal::WidgetMapper* mapper, std::uintptr_t windowId,
    internal::FindWidgetNodeFn findNode) noexcept {
  if (mapper == nullptr || mapper->data == nullptr ||
      mapper->data->numBuckets <= 0 || mapper->data->size <= 0) {
    return std::unexpected{
        std::error_code{ERROR_NOT_FOUND, std::system_category()}};
  }
  const std::uint32_t kHash{
      static_cast<std::uint32_t>((windowId >> internal::kWidgetHashShift) ^
                                 windowId) ^
      mapper->data->seed};
  const auto* kNode{*findNode(mapper, windowId, kHash)};
  if (kNode == nullptr ||
      kNode == core::PointerCast<internal::WidgetHashNode*>(mapper->data) ||
      kNode->windowId != windowId || kNode->widget == nullptr) {
    return std::unexpected{
        std::error_code{ERROR_NOT_FOUND, std::system_category()}};
  }
  return kNode->widget;
}

}  // namespace

WidgetUpdater::WidgetUpdater(internal::WidgetMapper* const* mapper,
                             internal::FindWidgetNodeFn findNode,
                             internal::UpdateWidgetFn updateWidget) noexcept
    : kMapper_{mapper}, kFindNode_{findNode}, kUpdateWidget_{updateWidget} {}

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
  const auto kWidget{FindWidget(*kMapper_, kWindowId, kFindNode_)};
  if (!kWidget) {
    return std::unexpected{kWidget.error()};
  }
  kUpdateWidget_(*kWidget);
  return {};
}

}  // namespace rgf::qt
