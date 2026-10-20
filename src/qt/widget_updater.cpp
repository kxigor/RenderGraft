#include <windows.h>

#include <rgf/core/cast.hpp>
#include <rgf/qt/widget_updater.hpp>

namespace rgf::qt {

namespace {

[[nodiscard]] std::expected<internal::Widget*, std::error_code> FindWidget(
    const internal::WidgetMapper* mapper, std::uintptr_t windowId,
    internal::FindWidgetNode findNode) noexcept {
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
                             internal::FindWidgetNode findNode,
                             internal::UpdateWidget updateWidget) noexcept
    : kMapper_{mapper}, kFindNode_{findNode}, kUpdateWidget_{updateWidget} {}

std::expected<void, std::error_code> WidgetUpdater::Update(
    std::uintptr_t windowId) const noexcept {
  const auto kWidget{FindWidget(*kMapper_, windowId, kFindNode_)};
  if (!kWidget) {
    return std::unexpected{kWidget.error()};
  }
  kUpdateWidget_(*kWidget);
  return {};
}

}  // namespace rgf::qt
