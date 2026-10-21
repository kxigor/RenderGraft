#include <windows.h>

#include <rgf/app/app.hpp>
#include <rgf/module/entry.hpp>

#include "farcry3.hpp"

namespace {

void OnAttach() {
  rgf::dll::FarCry3 game{};
  rgf::app::Run(game);
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason,
                      [[maybe_unused]] LPVOID reserved) {
  return rgf::module::DllEntry(instance, reason, &OnAttach);
}
