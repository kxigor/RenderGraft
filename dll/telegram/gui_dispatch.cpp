#include "gui_dispatch.hpp"

#include <windows.h>

#include <atomic>
#include <bit>
#include <exception>
#include <expected>
#include <optional>
#include <rgf/win/error.hpp>
#include <rgf/win/module_view.hpp>
#include <system_error>
#include <tuple>
#include <utility>

namespace rgf::telegram {

namespace {

const WCHAR kModuleAnchor{};
constexpr DWORD kRetryMilliseconds{10};

class WindowsHook {
 public:
  explicit WindowsHook(HHOOK hook) noexcept : hook_{hook} {}

  WindowsHook(const WindowsHook&) = delete;
  WindowsHook& operator=(const WindowsHook&) = delete;

  WindowsHook(WindowsHook&& other) noexcept : hook_{other.Release()} {}
  WindowsHook& operator=(WindowsHook&&) = delete;

  ~WindowsHook() { std::ignore = Reset(); }

  [[nodiscard]] HHOOK Get() const noexcept { return hook_; }
  [[nodiscard]] HHOOK Release() noexcept {
    return std::exchange(hook_, nullptr);
  }

  [[nodiscard]] std::expected<void, std::error_code> Reset() noexcept {
    if (hook_ != nullptr) {
      if (UnhookWindowsHookEx(/*hhk = */ hook_) == FALSE) {
        return std::unexpected{win::LastError()};
      }
      hook_ = nullptr;
    }
    return {};
  }

 private:
  HHOOK hook_;
};

struct GuiCall {
  GuiCall(HWND target, UINT command, GuiCallback action,
          WindowsHook ownedHook) noexcept
      : kWindow_{target},
        kMessage_{command},
        callback{action},
        hook{std::move(ownedHook)} {}

  HWND__* const kWindow_;
  const UINT kMessage_;
  std::atomic<GuiCallback> callback;
  WindowsHook hook;
  std::error_code result;
  bool retry{false};
  std::atomic<bool> complete{false};
};

struct DispatchState {
  std::optional<GuiCall> call;
  std::atomic<GuiCall*> current{nullptr};
  std::atomic_flag busy;
};

[[nodiscard]] DispatchState& State() noexcept {
  static DispatchState state;
  return state;
}

[[nodiscard]] GuiCall& CurrentCall() noexcept {
  GuiCall* const kCall{State().current.load(std::memory_order_acquire)};
  if (kCall == nullptr) {
    std::terminate();
  }
  return *kCall;
}

[[nodiscard]] unsigned int& WindowHookDepth() noexcept {
  static thread_local unsigned int depth{0};
  return depth;
}

class HookDepth {
 public:
  HookDepth() noexcept { ++WindowHookDepth(); }

  HookDepth(const HookDepth&) = delete;
  HookDepth& operator=(const HookDepth&) = delete;

  HookDepth(HookDepth&&) = delete;
  HookDepth& operator=(HookDepth&&) = delete;

  ~HookDepth() { --WindowHookDepth(); }
};

void ClearCall() noexcept {
  State().current.store(nullptr, std::memory_order_release);
  State().call.reset();
  State().busy.clear(std::memory_order_release);
}

[[nodiscard]] std::expected<void, std::error_code> AbortCall(
    std::error_code error) noexcept {
  GuiCall& call{CurrentCall()};
  call.callback.store(nullptr, std::memory_order_release);
  const auto kReset{call.hook.Reset()};
  if (!kReset) {
    return std::unexpected{kReset.error()};
  }
  return std::unexpected{error};
}

LRESULT CALLBACK Dispatch(int code, WPARAM parameter,
                          LPARAM messagePointer) noexcept {
  const HookDepth kDepth{};
  GuiCall* const kCall{State().current.load(std::memory_order_acquire)};
  if (code < 0 || kCall == nullptr) {
    return CallNextHookEx(/*hhk = */ nullptr, /*nCode = */ code,
                          /*wParam = */ parameter,
                          /*lParam = */ messagePointer);
  }

  const auto* const kMessage{std::bit_cast<const CWPSTRUCT*>(messagePointer)};
  if (kMessage->hwnd != kCall->kWindow_ ||
      kMessage->message != kCall->kMessage_ ||
      kMessage->wParam != std::bit_cast<WPARAM>(kCall)) {
    return CallNextHookEx(/*hhk = */ nullptr, /*nCode = */ code,
                          /*wParam = */ parameter,
                          /*lParam = */ messagePointer);
  }
  if (kCall->complete.load(std::memory_order_acquire)) {
    return 0;
  }

  const GuiCallback kCallback{kCall->callback.load(std::memory_order_acquire)};
  if (kCallback == nullptr) {
    return 0;
  }

  kCall->retry = WindowHookDepth() != 1;
  kCall->result = kCall->retry
                      ? std::error_code{ERROR_BUSY, std::system_category()}
                      : kCallback();
  kCall->retry = kCall->result.value() == ERROR_BUSY &&
                 kCall->result.category() == std::system_category();
  if (!kCall->retry) {
    const auto kReset{kCall->hook.Reset()};
    if (!kReset) {
      kCall->result = kReset.error();
    }
  }
  kCall->complete.store(true, std::memory_order_release);
  return 0;
}

[[nodiscard]] std::expected<void, std::error_code> PrepareCall(
    HWND window, GuiCallback callback) {
  DWORD processId{};
  const DWORD kThreadId{GetWindowThreadProcessId(
      /*hWnd = */ window, /*lpdwProcessId = */ &processId)};
  if (kThreadId == 0) {
    const std::error_code kError{win::LastError()};
    return std::unexpected{kError};
  }
  if (processId != GetCurrentProcessId() || kThreadId == GetCurrentThreadId()) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_PARAMETER, std::system_category()}};
  }

  const std::expected<win::ModuleView, std::error_code> kModule{
      win::ModuleView::FromAddress(&kModuleAnchor)};
  if (!kModule) {
    return std::unexpected{kModule.error()};
  }
  const UINT kMessage{RegisterWindowMessageW(
      /*lpString = */ L"RenderGraft.Telegram.GuiDispatch")};
  if (kMessage == 0) {
    const std::error_code kError{win::LastError()};
    return std::unexpected{kError};
  }

  WindowsHook hook{SetWindowsHookExW(/*idHook = */ WH_CALLWNDPROC,
                                     /*lpfn = */ &Dispatch,
                                     /*hmod = */ kModule->Get(),
                                     /*dwThreadId = */ kThreadId)};
  if (hook.Get() == nullptr) {
    const std::error_code kError{win::LastError()};
    return std::unexpected{kError};
  }
  GuiCall& call{
      State().call.emplace(window, kMessage, callback, std::move(hook))};
  State().current.store(&call, std::memory_order_release);
  return {};
}

[[nodiscard]] std::expected<void, std::error_code> SendCall() {
  GuiCall& call{CurrentCall()};
  for (;;) {
    call.complete.store(false, std::memory_order_release);
    SetLastError(/*dwErrCode = */ ERROR_SUCCESS);
    std::ignore = SendMessageW(/*hWnd = */ call.kWindow_,
                               /*Msg = */ call.kMessage_,
                               /*wParam = */ std::bit_cast<WPARAM>(&call),
                               /*lParam = */ 0);
    if (!call.complete.load(std::memory_order_acquire)) {
      const std::error_code kError{win::LastError()};
      return AbortCall(
          kError ? kError
                 : std::error_code{ERROR_GEN_FAILURE, std::system_category()});
    }
    if (call.retry) {
      Sleep(/*dwMilliseconds = */ kRetryMilliseconds);
      continue;
    }
    if (call.hook.Get() != nullptr) {
      return AbortCall(call.result);
    }

    const std::error_code kResult{call.result};
    ClearCall();
    if (kResult) {
      return std::unexpected{kResult};
    }
    return {};
  }
}

}  // namespace

std::expected<void, std::error_code> OnGuiThread(HWND window,
                                                 GuiCallback callback) {
  if (callback == nullptr) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_PARAMETER, std::system_category()}};
  }
  if (State().busy.test_and_set(std::memory_order_acquire)) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }

  const std::expected<void, std::error_code> kPrepared{
      PrepareCall(window, callback)};
  if (!kPrepared) {
    ClearCall();
    return std::unexpected{kPrepared.error()};
  }
  return SendCall();
}

}  // namespace rgf::telegram
