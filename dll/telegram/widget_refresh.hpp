#pragma once

#include <windows.h>

#include <expected>
#include <system_error>

namespace rgf::telegram {

[[nodiscard]] std::expected<void, std::error_code> RefreshWindow(
    HWND window) noexcept;

}  // namespace rgf::telegram
