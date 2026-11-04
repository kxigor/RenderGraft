#pragma once

#include <expected>
#include <rgf/render/overlay.hpp>
#include <system_error>

struct IDXGISwapChain;

namespace rgf::render::dxgi {

[[nodiscard]] std::expected<void, std::error_code> DrawOverlayFrame(
    IDXGISwapChain* swapChain, FrameCallback onFrame) noexcept;

}  // namespace rgf::render::dxgi
