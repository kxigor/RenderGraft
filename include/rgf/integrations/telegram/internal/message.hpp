#pragma once

#include <rgf/qt/internal/abi.hpp>

namespace rgf::integrations::telegram::internal {

struct Message;
struct PaintContext;

using MessageDrawFn = void (*)(const Message* message,
                               qt::internal::Painter& painter,
                               const PaintContext& context);

}  // namespace rgf::integrations::telegram::internal
