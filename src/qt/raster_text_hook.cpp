#include <algorithm>
#include <array>
#include <exception>
#include <rgf/core/cast.hpp>
#include <rgf/qt/raster_image_hook.hpp>
#include <rgf/qt/raster_text_hook.hpp>
#include <span>

namespace rgf::qt {

namespace {

[[nodiscard]] internal::PaintEngine* FindEngine(
    internal::Painter& painter, internal::GetPaintEngineFn getPaintEngine,
    void** rasterVtable) noexcept {
  auto* engine{getPaintEngine(&painter)};
  return engine != nullptr && (engine->vtable == rasterVtable ||
                               RasterImageHook::IsInstalledOn(*engine))
             ? engine
             : nullptr;
}

[[nodiscard]] std::array<void*, internal::kRasterVirtualCount + 1> CloneTable(
    void** table) noexcept {
  if (table == nullptr) {
    return {};
  }
  std::array<void*, internal::kRasterVirtualCount + 1> result{};
  std::ranges::copy(std::span{table - 1, internal::kRasterVirtualCount + 1},
                    result.begin());
  return result;
}

[[nodiscard]] internal::RectF TextBounds(
    const internal::PointF& position, const internal::TextItem& item) noexcept {
  const double kAscent{static_cast<double>(item.ascent) /
                       internal::kFixedScale};
  const double kDescent{static_cast<double>(item.descent) /
                        internal::kFixedScale};
  return {.x = position.x,
          .y = position.y - kAscent,
          .width = static_cast<double>(item.width) / internal::kFixedScale,
          .height = kAscent + kDescent};
}

}  // namespace

RasterTextHook::RasterTextHook(internal::Painter& painter,
                               internal::GetPaintEngineFn getPaintEngine,
                               void** rasterVtable,
                               AfterTextCallback afterText) noexcept
    : kEngine_{FindEngine(painter, getPaintEngine, rasterVtable)},
      kOriginalTable_{kEngine_ != nullptr ? kEngine_->vtable : nullptr},
      kOriginalDraw_{kOriginalTable_ != nullptr
                         ? core::PointerCast<internal::DrawTextItemFn>(
                               kOriginalTable_[internal::kDrawTextItemSlot])
                         : nullptr},
      kFillColor_{kOriginalTable_ != nullptr
                      ? core::PointerCast<internal::FillColorFn>(
                            kOriginalTable_[internal::kFillColorSlot])
                      : nullptr},
      kAfterText_{afterText},
      kPrevious_{Current()},
      table_{CloneTable(kOriginalTable_)} {
  if (kEngine_ == nullptr) {
    return;
  }
  table_[internal::kDrawTextItemSlot + 1] =
      core::PointerCast<void*>(&RasterTextHook::DrawTextItem);
  Current() = this;
  kEngine_->vtable = table_.data() + 1;
}

RasterTextHook::~RasterTextHook() {
  if (kEngine_ == nullptr) {
    return;
  }
  kEngine_->vtable = kOriginalTable_;
  Current() = kPrevious_;
}

const RasterTextHook*& RasterTextHook::Current() noexcept {
  thread_local const RasterTextHook* hook{nullptr};
  return hook;
}

const RasterTextHook& RasterTextHook::Owner(
    const internal::PaintEngine& engine) noexcept {
  for (const auto* hook{Current()}; hook != nullptr; hook = hook->kPrevious_) {
    if (hook->kEngine_ == &engine) {
      return *hook;
    }
  }
  std::terminate();
}

void RasterTextHook::DrawTextItem(internal::PaintEngine* engine,
                                  const internal::PointF& position,
                                  const internal::TextItem& item) noexcept {
  const auto& hook{Owner(*engine)};
  const internal::RectF kBounds{TextBounds(position, item)};
  hook.kOriginalDraw_(engine, position, item);
  const RasterCanvas kCanvas{*engine, hook.kFillColor_};
  hook.kAfterText_(kCanvas, kBounds);
}

}  // namespace rgf::qt
