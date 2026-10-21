#include <rgf/config/config.hpp>
#include <rgf/hook/held_patch.hpp>
#include <rgf/integrations/fc3/fc3.hpp>

#include "internal.hpp"

namespace rgf::dll {

void SteadyAim() noexcept {
  static hook::HeldPatch recoil{&integrations::fc3::SuppressRecoil};
  static hook::HeldPatch spread{&integrations::fc3::SuppressSpread};
  static hook::HeldPatch sway{&integrations::fc3::SuppressSway};
  static hook::HeldPatch spentRounds{&integrations::fc3::IgnoreSpentRounds};
  static hook::HeldPatch emptyMagazine{&integrations::fc3::IgnoreEmptyMagazine};

  const config::Settings& kSettings{config::Current()};
  recoil.Follow(kSettings.noRecoil.load());
  spread.Follow(kSettings.noSpread.load());
  sway.Follow(kSettings.noSway.load());
  spentRounds.Follow(kSettings.infiniteAmmo.load());
  emptyMagazine.Follow(kSettings.infiniteAmmo.load());
}

}  // namespace rgf::dll
