#include <gtest/gtest.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <expected>
#include <future>
#include <memory>
#include <rgf/win/error.hpp>
#include <rgf/win/window.hpp>
#include <rgf/win/window_dispatch.hpp>
#include <semaphore>
#include <system_error>
#include <thread>
#include <type_traits>

namespace {

constexpr PCWSTR kClassName{L"RgfWindowDispatchProbe"};
constexpr std::chrono::seconds kCallbackTimeout{5};

class DispatchWindow {
 public:
  DispatchWindow() {
    std::promise<std::expected<HWND, std::error_code>> ready;
    auto future{ready.get_future()};
    thread_ = std::jthread{[this, &ready] { Run(ready); }};
    const auto kWindow{future.get()};
    if (kWindow) {
      window_ = *kWindow;
    } else {
      error_ = kWindow.error();
    }
  }

  DispatchWindow(const DispatchWindow&) = delete;
  DispatchWindow& operator=(const DispatchWindow&) = delete;

  DispatchWindow(DispatchWindow&&) = delete;
  DispatchWindow& operator=(DispatchWindow&&) = delete;

  ~DispatchWindow() {
    if (window_ != nullptr) {
      EXPECT_NE(PostThreadMessageW(threadId_, WM_QUIT, 0, 0), FALSE);
    }
    thread_.join();
  }

  [[nodiscard]] HWND Get() const noexcept { return window_; }
  [[nodiscard]] DWORD ThreadId() const noexcept { return threadId_; }
  [[nodiscard]] std::error_code Error() const noexcept { return error_; }

 private:
  void Run(std::promise<std::expected<HWND, std::error_code>>& ready) {
    threadId_ = GetCurrentThreadId();
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &DefWindowProcW;
    description.hInstance = GetModuleHandleW(nullptr);
    description.lpszClassName = kClassName;
    if (RegisterClassExW(&description) == 0) {
      ready.set_value(std::unexpected{rgf::win::LastError()});
      return;
    }

    HWND__* const kWindow{CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0,
                                          HWND_MESSAGE, nullptr,
                                          description.hInstance, nullptr)};
    if (kWindow == nullptr) {
      const std::error_code kError{rgf::win::LastError()};
      EXPECT_NE(UnregisterClassW(kClassName, description.hInstance), FALSE);
      ready.set_value(std::unexpected{kError});
      return;
    }

    ready.set_value(kWindow);
    MSG message{};
    BOOL result{};
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    EXPECT_NE(result, -1);
    EXPECT_NE(DestroyWindow(kWindow), FALSE);
    EXPECT_NE(UnregisterClassW(kClassName, description.hInstance), FALSE);
  }

  std::jthread thread_;
  HWND window_{nullptr};
  DWORD threadId_{0};
  std::error_code error_;
};

struct CallbackProbe {
  std::atomic<int> calls{0};
  std::atomic<DWORD> threadId{0};
  std::atomic<bool> completed{false};
  std::binary_semaphore entered{0};
  std::binary_semaphore finish{0};
};

[[nodiscard]] CallbackProbe& Probe() noexcept {
  static CallbackProbe probe;
  return probe;
}

[[nodiscard]] std::error_code RecordCallback() noexcept {
  Probe().threadId.store(GetCurrentThreadId());
  Probe().calls.fetch_add(1);
  Probe().completed.store(true);
  return {};
}

[[nodiscard]] std::error_code BusyOnceCallback() noexcept {
  Probe().threadId.store(GetCurrentThreadId());
  if (Probe().calls.fetch_add(1) == 0) {
    return {ERROR_BUSY, std::system_category()};
  }
  Probe().completed.store(true);
  return {};
}

[[nodiscard]] std::error_code ErrorCallback() noexcept {
  Probe().calls.fetch_add(1);
  return std::make_error_code(std::errc::permission_denied);
}

[[nodiscard]] std::error_code BlockingCallback() noexcept {
  Probe().threadId.store(GetCurrentThreadId());
  Probe().calls.fetch_add(1);
  Probe().entered.release();
  Probe().finish.acquire();
  Probe().completed.store(true);
  return {};
}

void ExpectConcurrentDispatchIsBusy(HWND window) {
  const auto kConcurrent{rgf::win::OnWindowThread(window, &RecordCallback)};
  const std::error_code kExpected{ERROR_BUSY, std::system_category()};
  ASSERT_FALSE(kConcurrent.has_value());
  EXPECT_EQ(kConcurrent.error(), kExpected);
  EXPECT_FALSE(Probe().completed.load());
  EXPECT_EQ(Probe().calls.load(), 1);
}

class WindowDispatchUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_FALSE(window_.Error());
    ASSERT_NE(window_.Get(), nullptr);
    Probe().calls.store(0);
    Probe().threadId.store(0);
    Probe().completed.store(false);
  }

  [[nodiscard]] const DispatchWindow& Window() const noexcept {
    return window_;
  }

 private:
  DispatchWindow window_;
};

}  // namespace

TEST_F(WindowDispatchUnit, ReportsTheWindowOwnersThreadInsteadOfTheCaller) {
  const auto kThread{rgf::win::WindowThreadId(Window().Get())};

  ASSERT_TRUE(kThread.has_value()) << kThread.error().message();
  EXPECT_EQ(*kThread, Window().ThreadId());
  EXPECT_NE(*kThread, GetCurrentThreadId());
}

TEST_F(WindowDispatchUnit,
       ExecutesOnTheWindowThreadAndCompletesBeforeReturning) {
  const auto kResult{rgf::win::OnWindowThread(Window().Get(), &RecordCallback)};

  ASSERT_TRUE(kResult.has_value()) << kResult.error().message();
  EXPECT_EQ(Probe().calls.load(), 1);
  EXPECT_EQ(Probe().threadId.load(), Window().ThreadId());
  EXPECT_NE(Probe().threadId.load(), GetCurrentThreadId());
  EXPECT_TRUE(Probe().completed.load());
}

TEST_F(WindowDispatchUnit, CanDispatchAgainAfterThePreviousHookWasRemoved) {
  ASSERT_TRUE(rgf::win::OnWindowThread(Window().Get(), &RecordCallback));
  ASSERT_TRUE(rgf::win::OnWindowThread(Window().Get(), &RecordCallback));

  EXPECT_EQ(Probe().calls.load(), 2);
  EXPECT_EQ(Probe().threadId.load(), Window().ThreadId());
}

TEST_F(WindowDispatchUnit,
       RetriesBusyWithoutReturningBeforeTheActionCompletes) {
  const auto kResult{
      rgf::win::OnWindowThread(Window().Get(), &BusyOnceCallback)};

  ASSERT_TRUE(kResult.has_value()) << kResult.error().message();
  EXPECT_EQ(Probe().calls.load(), 2);
  EXPECT_EQ(Probe().threadId.load(), Window().ThreadId());
  EXPECT_TRUE(Probe().completed.load());
}

TEST_F(WindowDispatchUnit, PreservesCallbackErrorsAndAllowsAnotherSession) {
  const auto kResult{rgf::win::OnWindowThread(Window().Get(), &ErrorCallback)};

  ASSERT_FALSE(kResult.has_value());
  EXPECT_EQ(kResult.error(),
            std::make_error_code(std::errc::permission_denied));
  EXPECT_EQ(Probe().calls.load(), 1);
  ASSERT_TRUE(rgf::win::OnWindowThread(Window().Get(), &RecordCallback));
  EXPECT_EQ(Probe().calls.load(), 2);
}

TEST_F(WindowDispatchUnit,
       RejectsConcurrentDispatchWhileTheWindowActionIsRunning) {
  std::expected<void, std::error_code> result;
  std::jthread worker{[this, &result] {
    result = rgf::win::OnWindowThread(Window().Get(), &BlockingCallback);
  }};
  const bool kEntered{Probe().entered.try_acquire_for(kCallbackTimeout)};
  EXPECT_TRUE(kEntered);
  if (kEntered) {
    ExpectConcurrentDispatchIsBusy(Window().Get());
  }
  Probe().finish.release();
  worker.join();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(Probe().completed.load());
}

TEST_F(WindowDispatchUnit,
       RejectsNullCallbacksAndInvalidWindowsBeforeInstalling) {
  const auto kCallback{rgf::win::OnWindowThread(Window().Get(), nullptr)};
  const std::error_code kExpected{ERROR_INVALID_PARAMETER,
                                  std::system_category()};
  ASSERT_FALSE(kCallback.has_value());
  EXPECT_EQ(kCallback.error(), kExpected);

  const auto kWindow{rgf::win::OnWindowThread(nullptr, &RecordCallback)};
  ASSERT_FALSE(kWindow.has_value());
  EXPECT_NE(kWindow.error().value(), 0);
  EXPECT_EQ(Probe().calls.load(), 0);
  EXPECT_TRUE(rgf::win::OnWindowThread(Window().Get(), &RecordCallback));
}

TEST_F(WindowDispatchUnit, RejectsCallsFromTheTargetWindowThread) {
  using WindowOwner =
      std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>;
  const WindowOwner kOwnWindow{
      CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                      GetModuleHandleW(nullptr), nullptr),
      &DestroyWindow};
  ASSERT_NE(kOwnWindow.get(), nullptr);

  const auto kResult{
      rgf::win::OnWindowThread(kOwnWindow.get(), &RecordCallback)};
  const std::error_code kExpected{ERROR_INVALID_PARAMETER,
                                  std::system_category()};

  ASSERT_FALSE(kResult.has_value());
  EXPECT_EQ(kResult.error(), kExpected);
  EXPECT_EQ(Probe().calls.load(), 0);
  EXPECT_TRUE(rgf::win::OnWindowThread(Window().Get(), &RecordCallback));
}
