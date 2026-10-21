#pragma once

#include <windows.h>

#include <string>

namespace rgf::test::module_probe {

enum class Outcome { Return, Throw };

[[nodiscard]] inline std::wstring EventName(Outcome outcome, PCWSTR state) {
  return L"Local\\rgf_module_" + std::to_wstring(GetCurrentProcessId()) +
         (outcome == Outcome::Throw ? L"_throw_" : L"_return_") + state;
}

using ThreadIdFn = DWORD (*)() noexcept;

}  // namespace rgf::test::module_probe
