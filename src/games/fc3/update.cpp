#include <bit>
#include <rgf/games/fc3/internal/layout/scheduler.hpp>
#include <rgf/games/fc3/internal/module.hpp>
#include <rgf/games/fc3/update.hpp>
#include <rgf/hook/hook.hpp>

namespace rgf::games::fc3 {

namespace {

[[nodiscard]] const hook::Hook*& BoundUpdate() noexcept {
  static const hook::Hook* hook{nullptr};
  return hook;
}

}  // namespace

void** RunAllGroupSlot() noexcept {
  return std::bit_cast<void**>(Resolve(kRunAllGroupSlotAddress));
}

UpdateBinding::UpdateBinding(const hook::Hook& hook) noexcept {
  BoundUpdate() = &hook;
}

UpdateBinding::~UpdateBinding() { BoundUpdate() = nullptr; }

RunAllGroupFn OriginalRunAllGroup() noexcept {
  const hook::Hook* const kHook{BoundUpdate()};
  if (kHook == nullptr) {
    return nullptr;
  }

  return kHook->Original<RunAllGroupFn>();
}

}  // namespace rgf::games::fc3
