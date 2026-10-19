#pragma once

#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_canvas.hpp>

namespace rgf::telegram {

void DrawRasterMarker(const qt::RasterCanvas& canvas,
                      const qt::internal::RectF& bounds) noexcept;

}  // namespace rgf::telegram
