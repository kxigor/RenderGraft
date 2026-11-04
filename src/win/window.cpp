#include <bit>
#include <rgf/core/cast.hpp>
#include <rgf/win/error.hpp>
#include <rgf/win/window.hpp>

namespace rgf::win {

namespace {

struct WindowSearch {
  explicit WindowSearch(DWORD processId) noexcept : kProcessId_{processId} {}

  WindowSearch(const WindowSearch&) = delete;
  WindowSearch& operator=(const WindowSearch&) = delete;
  WindowSearch(WindowSearch&&) = delete;
  WindowSearch& operator=(WindowSearch&&) = delete;

  ~WindowSearch() = default;

  const DWORD kProcessId_;
  HWND window{nullptr};
  std::error_code error;
};

BOOL CALLBACK SelectWindow(HWND window, LPARAM parameter) {
  if (IsWindowVisible(/*hWnd = */ window) == FALSE) {
    return TRUE;
  }

  auto& search{*std::bit_cast<WindowSearch*>(parameter)};
  DWORD processId{};
  if (GetWindowThreadProcessId(/*hWnd = */ window,
                               /*lpdwProcessId = */ &processId) == 0) {
    const std::error_code kError{LastError()};
    if (kError.value() == ERROR_INVALID_WINDOW_HANDLE) {
      return TRUE;
    }
    search.error = kError;
    return FALSE;
  }
  if (processId != search.kProcessId_) {
    return TRUE;
  }

  SetLastError(/*dwErrCode = */ ERROR_SUCCESS);
  const HWND__* const kOwner{
      GetWindow(/*hWnd = */ window, /*uCmd = */ GW_OWNER)};
  if (kOwner != nullptr) {
    return TRUE;
  }
  const std::error_code kError{LastError()};
  if (kError.value() == ERROR_INVALID_WINDOW_HANDLE) {
    return TRUE;
  }
  if (kError) {
    search.error = kError;
    return FALSE;
  }
  search.window = window;
  return FALSE;
}

}  // namespace

std::expected<HWND, std::error_code> FindVisibleUnownedWindow(
    DWORD processId) noexcept {
  WindowSearch search{processId};
  SetLastError(/*dwErrCode = */ ERROR_SUCCESS);
  const BOOL kEnumerated{
      EnumWindows(/*lpEnumFunc = */ &SelectWindow,
                  /*lParam = */ core::PointerCast<LPARAM>(&search))};
  if (search.error) {
    return std::unexpected{search.error};
  }
  if (search.window != nullptr) {
    return search.window;
  }
  if (kEnumerated == FALSE) {
    return std::unexpected{LastError()};
  }
  return std::unexpected{
      std::error_code{ERROR_NOT_FOUND, std::system_category()}};
}

std::expected<DWORD, std::error_code> WindowThreadId(HWND window) noexcept {
  const DWORD kThreadId{GetWindowThreadProcessId(
      /*hWnd = */ window, /*lpdwProcessId = */ nullptr)};
  if (kThreadId == 0) {
    return std::unexpected{LastError()};
  }
  return kThreadId;
}

}  // namespace rgf::win
