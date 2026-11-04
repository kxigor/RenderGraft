#pragma once

#include <rgf/qt/internal/abi.hpp>

namespace rgf::qt {

class RasterTextHook;
class RasterImageHook;

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
  friend class RasterImageHook;

  RasterCanvas(internal::PaintEngine& engine,
               internal::FillColorFn fillColor) noexcept;

  internal::PaintEngine& kEngine_;
  const internal::FillColorFn kFillColor_;
};

}  // namespace rgf::qt
