#pragma once

#include <rgf/app/game.hpp>
#include <rgf/app/scoped_hooks.hpp>

namespace rgf::dll {

class FarCry3 final : public app::Game {
 public:
  [[nodiscard]] const char* Name() const noexcept override;

  void InstallHooks(app::ScopedHooks& hooks) override;

  void OnFrame() noexcept override;
};

}  // namespace rgf::dll
