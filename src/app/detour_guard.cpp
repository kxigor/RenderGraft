#include <windows.h>

#include <atomic>
#include <rgf/app/detour_guard.hpp>

namespace rgf::app {

namespace {

constexpr DWORD kDrainPollMilliseconds{10};
constexpr int kDrainTicks{200};
constexpr DWORD kSettleMilliseconds{100};

[[nodiscard]] std::atomic<int>& DetourDepth() noexcept {
  static std::atomic<int> depth{0};
  return depth;
}

}  // namespace

DetourGuard::DetourGuard() noexcept { DetourDepth().fetch_add(1); }

DetourGuard::~DetourGuard() { DetourDepth().fetch_sub(1); }

void WaitForDetours() noexcept {
  for (int tick{0}; tick < kDrainTicks; ++tick) {
    if (DetourDepth().load() == 0) {
      break;
    }
    Sleep(
        /*dwMilliseconds = */ kDrainPollMilliseconds);
  }

  Sleep(
      /*dwMilliseconds = */ kSettleMilliseconds);
}

}  // namespace rgf::app
