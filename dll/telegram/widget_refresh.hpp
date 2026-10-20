#pragma once

#include <windows.h>

#include <expected>
#include <system_error>

namespace rgf::telegram {

class RenderTarget;

[[nodiscard]] std::expected<void, std::error_code> RefreshWindow(
    const RenderTarget& target, HWND window) noexcept;

}  // namespace rgf::telegram
