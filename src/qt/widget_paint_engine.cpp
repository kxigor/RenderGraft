#include <cstddef>
#include <rgf/core/cast.hpp>
#include <rgf/qt/widget_paint_engine.hpp>

namespace rgf::qt {

std::expected<internal::PaintEngine*, std::error_code> WidgetPaintEngine(
    const internal::Widget& widget) noexcept {
  const auto* kWidgetBytes{core::PointerCast<const std::byte*>(&widget)};
  const auto* kDevice{core::PointerCast<const internal::PaintDevice*>(
      kWidgetBytes + internal::kWidgetPaintDeviceOffset)};
  const auto kRedirected{core::PointerCast<internal::RedirectedDeviceFn>(
      kDevice->vtable[internal::kPaintDeviceRedirectedSlot])};
  const auto* kTarget{kRedirected(kDevice, nullptr)};
  if (kTarget == nullptr) {
    return std::unexpected{std::make_error_code(std::errc::not_supported)};
  }
  const auto kPaintEngine{core::PointerCast<internal::GetDevicePaintEngineFn>(
      kTarget->vtable[internal::kPaintDevicePaintEngineSlot])};
  auto* engine{kPaintEngine(kTarget)};
  if (engine == nullptr) {
    return std::unexpected{std::make_error_code(std::errc::not_supported)};
  }
  return engine;
}

}  // namespace rgf::qt
