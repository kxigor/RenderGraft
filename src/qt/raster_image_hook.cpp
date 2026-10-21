#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <rgf/core/cast.hpp>
#include <rgf/qt/raster_image_hook.hpp>
#include <span>

namespace rgf::qt {

namespace {

[[nodiscard]] std::array<void*, internal::kRasterVirtualCount + 1> CloneTable(
    void** table) noexcept {
  if (table == nullptr) {
    return {};
  }
  std::array<void*, internal::kRasterVirtualCount + 1> result{};
  std::ranges::copy(std::span{table - 1, result.size()}, result.begin());
  return result;
}

[[nodiscard]] internal::RectF ImageBounds(
    const internal::PointF& position, const internal::Image& image) noexcept {
  if (image.data == nullptr) {
    return {};
  }
  const double kRatio{image.data->devicePixelRatio};
  if (!std::isfinite(kRatio) || kRatio <= 0.0) {
    return {};
  }
  return {.x = position.x,
          .y = position.y,
          .width = static_cast<double>(image.data->width) / kRatio,
          .height = static_cast<double>(image.data->height) / kRatio};
}

[[nodiscard]] internal::RectF PixmapBounds(
    const internal::PointF& position, const internal::Pixmap& pixmap) noexcept {
  if (pixmap.data == nullptr) {
    return {};
  }
  const double kRatio{core::PointerCast<internal::PixmapDevicePixelRatioFn>(
      pixmap.data->vtable[internal::kPixmapDevicePixelRatioSlot])(pixmap.data)};
  if (!std::isfinite(kRatio) || kRatio <= 0.0) {
    return {};
  }
  return {.x = position.x,
          .y = position.y,
          .width = static_cast<double>(pixmap.data->width) / kRatio,
          .height = static_cast<double>(pixmap.data->height) / kRatio};
}

}  // namespace

RasterImageHook::RasterImageHook(internal::PaintEngine* engine,
                                 void** rasterVtable,
                                 ImageCallback onImage) noexcept
    : kEngine_{FindEngine(engine, rasterVtable, onImage)},
      kOriginalTable_{kEngine_ != nullptr ? kEngine_->vtable : nullptr},
      kFillColor_{kOriginalTable_ != nullptr
                      ? core::PointerCast<internal::FillColorFn>(
                            kOriginalTable_[internal::kFillColorSlot])
                      : nullptr},
      kOnImage_{onImage},
      kPrevious_{Current()},
      table_{CloneTable(kOriginalTable_)} {
  if (!IsActive()) {
    return;
  }
  table_[internal::kDrawPixmapRectSlot + 1] =
      core::PointerCast<void*>(&DrawPixmapRect);
  table_[internal::kDrawTiledPixmapSlot + 1] =
      core::PointerCast<void*>(&DrawTiledPixmap);
  table_[internal::kDrawImageRectSlot + 1] =
      core::PointerCast<void*>(&DrawImageRect);
  table_[internal::kDrawPixmapPointSlot + 1] =
      core::PointerCast<void*>(&DrawPixmapPoint);
  table_[internal::kDrawImagePointSlot + 1] =
      core::PointerCast<void*>(&DrawImagePoint);
  Current() = this;
  kEngine_->vtable = table_.data() + 1;
}

RasterImageHook::~RasterImageHook() {
  if (!IsActive()) {
    return;
  }
  kEngine_->vtable = kOriginalTable_;
  Current() = kPrevious_;
}

bool RasterImageHook::IsActive() const noexcept { return kEngine_ != nullptr; }

internal::PaintEngine* RasterImageHook::FindEngine(
    internal::PaintEngine* engine, void** rasterVtable,
    ImageCallback onImage) noexcept {
  if (engine == nullptr || rasterVtable == nullptr || onImage == nullptr) {
    return nullptr;
  }
  if (engine->vtable == rasterVtable) {
    return engine;
  }
  for (const auto* hook{Current()}; hook != nullptr; hook = hook->kPrevious_) {
    if (hook->kEngine_ == engine && engine->vtable == hook->table_.data() + 1) {
      return engine;
    }
  }
  return nullptr;
}

const RasterImageHook*& RasterImageHook::Current() noexcept {
  thread_local const RasterImageHook* hook{nullptr};
  return hook;
}

const RasterImageHook& RasterImageHook::Owner(
    const internal::PaintEngine& engine) noexcept {
  for (const auto* hook{Current()}; hook != nullptr; hook = hook->kPrevious_) {
    if (hook->kEngine_ == &engine) {
      return *hook;
    }
  }
  std::terminate();
}

void RasterImageHook::DrawPixmapRect(
    internal::PaintEngine* engine, const internal::RectF& rectangle,
    [[maybe_unused]] const internal::Pixmap& pixmap,
    [[maybe_unused]] const internal::RectF& source) noexcept {
  Owner(*engine).Draw(rectangle);
}

void RasterImageHook::DrawTiledPixmap(
    internal::PaintEngine* engine, const internal::RectF& rectangle,
    [[maybe_unused]] const internal::Pixmap& pixmap,
    [[maybe_unused]] const internal::PointF& source) noexcept {
  Owner(*engine).Draw(rectangle);
}

void RasterImageHook::DrawImageRect(
    internal::PaintEngine* engine, const internal::RectF& rectangle,
    [[maybe_unused]] const internal::Image& image,
    [[maybe_unused]] const internal::RectF& source,
    [[maybe_unused]] std::int32_t flags) noexcept {
  Owner(*engine).Draw(rectangle);
}

void RasterImageHook::DrawPixmapPoint(internal::PaintEngine* engine,
                                      const internal::PointF& position,
                                      const internal::Pixmap& pixmap) noexcept {
  Owner(*engine).Draw(PixmapBounds(position, pixmap));
}

void RasterImageHook::DrawImagePoint(internal::PaintEngine* engine,
                                     const internal::PointF& position,
                                     const internal::Image& image) noexcept {
  Owner(*engine).Draw(ImageBounds(position, image));
}

void RasterImageHook::Draw(const internal::RectF& bounds) const noexcept {
  if (bounds.width <= 0.0 || bounds.height <= 0.0) {
    return;
  }
  const RasterCanvas kCanvas{*kEngine_, kFillColor_};
  kOnImage_(kCanvas, bounds);
}

}  // namespace rgf::qt
