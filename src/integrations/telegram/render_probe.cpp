#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <expected>
#include <memory>
#include <new>
#include <rgf/integrations/telegram/history_image_hook.hpp>
#include <rgf/integrations/telegram/raster_marker.hpp>
#include <rgf/integrations/telegram/render_probe.hpp>
#include <rgf/integrations/telegram/render_target.hpp>
#include <rgf/win/window.hpp>
#include <rgf/win/window_dispatch.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

namespace rgf::integrations::telegram {

namespace {

class RenderProbe {
 public:
  RenderProbe(RenderTarget target, HWND window, DWORD guiThread,
              ReportCallback report,
              std::unique_ptr<HistoryImageHook> imageHook) noexcept
      : kTarget_{target},
        kWindow_{window},
        kGuiThread_{guiThread},
        kReport_{report},
        kImageHook_{std::move(imageHook)} {}

  RenderProbe(const RenderProbe&) = delete;
  RenderProbe& operator=(const RenderProbe&) = delete;
  RenderProbe(RenderProbe&&) = delete;
  RenderProbe& operator=(RenderProbe&&) = delete;

  ~RenderProbe() = default;

  [[nodiscard]] static std::expected<std::unique_ptr<RenderProbe>,
                                     std::error_code>
  Create(ReportCallback report);
  [[nodiscard]] std::expected<void, std::error_code> Start();
  [[nodiscard]] std::expected<void, std::error_code> Stop();
  [[nodiscard]] bool CanRelease() const noexcept;
  [[nodiscard]] bool DispatchCompleted() const noexcept;
  [[nodiscard]] const char* FailureStep() const noexcept;
  void Report() const noexcept;
  void ReportFailure(const std::error_code& error, const char* step,
                     const std::error_code& cleanupError, bool retained) const;

 private:
  [[nodiscard]] static std::atomic<RenderProbe*>& DispatchTarget() noexcept;
  [[nodiscard]] static RenderProbe& CurrentDispatch() noexcept;
  [[nodiscard]] std::expected<void, std::error_code> Dispatch(
      win::WindowCallback callback);
  [[nodiscard]] static std::error_code Install() noexcept;
  [[nodiscard]] static std::error_code Remove() noexcept;
  void RequestRepaint() noexcept;

  const RenderTarget kTarget_;
  HWND__* const kWindow_;
  const DWORD kGuiThread_;
  const ReportCallback kReport_;
  const std::unique_ptr<HistoryImageHook> kImageHook_;
  std::error_code dispatchError_;
  std::error_code hookError_;
  std::error_code repaintError_;
  bool dispatchCompleted_{true};
  bool repaintRequested_{false};
  bool starting_{false};
};

std::expected<std::unique_ptr<RenderProbe>, std::error_code>
RenderProbe::Create(ReportCallback report) {
  const auto kTarget{RenderTarget::Locate()};
  if (!kTarget) {
    return std::unexpected{kTarget.error()};
  }
  const auto kWindow{win::FindVisibleUnownedWindow(GetCurrentProcessId())};
  if (!kWindow) {
    return std::unexpected{kWindow.error()};
  }
  const auto kGuiThread{win::WindowThreadId(*kWindow)};
  if (!kGuiThread) {
    return std::unexpected{kGuiThread.error()};
  }
  try {
    auto imageHook{HistoryImageHook::Create(kTarget->HistoryTable(),
                                            kTarget->RasterTable(), *kGuiThread,
                                            &DrawRasterMarker)};
    if (!imageHook) {
      return std::unexpected{imageHook.error()};
    }
    return std::make_unique<RenderProbe>(*kTarget, *kWindow, *kGuiThread,
                                         report, std::move(*imageHook));
  } catch (const std::bad_alloc&) {
    return std::unexpected{std::make_error_code(std::errc::not_enough_memory)};
  }
}

std::atomic<RenderProbe*>& RenderProbe::DispatchTarget() noexcept {
  static std::atomic<RenderProbe*> target{nullptr};
  return target;
}

RenderProbe& RenderProbe::CurrentDispatch() noexcept {
  auto* probe{DispatchTarget().load(std::memory_order_acquire)};
  if (probe == nullptr) {
    std::terminate();
  }
  return *probe;
}

std::expected<void, std::error_code> RenderProbe::Dispatch(
    win::WindowCallback callback) {
  if (!dispatchCompleted_) {
    return std::unexpected{dispatchError_};
  }
  auto* previous{static_cast<RenderProbe*>(nullptr)};
  if (!DispatchTarget().compare_exchange_strong(previous, this,
                                                std::memory_order_acq_rel)) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  hookError_.clear();
  repaintError_.clear();
  repaintRequested_ = false;
  dispatchCompleted_ = false;
  const auto kDispatched{win::OnWindowThread(kWindow_, callback)};
  if (!kDispatched) {
    dispatchError_ = kDispatched.error();
    return std::unexpected{dispatchError_};
  }
  dispatchCompleted_ = true;
  DispatchTarget().store(nullptr, std::memory_order_release);
  if (hookError_) {
    return std::unexpected{hookError_};
  }
  if (repaintError_) {
    return std::unexpected{repaintError_};
  }
  return {};
}

std::expected<void, std::error_code> RenderProbe::Start() {
  starting_ = true;
  return Dispatch(&Install);
}

std::expected<void, std::error_code> RenderProbe::Stop() {
  starting_ = false;
  return Dispatch(&Remove);
}

std::error_code RenderProbe::Install() noexcept {
  auto& probe{CurrentDispatch()};
  const auto kInstalled{probe.kImageHook_->Enable()};
  if (!kInstalled) {
    probe.hookError_ = kInstalled.error();
    return {};
  }
  probe.RequestRepaint();
  return {};
}

std::error_code RenderProbe::Remove() noexcept {
  auto& probe{CurrentDispatch()};
  if (probe.kImageHook_->IsDrawing()) {
    return {ERROR_BUSY, std::system_category()};
  }
  const auto kRemoved{probe.kImageHook_->Disable()};
  if (!kRemoved) {
    probe.hookError_ = kRemoved.error();
  } else if (!probe.kImageHook_->CanRelease()) {
    probe.hookError_ = {ERROR_INVALID_THREAD_ID, std::system_category()};
  }
  if (probe.kImageHook_->IsDetached()) {
    probe.RequestRepaint();
  }
  return {};
}

void RenderProbe::RequestRepaint() noexcept {
  const auto kRefreshed{kTarget_.WidgetUpdates().Update(kWindow_)};
  if (!kRefreshed) {
    repaintError_ = kRefreshed.error();
    return;
  }
  repaintRequested_ = true;
}

bool RenderProbe::CanRelease() const noexcept {
  return dispatchCompleted_ && kImageHook_->CanRelease();
}

bool RenderProbe::DispatchCompleted() const noexcept {
  return dispatchCompleted_;
}

const char* RenderProbe::FailureStep() const noexcept {
  if (!dispatchCompleted_) {
    return starting_ ? "dispatching hook installation"
                     : "dispatching hook removal";
  }
  if (hookError_) {
    return starting_ ? "installing the rendering hook"
                     : "removing the rendering hook";
  }
  return starting_ ? "requesting the initial repaint"
                   : "requesting the final repaint";
}

void RenderProbe::Report() const noexcept {
  constexpr std::size_t kReportChars{384};
  std::array<char, kReportChars> text{};
  const auto kPaintError{kImageHook_->PaintError()};
  const int kLength{std::snprintf(
      text.data(), text.size(),
      "render_probe=completed\nprobe_content=history_images\n"
      "gui_thread_id=%lu\nhistory_paint_calls=%llu\n"
      "image_draw_calls=%llu\nskipped_paints=%llu\n"
      "paint_error=%d\npaint_error_category=%s\nhooks_removed=true\n"
      "repaint_requested=true\ndll_retained=false\n",
      kGuiThread_, static_cast<unsigned long long>(kImageHook_->PaintCalls()),
      static_cast<unsigned long long>(kImageHook_->ImageCalls()),
      static_cast<unsigned long long>(kImageHook_->SkippedPaints()),
      kPaintError.value(),
      kPaintError ? kPaintError.category().name() : "none")};
  if (kLength > 0 && static_cast<std::size_t>(kLength) < text.size()) {
    kReport_({text.data(), static_cast<std::size_t>(kLength)});
  }
}

void RenderProbe::ReportFailure(const std::error_code& error, const char* step,
                                const std::error_code& cleanupError,
                                bool retained) const {
  std::string text{
      std::string{"render_probe=failed\nstep="} + step +
      "\nreason=" + error.message() + "\nhooks_removed=" +
      (dispatchCompleted_ && kImageHook_->IsDetached() ? "true" : "false") +
      "\nrepaint_requested=" +
      (dispatchCompleted_ && repaintRequested_ ? "true" : "false") +
      "\ndll_retained=" + (retained ? "true" : "false") + "\n"};
  if (dispatchCompleted_ && repaintError_ && repaintError_ != error) {
    text += "repaint_reason=" + repaintError_.message() + "\n";
  }
  if (cleanupError) {
    text += "cleanup_reason=" + cleanupError.message() + "\n";
  }
  kReport_(text);
}

class RetainedSession {
 public:
  RetainedSession() = default;
  RetainedSession(const RetainedSession&) = delete;
  RetainedSession& operator=(const RetainedSession&) = delete;
  RetainedSession(RetainedSession&&) = delete;
  RetainedSession& operator=(RetainedSession&&) = delete;

  ~RetainedSession() { std::ignore = probe_.release(); }

  [[nodiscard]] RenderProbe* Get() const noexcept { return probe_.get(); }

  void Retain(std::unique_ptr<RenderProbe> probe) noexcept {
    if (probe_) {
      std::terminate();
    }
    probe_ = std::move(probe);
  }

 private:
  std::unique_ptr<RenderProbe> probe_;
};

[[nodiscard]] RetainedSession& RetainedProbe() noexcept {
  static RetainedSession session;
  return session;
}

void FinishFailure(std::unique_ptr<RenderProbe> probe,
                   const std::error_code& error, const char* step,
                   const std::error_code& cleanupError) {
  const bool kRetained{!probe->CanRelease()};
  auto* report{probe.get()};
  if (kRetained) {
    RetainedProbe().Retain(std::move(probe));
  }
  report->ReportFailure(error, step, cleanupError, kRetained);
  if (kRetained) {
    throw std::system_error{error, step};
  }
}

void RunProbe(std::unique_ptr<RenderProbe> probe, ReportCallback report) {
  try {
    const auto kStarted{probe->Start()};
    if (!kStarted) {
      const auto* kStep{probe->FailureStep()};
      std::error_code cleanupError;
      if (probe->DispatchCompleted() && !probe->CanRelease()) {
        const auto kStopped{probe->Stop()};
        if (!kStopped) {
          cleanupError = kStopped.error();
        }
      }
      FinishFailure(std::move(probe), kStarted.error(), kStep, cleanupError);
      return;
    }
    report(
        "render_probe=active\nprobe_content=history_images\nduration_ms="
        "15000\n");
    constexpr std::chrono::seconds kDuration{15};
    std::this_thread::sleep_for(kDuration);
    const auto kStopped{probe->Stop()};
    if (!kStopped) {
      const auto* kStep{probe->FailureStep()};
      FinishFailure(std::move(probe), kStopped.error(), kStep, {});
      return;
    }
    probe->Report();
  } catch (...) {
    if (probe && !probe->CanRelease()) {
      RetainedProbe().Retain(std::move(probe));
    }
    throw;
  }
}

}  // namespace

void RunRenderProbe(ReportCallback report) {
  if (report == nullptr) {
    throw std::invalid_argument{"a rendering report callback is required"};
  }
  if (RetainedProbe().Get() != nullptr) {
    throw std::system_error{std::error_code{ERROR_BUSY, std::system_category()},
                            "a rendering session is retained"};
  }
  auto probe{RenderProbe::Create(report)};
  if (!probe) {
    report(std::string{"render_probe=unavailable\nreason="} +
           probe.error().message() + "\n");
    return;
  }
  RunProbe(std::move(*probe), report);
}

}  // namespace rgf::integrations::telegram
