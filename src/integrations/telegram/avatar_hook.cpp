#include <exception>
#include <new>
#include <rgf/core/cast.hpp>
#include <rgf/integrations/telegram/avatar_hook.hpp>
#include <tuple>
#include <utility>

#include "internal/peer.hpp"

namespace rgf::integrations::telegram {

namespace {

[[nodiscard]] std::atomic<AvatarHook*>& CallbackTargetStorage() noexcept {
  static std::atomic<AvatarHook*> target{nullptr};
  return target;
}

}  // namespace

std::expected<std::unique_ptr<AvatarHook>, std::error_code> AvatarHook::Create(
    std::span<const hook::Vtable> historyTables,
    internal::PaintPeerUserpicFn paintUserpic, void** rasterTable,
    DWORD guiThreadId, MaskCallback mask) {
  if (historyTables.empty() || paintUserpic == nullptr ||
      rasterTable == nullptr || guiThreadId == 0 || !mask) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  if (CallbackTargetStorage().load(std::memory_order_acquire) != nullptr) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  auto paintHook{hook::InlineHook::Create(paintUserpic, &PaintUserpicDetour)};
  if (!paintHook) {
    return std::unexpected{paintHook.error()};
  }
  auto historyHook{HistoryImageHook::Create(historyTables, rasterTable,
                                            guiThreadId, &OnImageDraw)};
  if (!historyHook) {
    return std::unexpected{historyHook.error()};
  }
  std::unique_ptr<AvatarHook> avatarHook{new (std::nothrow) AvatarHook{
      std::move(*paintHook), std::move(*historyHook), guiThreadId,
      std::move(mask)}};
  if (!avatarHook) {
    return std::unexpected{std::make_error_code(std::errc::not_enough_memory)};
  }
  return avatarHook;
}

AvatarHook::AvatarHook(hook::InlineHook paintHook,
                       std::unique_ptr<HistoryImageHook> historyHook,
                       DWORD guiThreadId, MaskCallback mask) noexcept
    : paintHook_{std::move(paintHook)},
      kHistoryHook_{std::move(historyHook)},
      kOriginalPaint_{paintHook_.Original<internal::PaintPeerUserpicFn>()},
      kGuiThreadId_{guiThreadId},
      kMask_{std::move(mask)} {}

AvatarHook::~AvatarHook() {
  if (!CanRelease()) {
    std::terminate();
  }
  auto* previousTarget{this};
  std::ignore = CallbackTargetStorage().compare_exchange_strong(
      previousTarget, nullptr, std::memory_order_acq_rel,
      std::memory_order_acquire);
}

std::expected<void, std::error_code> AvatarHook::Enable() noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  auto* previousTarget{static_cast<AvatarHook*>(nullptr)};
  if (!CallbackTargetStorage().compare_exchange_strong(
          previousTarget, this, std::memory_order_acq_rel,
          std::memory_order_acquire) &&
      previousTarget != this) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  const auto kPeerEnabled{paintHook_.Enable()};
  if (!kPeerEnabled) {
    return std::unexpected{kPeerEnabled.error()};
  }
  peerAttached_.store(true, std::memory_order_release);
  return kHistoryHook_->Enable();
}

std::expected<void, std::error_code> AvatarHook::Disable() noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  if (IsDrawing()) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  if (peerAttached_.load(std::memory_order_acquire)) {
    const auto kPeerDisabled{paintHook_.Disable()};
    if (!kPeerDisabled) {
      return std::unexpected{kPeerDisabled.error()};
    }
    peerAttached_.store(false, std::memory_order_release);
  }
  return kHistoryHook_->IsDetached() ? std::expected<void, std::error_code>{}
                                     : kHistoryHook_->Disable();
}

void AvatarHook::PaintUserpicDetour(
    const internal::PeerData* peer, qt::internal::Painter& painter,
    internal::PeerUserpicView& view,
    internal::PaintUserpicContext& context) noexcept {
  RequireCallbackTarget().PaintUserpic(peer, painter, view, context);
}

void AvatarHook::PaintUserpic(const internal::PeerData* peer,
                              qt::internal::Painter& painter,
                              internal::PeerUserpicView& view,
                              internal::PaintUserpicContext& context) noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    foreignThreadObserved_.store(true, std::memory_order_relaxed);
    kOriginalPaint_(peer, painter, view, context);
    return;
  }
  ++drawDepth_;
  ++paintCalls_;
  const auto kOwner{
      internal::ReadPeerId(core::PointerCast<std::uintptr_t>(peer))};
  const auto kPreviousOwner{std::exchange(
      currentOwner_, kOwner ? std::optional{*kOwner} : std::nullopt)};
  if (!kOwner) {
    ++ownerFailures_;
    ownerError_ = kOwner.error();
  }
  kOriginalPaint_(peer, painter, view, context);
  currentOwner_ = kPreviousOwner;
  --drawDepth_;
}

bool AvatarHook::OnImageDraw(const qt::RasterCanvas& canvas,
                             const qt::internal::RectF& bounds) noexcept {
  auto& target{RequireCallbackTarget()};
  if (!target.currentOwner_) {
    return false;
  }
  ++target.imageCalls_;
  if (!target.kMask_(*target.currentOwner_, canvas, bounds)) {
    return false;
  }
  ++target.maskedCalls_;
  return true;
}

AvatarHook& AvatarHook::RequireCallbackTarget() noexcept {
  auto* target{CallbackTargetStorage().load(std::memory_order_acquire)};
  if (target == nullptr) {
    std::terminate();
  }
  return *target;
}

bool AvatarHook::IsDrawing() const noexcept {
  return drawDepth_ != 0 || kHistoryHook_->IsDrawing();
}

bool AvatarHook::IsDetached() const noexcept {
  return !peerAttached_.load(std::memory_order_acquire) &&
         kHistoryHook_->IsDetached();
}

bool AvatarHook::CanRelease() const noexcept {
  return IsDetached() && !IsDrawing() && kHistoryHook_->CanRelease() &&
         !foreignThreadObserved_.load(std::memory_order_relaxed);
}

DWORD AvatarHook::GuiThreadId() const noexcept { return kGuiThreadId_; }
std::uint64_t AvatarHook::PaintCalls() const noexcept { return paintCalls_; }
std::uint64_t AvatarHook::HistoryPaintCalls() const noexcept {
  return kHistoryHook_->PaintCalls();
}
std::uint64_t AvatarHook::ImageCalls() const noexcept { return imageCalls_; }
std::uint64_t AvatarHook::MaskedCalls() const noexcept { return maskedCalls_; }
std::uint64_t AvatarHook::OwnerFailures() const noexcept {
  return ownerFailures_;
}
std::error_code AvatarHook::OwnerError() const noexcept { return ownerError_; }
std::uint64_t AvatarHook::SkippedPaints() const noexcept {
  return kHistoryHook_->SkippedPaints();
}
std::error_code AvatarHook::PaintError() const noexcept {
  return kHistoryHook_->PaintError();
}

}  // namespace rgf::integrations::telegram
