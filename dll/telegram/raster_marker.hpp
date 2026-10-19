#pragma once

#include <array>
#include <cstdint>

#include "internal/qt.hpp"

namespace rgf::telegram {

class RasterMarker {
 public:
  RasterMarker(internal::Painter& painter,
               internal::GetPaintEngine getPaintEngine,
               void** rasterVtable) noexcept;

  RasterMarker(const RasterMarker&) = delete;
  RasterMarker& operator=(const RasterMarker&) = delete;

  RasterMarker(RasterMarker&&) = delete;
  RasterMarker& operator=(RasterMarker&&) = delete;

  ~RasterMarker();

  [[nodiscard]] static std::uint64_t TextCalls() noexcept;
  [[nodiscard]] static std::uint64_t MarkersDrawn() noexcept;

 private:
  [[nodiscard]] static const RasterMarker*& Current() noexcept;
  [[nodiscard]] static const RasterMarker& Owner(
      const internal::PaintEngine& engine) noexcept;

  static void DrawTextItem(internal::PaintEngine* engine,
                           const internal::PointF& position,
                           const internal::TextItem& item) noexcept;

  internal::PaintEngine* const kEngine_;
  void** const kOriginalTable_;
  const internal::DrawTextItem kOriginalDraw_;
  const internal::FillColor kFillColor_;
  const RasterMarker* const kPrevious_;
  std::array<void*, internal::kRasterVirtualCount + 1> table_;
};

}  // namespace rgf::telegram
