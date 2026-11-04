#pragma once

#include <windows.h>

#include <expected>
#include <system_error>

namespace rgf::win {

using WindowCallback = std::error_code (*)() noexcept;

[[nodiscard]] std::expected<void, std::error_code> OnWindowThread(
    HWND window, WindowCallback callback);

}  // namespace rgf::win
