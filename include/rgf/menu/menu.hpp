#pragma once

#include <windows.h>

namespace rgf::menu {

void Draw() noexcept;

[[nodiscard]] bool HandleMessage(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam) noexcept;

}  // namespace rgf::menu
