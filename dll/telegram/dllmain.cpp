#include <windows.h>

#include <rgf/module/entry.hpp>
#include <rgf/telegram/render_probe.hpp>

#include "diagnostics.hpp"

namespace {

void OnAttach() {
  rgf::telegram::RunRenderProbe(&rgf::telegram::RunDiagnostics);
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason,
                      [[maybe_unused]] LPVOID reserved) {
  return rgf::module::DllEntry(instance, reason, &OnAttach);
}
