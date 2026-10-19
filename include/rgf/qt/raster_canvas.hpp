#pragma once

#include <rgf/qt/internal/abi.hpp>

namespace rgf::qt {

class RasterTextHook;

class RasterCanvas {
 public:
  RasterCanvas(const RasterCanvas&) = delete;
  RasterCanvas& operator=(const RasterCanvas&) = delete;

  RasterCanvas(RasterCanvas&&) = delete;
  RasterCanvas& operator=(RasterCanvas&&) = delete;

  ~RasterCanvas() = default;

  void FillRect(const internal::RectF& rectangle,
                const internal::Color& color) const noexcept;

 private:
  friend class RasterTextHook;

  RasterCanvas(internal::PaintEngine& engine,
               internal::FillColor fillColor) noexcept;

  internal::PaintEngine& kEngine_;
  const internal::FillColor kFillColor_;
};

}  // namespace rgf::qt
