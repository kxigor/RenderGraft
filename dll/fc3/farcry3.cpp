#include "farcry3.hpp"

#include <bit>
#include <cstdint>
#include <rgf/abi/abi.hpp>
#include <rgf/app/detour_guard.hpp>
#include <rgf/app/scoped_hooks.hpp>
#include <rgf/config/config.hpp>
#include <rgf/esp/esp.hpp>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/integrations/fc3/fc3.hpp>
#include <rgf/process/memory.hpp>

#include "internal.hpp"

namespace rgf::dll {

namespace {

constexpr std::uint64_t kStatusInterval{1800};

void RGF_ENGINE_CALL RunAllGroupDetour(void* scheduler) {
  const app::DetourGuard kInside{};

  if (config::Current().espEnabled.load()) {
    integrations::fc3::TakeCensus(std::bit_cast<std::uintptr_t>(scheduler));
  }
  ProbeTrace();
  UpdateReach();

  integrations::fc3::RunAllGroupFn original{
      integrations::fc3::OriginalRunAllGroup()};
  if (original != nullptr) {
    original(scheduler);
  }
}

}  // namespace

const char* FarCry3::Name() const noexcept { return "Far Cry 3"; }

void FarCry3::InstallHooks(app::ScopedHooks& hooks) {
  hooks.Add<integrations::fc3::UpdateBinding>(
      hook::VmtHook::Create(hook::Vtable{integrations::fc3::RunAllGroupSlot()},
                            0),
      &RunAllGroupDetour, "entity update");
}

void FarCry3::OnFrame() noexcept {
  static std::uint64_t frames{0};
  ++frames;

  process::ForgetReadableRegions();
  integrations::fc3::PublishCensus();
  SteadyAim();
  RunAssist();

  if (frames % kStatusInterval == 0) {
    LogStatus(frames);
  }

  esp::DrawFrame();
}

}  // namespace rgf::dll
