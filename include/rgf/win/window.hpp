#pragma once

#include <windows.h>

#include <expected>
#include <system_error>

namespace rgf::win {

[[nodiscard]] std::expected<HWND, std::error_code> FindVisibleUnownedWindow(
    DWORD processId) noexcept;

[[nodiscard]] std::expected<DWORD, std::error_code> WindowThreadId(
    HWND window) noexcept;

}  // namespace rgf::win
