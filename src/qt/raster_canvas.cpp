#include <rgf/qt/raster_canvas.hpp>

namespace rgf::qt {

RasterCanvas::RasterCanvas(internal::PaintEngine& engine,
                           internal::FillColorFn fillColor) noexcept
    : kEngine_{engine}, kFillColor_{fillColor} {}

void RasterCanvas::FillRect(const internal::RectF& rectangle,
                            const internal::Color& color) const noexcept {
  kFillColor_(&kEngine_, rectangle, color);
}

}  // namespace rgf::qt
