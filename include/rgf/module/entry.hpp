#pragma once

#include <windows.h>

namespace rgf::module {

using AttachCallback = void (*)();

[[nodiscard]] BOOL DllEntry(HMODULE self, DWORD reason,
                            AttachCallback onAttach) noexcept;

}  // namespace rgf::module
