#include <gtest/gtest.h>
#include <windows.h>

#include <memory>
#include <rgf/win/error.hpp>
#include <rgf/win/window.hpp>
#include <system_error>
#include <type_traits>

namespace {

using WindowOwner =
    std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>;

[[nodiscard]] WindowOwner CreateProbeWindow(bool visible, HWND owner) {
  constexpr int kOffscreenCoordinate{-32000};
  const DWORD kStyle{static_cast<DWORD>(WS_POPUP) |
                     (visible ? static_cast<DWORD>(WS_VISIBLE) : DWORD{0})};
  return WindowOwner{
      CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
                      L"RenderGraft window lookup probe", kStyle,
                      kOffscreenCoordinate, kOffscreenCoordinate, 1, 1, owner,
                      nullptr, GetModuleHandleW(nullptr), nullptr),
      &DestroyWindow};
}

}  // namespace

TEST(WindowUnit, FindsTheVisibleUnownedWindowAmongHiddenAndOwnedWindows) {
  const WindowOwner kVisible{CreateProbeWindow(true, nullptr)};
  ASSERT_NE(kVisible.get(), nullptr) << rgf::win::LastError().message();
  const WindowOwner kHidden{CreateProbeWindow(false, nullptr)};
  ASSERT_NE(kHidden.get(), nullptr) << rgf::win::LastError().message();
  const WindowOwner kOwned{CreateProbeWindow(true, kVisible.get())};
  ASSERT_NE(kOwned.get(), nullptr) << rgf::win::LastError().message();

  const auto kWindow{rgf::win::FindVisibleUnownedWindow(GetCurrentProcessId())};

  ASSERT_TRUE(kWindow.has_value()) << kWindow.error().message();
  EXPECT_EQ(*kWindow, kVisible.get());
}

TEST(WindowUnit, HiddenWindowsDoNotSatisfyTheSearch) {
  const WindowOwner kHidden{CreateProbeWindow(false, nullptr)};
  ASSERT_NE(kHidden.get(), nullptr) << rgf::win::LastError().message();

  const auto kWindow{rgf::win::FindVisibleUnownedWindow(GetCurrentProcessId())};
  const std::error_code kExpected{ERROR_NOT_FOUND, std::system_category()};

  ASSERT_FALSE(kWindow.has_value());
  EXPECT_EQ(kWindow.error(), kExpected);
}

TEST(WindowUnit, VisibleOwnedWindowsDoNotSatisfyTheSearch) {
  const WindowOwner kHiddenOwner{CreateProbeWindow(false, nullptr)};
  ASSERT_NE(kHiddenOwner.get(), nullptr) << rgf::win::LastError().message();
  const WindowOwner kOwned{CreateProbeWindow(true, kHiddenOwner.get())};
  ASSERT_NE(kOwned.get(), nullptr) << rgf::win::LastError().message();
  ASSERT_NE(IsWindowVisible(kOwned.get()), FALSE);

  const auto kWindow{rgf::win::FindVisibleUnownedWindow(GetCurrentProcessId())};
  const std::error_code kExpected{ERROR_NOT_FOUND, std::system_category()};

  ASSERT_FALSE(kWindow.has_value());
  EXPECT_EQ(kWindow.error(), kExpected);
}

TEST(WindowUnit, DoesNotReturnAWindowFromAnotherProcess) {
  const WindowOwner kVisible{CreateProbeWindow(true, nullptr)};
  ASSERT_NE(kVisible.get(), nullptr) << rgf::win::LastError().message();

  const auto kWindow{rgf::win::FindVisibleUnownedWindow(0)};
  const std::error_code kExpected{ERROR_NOT_FOUND, std::system_category()};

  ASSERT_FALSE(kWindow.has_value());
  EXPECT_EQ(kWindow.error(), kExpected);
}

TEST(WindowUnit, ReportsTheCreatingThreadForAHiddenWindow) {
  const WindowOwner kHidden{CreateProbeWindow(false, nullptr)};
  ASSERT_NE(kHidden.get(), nullptr) << rgf::win::LastError().message();

  const auto kThread{rgf::win::WindowThreadId(kHidden.get())};

  ASSERT_TRUE(kThread.has_value()) << kThread.error().message();
  EXPECT_EQ(*kThread, GetCurrentThreadId());
}

TEST(WindowUnit, InvalidWindowPreservesTheWin32ThreadQueryError) {
  SetLastError(ERROR_SUCCESS);
  const auto kThread{rgf::win::WindowThreadId(nullptr)};
  const std::error_code kError{rgf::win::LastError()};

  ASSERT_FALSE(kThread.has_value());
  EXPECT_NE(kError.value(), 0);
  EXPECT_EQ(kThread.error(), kError);
  EXPECT_EQ(&kThread.error().category(), &std::system_category());
}
