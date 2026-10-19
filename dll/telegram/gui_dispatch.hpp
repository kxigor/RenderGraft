#pragma once

#include <windows.h>

#include <expected>
#include <system_error>

namespace rgf::telegram {

using GuiCallback = std::error_code (*)() noexcept;

[[nodiscard]] std::expected<void, std::error_code> OnGuiThread(
    HWND window, GuiCallback callback);

}  // namespace rgf::telegram
