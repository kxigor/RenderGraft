#include <windows.h>

#include <rgf/module/entry.hpp>

#include "render_probe.hpp"

namespace {

void OnAttach() { rgf::telegram::RunRenderProbe(); }

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason,
                      [[maybe_unused]] LPVOID reserved) {
  return rgf::module::DllEntry(instance, reason, &OnAttach);
}
