#pragma once

#include <array>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_canvas.hpp>

namespace rgf::qt {

class RasterTextHook {
 public:
  using AfterTextCallback = void (*)(const RasterCanvas& canvas,
                                     const internal::RectF& bounds) noexcept;

  RasterTextHook(internal::Painter& painter,
                 internal::GetPaintEngine getPaintEngine, void** rasterVtable,
                 AfterTextCallback afterText) noexcept;

  RasterTextHook(const RasterTextHook&) = delete;
  RasterTextHook& operator=(const RasterTextHook&) = delete;

  RasterTextHook(RasterTextHook&&) = delete;
  RasterTextHook& operator=(RasterTextHook&&) = delete;

  ~RasterTextHook();

 private:
  [[nodiscard]] static const RasterTextHook*& Current() noexcept;
  [[nodiscard]] static const RasterTextHook& Owner(
      const internal::PaintEngine& engine) noexcept;

  static void DrawTextItem(internal::PaintEngine* engine,
                           const internal::PointF& position,
                           const internal::TextItem& item) noexcept;

  internal::PaintEngine* const kEngine_;
  void** const kOriginalTable_;
  const internal::DrawTextItem kOriginalDraw_;
  const internal::FillColor kFillColor_;
  const AfterTextCallback kAfterText_;
  const RasterTextHook* const kPrevious_;
  std::array<void*, internal::kRasterVirtualCount + 1> table_;
};

}  // namespace rgf::qt
