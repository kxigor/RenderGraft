#include "render_probe.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/qt/raster_text_hook.hpp>
#include <rgf/win/error.hpp>
#include <string>
#include <system_error>
#include <utility>

#include "diagnostics.hpp"
#include "gui_dispatch.hpp"
#include "internal/build.hpp"
#include "raster_marker.hpp"
#include "render_target.hpp"
#include "widget_refresh.hpp"

namespace rgf::telegram {

namespace {

struct Message;
struct PaintContext;

using MessageDraw = void (*)(const Message* message,
                             qt::internal::Painter& painter,
                             const PaintContext& context);

BOOL CALLBACK FindMainWindow(HWND window, LPARAM parameter) {
  DWORD process{};
  if (GetWindowThreadProcessId(/*hWnd = */ window,
                               /*lpdwProcessId = */ &process) != 0 &&
      process == GetCurrentProcessId() &&
      IsWindowVisible(/*hWnd = */ window) != FALSE &&
      GetWindow(/*hWnd = */ window, /*uCmd = */ GW_OWNER) == nullptr) {
    *std::bit_cast<HWND*>(parameter) = window;
    return FALSE;
  }
  return TRUE;
}

[[nodiscard]] std::expected<HWND, std::error_code> MainWindow() noexcept {
  HWND window{};
  SetLastError(/*dwErrCode = */ ERROR_SUCCESS);
  const BOOL kEnumerated{
      EnumWindows(/*lpEnumFunc = */ &FindMainWindow,
                  /*lParam = */ std::bit_cast<LPARAM>(&window))};
  if (window != nullptr) {
    return window;
  }
  if (kEnumerated == FALSE && GetLastError() != ERROR_SUCCESS) {
    return std::unexpected{win::LastError()};
  }
  return std::unexpected{
      std::error_code{ERROR_NOT_FOUND, std::system_category()}};
}

class RenderProbe {
 public:
  RenderProbe(RenderTarget target, HWND window, DWORD guiThread,
              std::unique_ptr<hook::VmtHook> messageHook) noexcept;

  RenderProbe(const RenderProbe&) = delete;
  RenderProbe& operator=(const RenderProbe&) = delete;
  RenderProbe(RenderProbe&&) = delete;
  RenderProbe& operator=(RenderProbe&&) = delete;

  ~RenderProbe() = default;

  static void Run();

 private:
  [[nodiscard]] static RenderProbe& Instance() noexcept;
  [[nodiscard]] static std::optional<RenderProbe>& Storage() noexcept;
  [[nodiscard]] static std::expected<RenderProbe*, std::error_code>
  PrepareSession();
  [[nodiscard]] static std::expected<std::unique_ptr<hook::VmtHook>,
                                     std::error_code>
  CreateMessageHook(const RenderTarget& target);
  [[nodiscard]] static std::error_code Install() noexcept;
  [[nodiscard]] static std::error_code Remove() noexcept;
  static void Draw(const Message* message, qt::internal::Painter& painter,
                   const PaintContext& context) noexcept;
  static void OnTextDrawn(const qt::RasterCanvas& canvas,
                          const qt::internal::RectF& bounds) noexcept;
  void Report() const noexcept;
  static void FailDispatch(const std::error_code& error, const char* step);

  const RenderTarget kTarget_;
  HWND__* const kWindow_;
  const DWORD kGuiThread_;
  const std::unique_ptr<hook::VmtHook> kMessageHook_;
  const MessageDraw kOriginal_;
  unsigned int drawDepth_{};
  std::uint64_t messageCalls_{};
  std::uint64_t qtTextCalls_{};
  std::uint64_t markerCalls_{};
  std::atomic<bool> unexpectedThread_{false};
};

RenderProbe::RenderProbe(RenderTarget target, HWND window, DWORD guiThread,
                         std::unique_ptr<hook::VmtHook> messageHook) noexcept
    : kTarget_{target},
      kWindow_{window},
      kGuiThread_{guiThread},
      kMessageHook_{std::move(messageHook)},
      kOriginal_{kMessageHook_->Original<MessageDraw>()} {}

std::optional<RenderProbe>& RenderProbe::Storage() noexcept {
  static std::optional<RenderProbe> probe;
  return probe;
}

RenderProbe& RenderProbe::Instance() noexcept {
  auto& storage{Storage()};
  if (!storage) {
    std::terminate();
  }
  return *storage;
}

std::expected<std::unique_ptr<hook::VmtHook>, std::error_code>
RenderProbe::CreateMessageHook(const RenderTarget& target) {
  auto hook{hook::VmtHook::CreateDynamic(target.MessageTable(),
                                         internal::kMessageDrawSlot)};
  if (!hook) {
    return std::unexpected{hook.error()};
  }
  const auto kDetour{(*hook)->SetDetour(&Draw)};
  if (!kDetour) {
    return std::unexpected{kDetour.error()};
  }
  return hook;
}

std::error_code RenderProbe::Install() noexcept {
  auto& probe{Instance()};
  const auto kInstalled{probe.kMessageHook_->Enable()};
  if (!kInstalled) {
    return kInstalled.error();
  }
  const auto kRefreshed{RefreshWindow(probe.kWindow_)};
  return kRefreshed ? std::error_code{} : kRefreshed.error();
}

std::error_code RenderProbe::Remove() noexcept {
  auto& probe{Instance()};
  if (probe.drawDepth_ != 0) {
    return {ERROR_BUSY, std::system_category()};
  }
  const auto kRemoved{probe.kMessageHook_->Disable()};
  if (!kRemoved) {
    return kRemoved.error();
  }
  if (probe.unexpectedThread_.load(std::memory_order_relaxed)) {
    return {ERROR_INVALID_THREAD_ID, std::system_category()};
  }
  const auto kRefreshed{RefreshWindow(probe.kWindow_)};
  return kRefreshed ? std::error_code{} : kRefreshed.error();
}

void RenderProbe::Draw(const Message* message, qt::internal::Painter& painter,
                       const PaintContext& context) noexcept {
  auto& probe{Instance()};
  if (GetCurrentThreadId() != probe.kGuiThread_) {
    probe.unexpectedThread_.store(true, std::memory_order_relaxed);
    probe.kOriginal_(message, painter, context);
    return;
  }
  ++probe.drawDepth_;
  ++probe.messageCalls_;
  {
    const qt::RasterTextHook kTextHook{painter, probe.kTarget_.PaintEngine(),
                                       probe.kTarget_.RasterTable(),
                                       &OnTextDrawn};
    probe.kOriginal_(message, painter, context);
  }
  --probe.drawDepth_;
}

void RenderProbe::OnTextDrawn(const qt::RasterCanvas& canvas,
                              const qt::internal::RectF& bounds) noexcept {
  auto& probe{Instance()};
  ++probe.qtTextCalls_;
  DrawRasterMarker(canvas, bounds);
  ++probe.markerCalls_;
}

void RenderProbe::Report() const noexcept {
  constexpr std::size_t kReportChars{256};
  std::array<char, kReportChars> text{};
  const int kLength{std::snprintf(
      text.data(), text.size(),
      "render_probe=completed\ngui_thread_id=%lu\nmessage_draw_calls=%llu\n"
      "qt_text_calls=%llu\nmarker_calls=%llu\nhooks_removed=true\n",
      kGuiThread_, static_cast<unsigned long long>(messageCalls_),
      static_cast<unsigned long long>(qtTextCalls_),
      static_cast<unsigned long long>(markerCalls_))};
  if (kLength > 0 && static_cast<std::size_t>(kLength) < text.size()) {
    RunDiagnostics({text.data(), static_cast<std::size_t>(kLength)});
  }
}

void RenderProbe::FailDispatch(const std::error_code& error, const char* step) {
  const std::string kText{std::string{"render_probe=failed\nstep="} + step +
                          "\nreason=" + error.message() +
                          "\ndll_retained=true\n"};
  RunDiagnostics(kText);
  throw std::system_error{error, step};
}

std::expected<RenderProbe*, std::error_code> RenderProbe::PrepareSession() {
  const auto kTarget{RenderTarget::Locate()};
  if (!kTarget) {
    return std::unexpected{kTarget.error()};
  }
  const auto kWindow{MainWindow()};
  if (!kWindow) {
    return std::unexpected{kWindow.error()};
  }
  const DWORD kGuiThread{GetWindowThreadProcessId(
      /*hWnd = */ *kWindow, /*lpdwProcessId = */ nullptr)};
  if (kGuiThread == 0) {
    return std::unexpected{win::LastError()};
  }
  auto messageHook{CreateMessageHook(*kTarget)};
  if (!messageHook) {
    return std::unexpected{messageHook.error()};
  }
  return &Storage().emplace(*kTarget, *kWindow, kGuiThread,
                            std::move(*messageHook));
}

void RenderProbe::Run() {
  const auto kProbe{PrepareSession()};
  if (!kProbe) {
    RunDiagnostics(std::string{"render_probe=unavailable\nreason="} +
                   kProbe.error().message() + "\n");
    return;
  }
  auto& probe{**kProbe};
  const auto kInstalled{OnGuiThread(probe.kWindow_, &Install)};
  if (!kInstalled) {
    FailDispatch(kInstalled.error(), "installing the rendering hook");
  }
  RunDiagnostics("render_probe=active\nduration_ms=15000\n");
  OutputDebugStringA("[rgf/telegram] render marker active for 15 seconds\n");
  constexpr DWORD kProbeDurationMs{15000};
  Sleep(/*dwMilliseconds = */ kProbeDurationMs);
  const auto kRemoved{OnGuiThread(probe.kWindow_, &Remove)};
  if (!kRemoved) {
    FailDispatch(kRemoved.error(), "removing the rendering hook");
  }
  probe.Report();
}

}  // namespace

void RunRenderProbe() { RenderProbe::Run(); }

}  // namespace rgf::telegram
