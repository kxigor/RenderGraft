#pragma once

#include <rgf/qt/internal/abi.hpp>

namespace rgf::integrations::telegram::internal {

using HistoryPaintFn = void (*)(qt::internal::Widget* widget,
                                qt::internal::PaintEvent* event);

}  // namespace rgf::integrations::telegram::internal
