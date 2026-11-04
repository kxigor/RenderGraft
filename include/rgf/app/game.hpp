#pragma once

#include <rgf/app/scoped_hooks.hpp>

namespace rgf::app {

class Game {
 public:
  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  Game(Game&&) = delete;
  Game& operator=(Game&&) = delete;

  virtual ~Game() = default;

  [[nodiscard]] virtual const char* Name() const noexcept = 0;

  virtual void InstallHooks(ScopedHooks& hooks) = 0;

  virtual void OnFrame() noexcept = 0;

 protected:
  Game() = default;
};

}  // namespace rgf::app
