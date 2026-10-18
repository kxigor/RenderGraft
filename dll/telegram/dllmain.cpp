#include <windows.h>

#include <rgf/module/entry.hpp>

#include "diagnostics.hpp"

namespace {

void OnAttach() { rgf::telegram::RunDiagnostics(); }

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason,
                      [[maybe_unused]] LPVOID reserved) {
  return rgf::module::DllEntry(instance, reason, &OnAttach);
}
