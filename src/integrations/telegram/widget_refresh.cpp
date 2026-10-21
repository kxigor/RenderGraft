#include <cstdint>
#include <rgf/core/cast.hpp>
#include <rgf/integrations/telegram/render_target.hpp>
#include <rgf/integrations/telegram/widget_refresh.hpp>
#include <rgf/win/window.hpp>

namespace rgf::integrations::telegram {

std::expected<void, std::error_code> RefreshWindow(const RenderTarget& target,
                                                   HWND window) noexcept {
  const auto kGuiThread{win::WindowThreadId(window)};
  if (!kGuiThread) {
    return std::unexpected{kGuiThread.error()};
  }
  if (*kGuiThread != GetCurrentThreadId()) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  return target.WidgetUpdates().Update(
      core::PointerCast<std::uintptr_t>(window));
}

}  // namespace rgf::integrations::telegram
