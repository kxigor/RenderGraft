#include <windows.h>

#include <afw/app/app.hpp>
#include <afw/module/entry.hpp>

#include "farcry3.hpp"

namespace {

void OnAttach() {
  afw::dll::FarCry3 game{};
  afw::app::Run(game);
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason,
                      [[maybe_unused]] LPVOID reserved) {
  return afw::module::DllEntry(instance, reason, &OnAttach);
}
