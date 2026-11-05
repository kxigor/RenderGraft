#pragma once

#include <rgf/qt/internal/abi.hpp>

namespace rgf::integrations::telegram::internal {

struct PeerData;
struct PeerUserpicView;
struct PaintUserpicContext;

// Telegram Desktop 7.3.5, MSVC x64: the 16-byte by-value context is
// passed indirectly in R9. Preserve the caller's temporary when forwarding
// across the compiler ABI boundary rather than copying it as a GCC aggregate.
using PaintPeerUserpicFn = void (*)(const PeerData* peer,
                                    qt::internal::Painter& painter,
                                    PeerUserpicView& view,
                                    PaintUserpicContext& context);

}  // namespace rgf::integrations::telegram::internal
