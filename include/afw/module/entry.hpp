#pragma once

#include <windows.h>

namespace afw::module {

using AttachCallback = void (*)();

[[nodiscard]] BOOL DllEntry(HMODULE self, DWORD reason,
                            AttachCallback onAttach) noexcept;

}  // namespace afw::module
