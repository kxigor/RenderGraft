#pragma once

#include <rgf/qt/internal/abi.hpp>

namespace rgf::telegram::internal {

struct Message;
struct PaintContext;

using MessageDraw = void (*)(const Message* message,
                             qt::internal::Painter& painter,
                             const PaintContext& context);

}  // namespace rgf::telegram::internal
