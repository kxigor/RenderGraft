#include <MinHook.h>

#include <cstddef>
#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <new>
#include <rgf/hook/inline_hook.hpp>
#include <string>
#include <system_error>
#include <utility>

namespace rgf::hook {

namespace {

class MinHookCategory final : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override {
    return "rgf::hook::minhook";
  }

  [[nodiscard]] std::string message(int value) const override {
    return MH_StatusToString(static_cast<MH_STATUS>(value));
  }
};

[[nodiscard]] std::error_code BackendError(MH_STATUS status) noexcept {
  static const MinHookCategory kCategory;
  return {static_cast<int>(status), kCategory};
}

class SessionRegistry {
 public:
  [[nodiscard]] std::expected<void, std::error_code> Acquire() noexcept {
    const std::scoped_lock kLock{mutex_};
    if (owners_ == 0) {
      const auto kStatus{MH_Initialize()};
      if (kStatus != MH_OK) {
        return std::unexpected{BackendError(kStatus)};
      }
    }
    ++owners_;
    return {};
  }

  void Release() noexcept {
    const std::scoped_lock kLock{mutex_};
    --owners_;
    if (owners_ == 0 && MH_Uninitialize() != MH_OK) {
      std::terminate();
    }
  }

 private:
  std::mutex mutex_;
  std::size_t owners_{};
};

[[nodiscard]] SessionRegistry& Registry() noexcept {
  static SessionRegistry registry;
  return registry;
}

class MinHookSession {
 public:
  [[nodiscard]] static std::expected<MinHookSession, std::error_code>
  Acquire() noexcept {
    const auto kAcquired{Registry().Acquire()};
    if (!kAcquired) {
      return std::unexpected{kAcquired.error()};
    }
    return MinHookSession{};
  }

  MinHookSession(const MinHookSession&) = delete;
  MinHookSession& operator=(const MinHookSession&) = delete;

  MinHookSession(MinHookSession&& other) noexcept
      : owns_{std::exchange(other.owns_, false)} {}
  MinHookSession& operator=(MinHookSession&&) = delete;

  ~MinHookSession() {
    if (owns_) {
      Registry().Release();
    }
  }

 private:
  MinHookSession() = default;

  bool owns_{true};
};

}  // namespace

class InlineHook::Binding {
 public:
  Binding(MinHookSession session, void* target, void* original) noexcept
      : kSession_{std::move(session)}, kTarget_{target}, kOriginal_{original} {}

  Binding(const Binding&) = delete;
  Binding& operator=(const Binding&) = delete;
  Binding(Binding&&) = delete;
  Binding& operator=(Binding&&) = delete;

  ~Binding() {
    if (!Disable() || MH_RemoveHook(kTarget_) != MH_OK) {
      std::terminate();
    }
  }

  [[nodiscard]] std::expected<void, std::error_code> Enable() noexcept {
    const auto kStatus{MH_EnableHook(kTarget_)};
    if (kStatus != MH_OK) {
      return std::unexpected{BackendError(kStatus)};
    }
    enabled_ = true;
    return {};
  }

  [[nodiscard]] std::expected<void, std::error_code> Disable() noexcept {
    if (!enabled_) {
      return {};
    }
    const auto kStatus{MH_DisableHook(kTarget_)};
    if (kStatus != MH_OK) {
      return std::unexpected{BackendError(kStatus)};
    }
    enabled_ = false;
    return {};
  }

  [[nodiscard]] void* Original() const noexcept { return kOriginal_; }

 private:
  const MinHookSession kSession_;
  void* const kTarget_;
  void* const kOriginal_;
  bool enabled_{};
};

InlineHook::InlineHook() noexcept = default;

InlineHook::InlineHook(std::unique_ptr<Binding> binding) noexcept
    : binding_{std::move(binding)} {}

InlineHook::InlineHook(InlineHook&& other) noexcept = default;

InlineHook::~InlineHook() = default;

std::expected<InlineHook, std::error_code> InlineHook::CreateRaw(
    void* target, void* detour) noexcept {
  if (target == nullptr || detour == nullptr || target == detour) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  auto session{MinHookSession::Acquire()};
  if (!session) {
    return std::unexpected{session.error()};
  }
  void* original{};
  const auto kStatus{MH_CreateHook(target, detour, &original)};
  if (kStatus != MH_OK) {
    return std::unexpected{BackendError(kStatus)};
  }
  std::unique_ptr<Binding> binding{
      new (std::nothrow) Binding{std::move(*session), target, original}};
  if (!binding) {
    if (MH_RemoveHook(target) != MH_OK) {
      std::terminate();
    }
    return std::unexpected{std::make_error_code(std::errc::not_enough_memory)};
  }
  return InlineHook{std::move(binding)};
}

void* InlineHook::OriginalRaw() const noexcept {
  return binding_ ? binding_->Original() : nullptr;
}

std::expected<void, std::error_code> InlineHook::Enable() noexcept {
  if (!binding_) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  return binding_->Enable();
}

std::expected<void, std::error_code> InlineHook::Disable() noexcept {
  return binding_ ? binding_->Disable()
                  : std::expected<void, std::error_code>{};
}

}  // namespace rgf::hook
