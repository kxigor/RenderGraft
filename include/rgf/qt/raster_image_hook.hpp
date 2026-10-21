#pragma once

#include <array>
#include <cstdint>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_canvas.hpp>

namespace rgf::qt {

class RasterImageHook {
 public:
  using ImageCallback = void (*)(const RasterCanvas& canvas,
                                 const internal::RectF& bounds) noexcept;

  RasterImageHook(internal::PaintEngine* engine, void** rasterVtable,
                  ImageCallback onImage) noexcept;

  RasterImageHook(const RasterImageHook&) = delete;
  RasterImageHook& operator=(const RasterImageHook&) = delete;
  RasterImageHook(RasterImageHook&&) = delete;
  RasterImageHook& operator=(RasterImageHook&&) = delete;

  ~RasterImageHook();

  [[nodiscard]] bool IsActive() const noexcept;

 private:
  [[nodiscard]] static internal::PaintEngine* FindEngine(
      internal::PaintEngine* engine, void** rasterVtable,
      ImageCallback onImage) noexcept;
  [[nodiscard]] static const RasterImageHook*& Current() noexcept;
  [[nodiscard]] static const RasterImageHook& Owner(
      const internal::PaintEngine& engine) noexcept;

  static void DrawPixmapRect(internal::PaintEngine* engine,
                             const internal::RectF& rectangle,
                             const internal::Pixmap& pixmap,
                             const internal::RectF& source) noexcept;
  static void DrawTiledPixmap(internal::PaintEngine* engine,
                              const internal::RectF& rectangle,
                              const internal::Pixmap& pixmap,
                              const internal::PointF& source) noexcept;
  static void DrawImageRect(internal::PaintEngine* engine,
                            const internal::RectF& rectangle,
                            const internal::Image& image,
                            const internal::RectF& source,
                            std::int32_t flags) noexcept;
  static void DrawPixmapPoint(internal::PaintEngine* engine,
                              const internal::PointF& position,
                              const internal::Pixmap& pixmap) noexcept;
  static void DrawImagePoint(internal::PaintEngine* engine,
                             const internal::PointF& position,
                             const internal::Image& image) noexcept;

  void Draw(const internal::RectF& bounds) const noexcept;

  internal::PaintEngine* const kEngine_;
  void** const kOriginalTable_;
  const internal::FillColorFn kFillColor_;
  const ImageCallback kOnImage_;
  const RasterImageHook* const kPrevious_;
  std::array<void*, internal::kRasterVirtualCount + 1> table_;
};

}  // namespace rgf::qt
