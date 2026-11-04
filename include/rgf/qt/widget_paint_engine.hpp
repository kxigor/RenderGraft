#pragma once

#include <expected>
#include <rgf/qt/internal/abi.hpp>
#include <system_error>

namespace rgf::qt {

[[nodiscard]] std::expected<internal::PaintEngine*, std::error_code>
WidgetPaintEngine(const internal::Widget& widget) noexcept;

}  // namespace rgf::qt
