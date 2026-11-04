#include <rgf/win/error.hpp>
#include <rgf/win/windows_hook.hpp>
#include <tuple>
#include <utility>

namespace rgf::win {

WindowsHook::WindowsHook(HHOOK hook) noexcept : hook_{hook} {}

WindowsHook::WindowsHook(WindowsHook&& other) noexcept
    : hook_{other.Release()} {}

WindowsHook::~WindowsHook() { std::ignore = Reset(); }

HHOOK WindowsHook::Get() const noexcept { return hook_; }

HHOOK WindowsHook::Release() noexcept { return std::exchange(hook_, nullptr); }

std::expected<void, std::error_code> WindowsHook::Reset() noexcept {
  if (hook_ != nullptr) {
    if (UnhookWindowsHookEx(/*hhk = */ hook_) == FALSE) {
      return std::unexpected{LastError()};
    }
    hook_ = nullptr;
  }
  return {};
}

}  // namespace rgf::win
