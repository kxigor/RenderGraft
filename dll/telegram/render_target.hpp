#pragma once

#include <cstdint>
#include <expected>
#include <rgf/hook/vtable.hpp>
#include <system_error>

#include "internal/qt.hpp"

namespace rgf::telegram {

class RenderTarget {
 public:
  [[nodiscard]] static std::expected<RenderTarget, std::error_code> Locate();

  [[nodiscard]] hook::Vtable MessageTable() const noexcept;
  [[nodiscard]] void** RasterTable() const noexcept;
  [[nodiscard]] internal::GetPaintEngine PaintEngine() const noexcept;

 private:
  explicit RenderTarget(std::uintptr_t base) noexcept;

  const std::uintptr_t kBase_;
};

}  // namespace rgf::telegram
