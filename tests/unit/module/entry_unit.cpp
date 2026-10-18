#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <bit>
#include <rgf/module/entry.hpp>
#include <rgf/win/scoped_handle.hpp>
#include <support/win_probe.hpp>

#include "probe.hpp"

namespace {

using rgf::test::module_probe::Outcome;

constexpr DWORD kWaitMilliseconds{10000};
int callbackCalls{0};

void CountAttach() { ++callbackCalls; }

// An assertion after LoadLibrary must also let the callback finish. This guard
// runs before the host closes its event handles; the DLL owns separate handles.
class ReleaseOnExit {
 public:
  explicit ReleaseOnExit(HANDLE event) noexcept : kEvent_{event} {}

  ReleaseOnExit(const ReleaseOnExit&) = delete;
  ReleaseOnExit& operator=(const ReleaseOnExit&) = delete;
  ReleaseOnExit(ReleaseOnExit&&) = delete;
  ReleaseOnExit& operator=(ReleaseOnExit&&) = delete;

  ~ReleaseOnExit() {
    SetEvent(
        /*hEvent = */ kEvent_);
  }

 private:
  const HANDLE kEvent_;
};

[[nodiscard]] HANDLE CreateProbeEvent(Outcome outcome, PCWSTR state) {
  return rgf::test::CreateProbeEvent(
      rgf::test::module_probe::EventName(outcome, state));
}

void ExerciseCallback(PCWSTR path, Outcome outcome) {
  const rgf::win::ScopedHandle kStarted{CreateProbeEvent(outcome, L"started")};
  const rgf::win::ScopedHandle kRelease{CreateProbeEvent(outcome, L"release")};
  const rgf::win::ScopedHandle kDestroyed{
      CreateProbeEvent(outcome, L"destroyed")};
  ASSERT_TRUE(static_cast<bool>(kStarted));
  ASSERT_TRUE(static_cast<bool>(kRelease));
  ASSERT_TRUE(static_cast<bool>(kDestroyed));
  const ReleaseOnExit kCleanup{kRelease.Get()};

  ASSERT_EQ(GetModuleHandleW(/*lpModuleName = */ path), nullptr);
  const HMODULE kModule{LoadLibraryW(/*lpLibFileName = */ path)};
  ASSERT_NE(kModule, nullptr) << "LoadLibraryW error " << GetLastError();
  ASSERT_EQ(WaitForSingleObject(
                /*hHandle        = */ kStarted.Get(),
                /*dwMilliseconds = */ kWaitMilliseconds),
            WAIT_OBJECT_0);

  const auto kThreadId{
      std::bit_cast<rgf::test::module_probe::ThreadIdFn>(GetProcAddress(
          /*hModule    = */ kModule,
          /*lpProcName = */ "RgfModuleProbeThreadId"))};
  ASSERT_NE(kThreadId, nullptr);
  const DWORD kWorkerId{kThreadId()};
  ASSERT_NE(kWorkerId, 0);
  EXPECT_NE(kWorkerId, GetCurrentThreadId());

  // Open before releasing: a thread ID alone cannot prove that self-unload has
  // finished, and a terminated thread ID may be reused.
  const rgf::win::ScopedHandle kWorker{OpenThread(
      /*dwDesiredAccess = */ SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION,
      /*bInheritHandle  = */ FALSE,
      /*dwThreadId      = */ kWorkerId)};
  ASSERT_TRUE(static_cast<bool>(kWorker));
  ASSERT_EQ(WaitForSingleObject(
                /*hHandle        = */ kDestroyed.Get(),
                /*dwMilliseconds = */ 0),
            WAIT_TIMEOUT);
  ASSERT_NE(SetEvent(/*hEvent = */ kRelease.Get()), FALSE);
  ASSERT_EQ(WaitForSingleObject(
                /*hHandle        = */ kWorker.Get(),
                /*dwMilliseconds = */ kWaitMilliseconds),
            WAIT_OBJECT_0);

  EXPECT_EQ(WaitForSingleObject(
                /*hHandle        = */ kDestroyed.Get(),
                /*dwMilliseconds = */ 0),
            WAIT_OBJECT_0);
  DWORD exitCode{};
  ASSERT_NE(GetExitCodeThread(
                /*hThread     = */ kWorker.Get(),
                /*lpExitCode  = */ &exitCode),
            FALSE);

  if (outcome == Outcome::Throw) {
    EXPECT_EQ(exitCode, static_cast<DWORD>(ERROR_UNHANDLED_EXCEPTION));
    ASSERT_EQ(GetModuleHandleW(/*lpModuleName = */ path), kModule);
    EXPECT_NE(FreeLibrary(/*hLibModule = */ kModule), FALSE);
    EXPECT_EQ(GetModuleHandleW(/*lpModuleName = */ path), nullptr);
  } else {
    EXPECT_EQ(exitCode, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(GetModuleHandleW(/*lpModuleName = */ path), nullptr);
  }
}

}  // namespace

TEST(ModuleEntryUnit, CallbackLocalsAreDestroyedBeforeTheWorkerUnloadsTheDll) {
  ExerciseCallback(RGF_MODULE_RETURN_PROBE_PATH, Outcome::Return);
}

TEST(ModuleEntryUnit, CallbackExceptionUnwindsLocalsAndRetainsTheDll) {
  ExerciseCallback(RGF_MODULE_THROW_PROBE_PATH, Outcome::Throw);
}

TEST(ModuleEntryUnit, OtherNotificationsDoNotRunTheCallback) {
  callbackCalls = 0;
  constexpr std::array<DWORD, 3> kReasons{DLL_THREAD_ATTACH, DLL_THREAD_DETACH,
                                          DLL_PROCESS_DETACH};
  for (const DWORD kReason : kReasons) {
    EXPECT_EQ(rgf::module::DllEntry(nullptr, kReason, &CountAttach), TRUE);
  }
  EXPECT_EQ(callbackCalls, 0);
}

TEST(ModuleEntryUnit, RejectsProcessAttachWithoutAModuleHandle) {
  callbackCalls = 0;
  SetLastError(/*dwErrCode = */ ERROR_SUCCESS);

  EXPECT_EQ(rgf::module::DllEntry(nullptr, DLL_PROCESS_ATTACH, &CountAttach),
            FALSE);
  EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER));
  EXPECT_EQ(callbackCalls, 0);
}

TEST(ModuleEntryUnit, RejectsProcessAttachWithoutACallback) {
  const HMODULE kSelf{GetModuleHandleW(/*lpModuleName = */ nullptr)};
  ASSERT_NE(kSelf, nullptr);
  SetLastError(/*dwErrCode = */ ERROR_SUCCESS);

  EXPECT_EQ(rgf::module::DllEntry(kSelf, DLL_PROCESS_ATTACH, nullptr), FALSE);
  EXPECT_EQ(GetLastError(), static_cast<DWORD>(ERROR_INVALID_PARAMETER));
}
