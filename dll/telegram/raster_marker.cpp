#include "raster_marker.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <rgf/core/cast.hpp>
#include <span>

namespace rgf::telegram {

namespace {

[[nodiscard]] internal::PaintEngine* FindEngine(
    internal::Painter& painter, internal::GetPaintEngine getPaintEngine,
    void** rasterVtable) noexcept {
  auto* engine{getPaintEngine(&painter)};
  return engine != nullptr && engine->vtable == rasterVtable ? engine : nullptr;
}

[[nodiscard]] std::array<void*, internal::kRasterVirtualCount + 1> CloneTable(
    void** table) noexcept {
  std::array<void*, internal::kRasterVirtualCount + 1> result{};
  std::ranges::copy(std::span{table - 1, internal::kRasterVirtualCount + 1},
                    result.begin());
  return result;
}

[[nodiscard]] std::atomic<std::uint64_t>& TextCount() noexcept {
  static std::atomic<std::uint64_t> count{0};
  return count;
}

[[nodiscard]] std::atomic<std::uint64_t>& MarkerCount() noexcept {
  static std::atomic<std::uint64_t> count{0};
  return count;
}

}  // namespace

RasterMarker::RasterMarker(internal::Painter& painter,
                           internal::GetPaintEngine getPaintEngine,
                           void** rasterVtable) noexcept
    : kEngine_{FindEngine(painter, getPaintEngine, rasterVtable)},
      kOriginalTable_{kEngine_ != nullptr ? kEngine_->vtable : nullptr},
      kOriginalDraw_{kOriginalTable_ != nullptr
                         ? core::PointerCast<internal::DrawTextItem>(
                               kOriginalTable_[internal::kDrawTextItemSlot])
                         : nullptr},
      kFillColor_{kOriginalTable_ != nullptr
                      ? core::PointerCast<internal::FillColor>(
                            kOriginalTable_[internal::kFillColorSlot])
                      : nullptr},
      kPrevious_{Current()},
      table_{CloneTable(rasterVtable)} {
  if (kEngine_ == nullptr) {
    return;
  }
  table_[internal::kDrawTextItemSlot + 1] =
      core::PointerCast<void*>(&RasterMarker::DrawTextItem);
  Current() = this;
  kEngine_->vtable = table_.data() + 1;
}

RasterMarker::~RasterMarker() {
  if (kEngine_ == nullptr) {
    return;
  }
  kEngine_->vtable = kOriginalTable_;
  Current() = kPrevious_;
}

std::uint64_t RasterMarker::TextCalls() noexcept {
  return TextCount().load(std::memory_order_relaxed);
}

std::uint64_t RasterMarker::MarkersDrawn() noexcept {
  return MarkerCount().load(std::memory_order_relaxed);
}

const RasterMarker*& RasterMarker::Current() noexcept {
  thread_local const RasterMarker* marker{nullptr};
  return marker;
}

const RasterMarker& RasterMarker::Owner(
    const internal::PaintEngine& engine) noexcept {
  for (const auto* marker{Current()}; marker != nullptr;
       marker = marker->kPrevious_) {
    if (marker->kEngine_ == &engine) {
      return *marker;
    }
  }
  std::terminate();
}

void RasterMarker::DrawTextItem(internal::PaintEngine* engine,
                                const internal::PointF& position,
                                const internal::TextItem& item) noexcept {
  const auto& marker{Owner(*engine)};
  TextCount().fetch_add(1, std::memory_order_relaxed);
  marker.kOriginalDraw_(engine, position, item);

  constexpr double kMarkerSize{6.0};
  constexpr std::uint16_t kMaximumChannel{0xffff};
  constexpr internal::Color kMarkerColor{.spec = 1,
                                         .alpha = kMaximumChannel,
                                         .red = kMaximumChannel,
                                         .green = 0,
                                         .blue = kMaximumChannel,
                                         .padding = 0};
  const internal::RectF kRectangle{.x = position.x,
                                   .y = position.y - kMarkerSize,
                                   .width = kMarkerSize,
                                   .height = kMarkerSize};
  marker.kFillColor_(engine, kRectangle, kMarkerColor);
  MarkerCount().fetch_add(1, std::memory_order_relaxed);
}

}  // namespace rgf::telegram
