#include "probe.hpp"

#include <windows.h>

#include <afw/module/entry.hpp>
#include <afw/win/scoped_handle.hpp>
#include <atomic>
#include <stdexcept>
#include <string>

namespace {

using afw::test::module_probe::Outcome;

constexpr Outcome kOutcome{AFW_MODULE_PROBE_THROWS != 0 ? Outcome::Throw
                                                        : Outcome::Return};
constexpr DWORD kWaitMilliseconds{10000};
std::atomic<DWORD> threadId{0};

[[nodiscard]] HANDLE OpenProbeEvent(PCWSTR state) {
  const std::wstring kName{afw::test::module_probe::EventName(kOutcome, state)};
  return OpenEventW(
      /*dwDesiredAccess = */ SYNCHRONIZE | EVENT_MODIFY_STATE,
      /*bInheritHandle  = */ FALSE,
      /*lpName          = */ kName.c_str());
}

class SignalOnExit {
 public:
  explicit SignalOnExit(HANDLE event) noexcept : kEvent_{event} {}

  SignalOnExit(const SignalOnExit&) = delete;
  SignalOnExit& operator=(const SignalOnExit&) = delete;
  SignalOnExit(SignalOnExit&&) = delete;
  SignalOnExit& operator=(SignalOnExit&&) = delete;

  ~SignalOnExit() {
    SetEvent(
        /*hEvent = */ kEvent_);
  }

 private:
  const HANDLE kEvent_;
};

void OnAttach() {
  const afw::win::ScopedHandle kStarted{OpenProbeEvent(L"started")};
  const afw::win::ScopedHandle kRelease{OpenProbeEvent(L"release")};
  const afw::win::ScopedHandle kDestroyed{OpenProbeEvent(L"destroyed")};
  if (!kStarted || !kRelease || !kDestroyed) {
    throw std::runtime_error{"could not open the module probe events"};
  }

  const SignalOnExit kSignal{kDestroyed.Get()};
  threadId.store(GetCurrentThreadId());
  SetEvent(
      /*hEvent = */ kStarted.Get());
  if (WaitForSingleObject(
          /*hHandle        = */ kRelease.Get(),
          /*dwMilliseconds = */ kWaitMilliseconds) != WAIT_OBJECT_0) {
    throw std::runtime_error{"module probe release timed out"};
  }

  if constexpr (kOutcome == Outcome::Throw) {
    throw std::runtime_error{"module probe callback failure"};
  }
}

}  // namespace

extern "C" DWORD AfwModuleProbeThreadId() noexcept { return threadId.load(); }

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
  return afw::module::DllEntry(instance, reason, &OnAttach);
}
