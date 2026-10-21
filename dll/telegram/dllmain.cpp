#include <windows.h>

#include <rgf/integrations/telegram/render_probe.hpp>
#include <rgf/module/entry.hpp>
#include <string>

#include "diagnostics.hpp"

namespace {

void OnAttach() {
  const auto kFilter{rgf::integrations::telegram::MessageSenderFilter::ForUser(
      RGF_TELEGRAM_MASKED_USER_ID)};
  if (!kFilter) {
    rgf::integrations::telegram::RunDiagnostics(
        std::string{"render_probe=unavailable\nstep=validating the selected "
                    "Telegram user\nreason="} +
        kFilter.error().message() + "\n");
    return;
  }
  rgf::integrations::telegram::RunRenderProbe(
      *kFilter, &rgf::integrations::telegram::RunDiagnostics);
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason,
                      [[maybe_unused]] LPVOID reserved) {
  return rgf::module::DllEntry(instance, reason, &OnAttach);
}
