#include <windows.h>

#include <cstddef>
#include <expected>
#include <rgf/hook/error.hpp>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/win/error.hpp>
#include <rgf/win/protect_guard.hpp>
#include <system_error>
#include <tuple>
#include <utility>

namespace rgf::hook {

namespace {

[[nodiscard]] std::expected<void*, std::error_code> ReadSlot(
    void** slot) noexcept {
  MEMORY_BASIC_INFORMATION info{};
  const SIZE_T kQueried{VirtualQuery(
      /*lpAddress = */ static_cast<LPCVOID>(slot),
      /*lpBuffer  = */ &info,
      /*dwLength  = */ sizeof(info))};
  if (kQueried != sizeof(info)) {
    return std::unexpected{win::LastError()};
  }
  if (info.State != MEM_COMMIT || info.Protect == PAGE_NOACCESS ||
      (info.Protect & PAGE_GUARD) != 0) {
    return std::unexpected{make_error_code(Error::SlotNotReadable)};
  }

  return *slot;
}

}  // namespace

std::expected<VmtHook, std::error_code> VmtHook::Create(
    Vtable table, std::size_t index) noexcept {
  if (table.Get() == nullptr) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  void** const kSlot{table.Slot(index)};

  const std::expected<void*, std::error_code> kOriginal{ReadSlot(kSlot)};
  if (!kOriginal) {
    return std::unexpected{kOriginal.error()};
  }

  return VmtHook{kSlot, *kOriginal};
}

VmtHook::VmtHook(void** slot, void* original) noexcept
    : slot_{slot}, original_{original} {}

VmtHook::VmtHook(VmtHook&& other) noexcept
    : isEnabled_{std::exchange(other.isEnabled_, false)},
      detour_{std::exchange(other.detour_, nullptr)},
      slot_{std::exchange(other.slot_, nullptr)},
      original_{std::exchange(other.original_, nullptr)} {}

VmtHook::~VmtHook() { std::ignore = Disable(); }

std::expected<void, std::error_code> VmtHook::SetDetourRaw(
    void* detour) noexcept {
  if (slot_ == nullptr) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  if (isEnabled_) {
    return std::unexpected{make_error_code(Error::HookAlreadyApplied)};
  }

  detour_ = detour;

  return {};
}

void* VmtHook::OriginalRaw() const noexcept { return original_; }

std::expected<void, std::error_code> VmtHook::Enable() noexcept {
  if (slot_ == nullptr) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  if (detour_ == nullptr) {
    return std::unexpected{make_error_code(Error::DetourNotSet)};
  }
  if (isEnabled_) {
    return std::unexpected{make_error_code(Error::HookAlreadyApplied)};
  }

  return SetEnabled(true);
}

std::expected<void, std::error_code> VmtHook::Disable() noexcept {
  if (slot_ == nullptr) {
    return {};
  }
  if (!isEnabled_) {
    return std::unexpected{make_error_code(Error::HookNotApplied)};
  }

  return SetEnabled(false);
}

std::expected<void, std::error_code> VmtHook::SetEnabled(
    bool isEnabled) noexcept {
  std::expected<win::ProtectGuard, std::error_code> guard{
      win::ProtectGuard::Create(GetCurrentProcess(), static_cast<LPVOID>(slot_),
                                sizeof(void*), PAGE_READWRITE)};
  if (!guard) {
    return std::unexpected{guard.error()};
  }

  *slot_ = isEnabled ? detour_ : original_;
  isEnabled_ = isEnabled;

  return {};
}

}  // namespace rgf::hook
