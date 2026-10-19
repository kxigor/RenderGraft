#include "widget_refresh.hpp"

#include <cstdint>
#include <rgf/core/cast.hpp>
#include <rgf/win/error.hpp>
#include <rgf/win/module_view.hpp>

#include "internal/widget.hpp"

namespace rgf::telegram {

namespace {

[[nodiscard]] std::expected<internal::Widget*, std::error_code> FindWidget(
    std::uintptr_t base, HWND window) noexcept {
  const auto* kMapper{*core::PointerCast<internal::WidgetMapper**>(
      base + internal::kWidgetMapperRva)};
  if (kMapper == nullptr || kMapper->data == nullptr ||
      kMapper->data->numBuckets <= 0 || kMapper->data->size <= 0) {
    return std::unexpected{
        std::error_code{ERROR_NOT_FOUND, std::system_category()}};
  }
  const auto kWindowId{core::PointerCast<std::uintptr_t>(window)};
  const std::uint32_t kHash{
      static_cast<std::uint32_t>((kWindowId >> internal::kWidgetHashShift) ^
                                 kWindowId) ^
      kMapper->data->seed};
  const auto kFindNode{core::PointerCast<internal::FindWidgetNode>(
      base + internal::kFindWidgetNodeRva)};
  const auto* kNode{*kFindNode(kMapper, kWindowId, kHash)};
  if (kNode == nullptr ||
      kNode == core::PointerCast<internal::WidgetHashNode*>(kMapper->data) ||
      kNode->windowId != kWindowId || kNode->widget == nullptr) {
    return std::unexpected{
        std::error_code{ERROR_NOT_FOUND, std::system_category()}};
  }
  return kNode->widget;
}

}  // namespace

std::expected<void, std::error_code> RefreshWindow(HWND window) noexcept {
  const DWORD kGuiThread{GetWindowThreadProcessId(
      /*hWnd = */ window, /*lpdwProcessId = */ nullptr)};
  if (kGuiThread == 0) {
    return std::unexpected{win::LastError()};
  }
  if (kGuiThread != GetCurrentThreadId()) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  const auto kModule{win::ModuleView::Executable()};
  if (!kModule) {
    return std::unexpected{kModule.error()};
  }
  const auto kBase{core::PointerCast<std::uintptr_t>(kModule->Get())};
  const auto kWidget{FindWidget(kBase, window)};
  if (!kWidget) {
    return std::unexpected{kWidget.error()};
  }
  const auto kUpdate{core::PointerCast<internal::UpdateWidget>(
      kBase + internal::kUpdateWidgetRva)};
  kUpdate(*kWidget);
  return {};
}

}  // namespace rgf::telegram
