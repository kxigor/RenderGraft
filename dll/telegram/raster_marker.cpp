#include "raster_marker.hpp"

#include <cstdint>

namespace rgf::telegram {

void DrawRasterMarker(const qt::RasterCanvas& canvas,
                      const qt::internal::RectF& bounds) noexcept {
  constexpr double kPadding{2.0};
  constexpr std::uint16_t kMaximumChannel{0xffff};
  constexpr qt::internal::Color kMarkerColor{.spec = 1,
                                             .alpha = kMaximumChannel,
                                             .red = 0xa0a0,
                                             .green = 0x2020,
                                             .blue = 0xf0f0,
                                             .padding = 0};
  const qt::internal::RectF kRectangle{
      .x = bounds.x - kPadding,
      .y = bounds.y - kPadding,
      .width = bounds.width + (2.0 * kPadding),
      .height = bounds.height + (2.0 * kPadding)};
  canvas.FillRect(kRectangle, kMarkerColor);
}

}  // namespace rgf::telegram
