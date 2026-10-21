#include <exception>
#include <new>
#include <rgf/integrations/telegram/message_render_hook.hpp>
#include <rgf/qt/raster_image_hook.hpp>
#include <rgf/qt/raster_text_hook.hpp>
#include <tuple>
#include <utility>

#include "internal/build.hpp"

namespace rgf::integrations::telegram {

namespace {

[[nodiscard]] std::atomic<MessageRenderHook*>&
CallbackTargetStorage() noexcept {
  static std::atomic<MessageRenderHook*> target{nullptr};
  return target;
}

}  // namespace

std::expected<std::unique_ptr<MessageRenderHook>, std::error_code>
MessageRenderHook::Create(hook::Vtable messageTable,
                          qt::internal::GetPaintEngineFn getPaintEngine,
                          void** rasterTable, DWORD guiThreadId,
                          MaskCallback mask) {
  if (messageTable.Get() == nullptr || getPaintEngine == nullptr ||
      rasterTable == nullptr || guiThreadId == 0 || !mask) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  if (CallbackTargetStorage().load(std::memory_order_acquire) != nullptr) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  auto messageDrawHook{PrepareMessageDrawHook(messageTable)};
  if (!messageDrawHook) {
    return std::unexpected{messageDrawHook.error()};
  }
  std::unique_ptr<MessageRenderHook> renderHook{
      new (std::nothrow)
          MessageRenderHook{std::move(*messageDrawHook), getPaintEngine,
                            rasterTable, guiThreadId, std::move(mask)}};
  if (!renderHook) {
    return std::unexpected{std::make_error_code(std::errc::not_enough_memory)};
  }
  return renderHook;
}

MessageRenderHook::MessageRenderHook(
    hook::VmtHook messageDrawHook,
    qt::internal::GetPaintEngineFn getPaintEngine, void** rasterTable,
    DWORD guiThreadId, MaskCallback mask) noexcept
    : messageDrawHook_{std::move(messageDrawHook)},
      kOriginalMessageDraw_{
          messageDrawHook_.Original<internal::MessageDrawFn>()},
      kGetPaintEngine_{getPaintEngine},
      kRasterTable_{rasterTable},
      kGuiThreadId_{guiThreadId},
      kMask_{std::move(mask)} {}

MessageRenderHook::~MessageRenderHook() {
  if (!CanRelease()) {
    std::terminate();
  }
  auto* previousTarget{this};
  std::ignore = CallbackTargetStorage().compare_exchange_strong(
      previousTarget, nullptr, std::memory_order_acq_rel,
      std::memory_order_acquire);
}

std::expected<void, std::error_code> MessageRenderHook::Enable() noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  auto* previousTarget{static_cast<MessageRenderHook*>(nullptr)};
  if (!CallbackTargetStorage().compare_exchange_strong(
          previousTarget, this, std::memory_order_acq_rel,
          std::memory_order_acquire) &&
      previousTarget != this) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  const auto kEnabled{messageDrawHook_.Enable()};
  if (!kEnabled) {
    return std::unexpected{kEnabled.error()};
  }
  isAttached_.store(true, std::memory_order_release);
  return {};
}

std::expected<void, std::error_code> MessageRenderHook::Disable() noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  if (IsDrawing()) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  if (IsDetached()) {
    return {};
  }
  const auto kDisabled{messageDrawHook_.Disable()};
  if (!kDisabled) {
    return std::unexpected{kDisabled.error()};
  }
  isAttached_.store(false, std::memory_order_release);
  return {};
}

void MessageRenderHook::MessageDrawDetour(
    const internal::Message* message, qt::internal::Painter& painter,
    const internal::PaintContext& context) noexcept {
  RequireCallbackTarget().DrawMessage(message, painter, context);
}

void MessageRenderHook::DrawMessage(
    const internal::Message* message, qt::internal::Painter& painter,
    const internal::PaintContext& context) noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    foreignThreadObserved_.store(true, std::memory_order_relaxed);
    kOriginalMessageDraw_(message, painter, context);
    return;
  }
  ++drawDepth_;
  ++messageCalls_;
  const auto kInfo{MessageInfo::Read(*message)};
  const auto* kPreviousInfo{
      std::exchange(messageInfo_, kInfo ? &*kInfo : nullptr)};
  if (!kInfo) {
    ++metadataFailures_;
    metadataError_ = kInfo.error();
  }
  {
    const qt::RasterImageHook kImages{kGetPaintEngine_(&painter), kRasterTable_,
                                      &OnImageDraw};
    const qt::RasterTextHook kTextHook{painter, kGetPaintEngine_, kRasterTable_,
                                       &OnTextDrawn};
    kOriginalMessageDraw_(message, painter, context);
  }
  messageInfo_ = kPreviousInfo;
  --drawDepth_;
}

void MessageRenderHook::OnTextDrawn(
    const qt::RasterCanvas& canvas,
    const qt::internal::RectF& bounds) noexcept {
  auto& target{RequireCallbackTarget()};
  ++target.textCalls_;
  if (target.TryMask(canvas, bounds)) {
    ++target.maskedTextCalls_;
  }
}

bool MessageRenderHook::OnImageDraw(
    const qt::RasterCanvas& canvas,
    const qt::internal::RectF& bounds) noexcept {
  auto& target{RequireCallbackTarget()};
  ++target.imageCalls_;
  if (!target.TryMask(canvas, bounds)) {
    return false;
  }
  ++target.maskedImageCalls_;
  return true;
}

bool MessageRenderHook::TryMask(
    const qt::RasterCanvas& canvas,
    const qt::internal::RectF& bounds) const noexcept {
  return messageInfo_ != nullptr && kMask_(*messageInfo_, canvas, bounds);
}

bool MessageRenderHook::IsDrawing() const noexcept { return drawDepth_ != 0; }

bool MessageRenderHook::IsDetached() const noexcept {
  return !isAttached_.load(std::memory_order_acquire);
}

bool MessageRenderHook::CanRelease() const noexcept {
  return IsDetached() && !IsDrawing() &&
         !foreignThreadObserved_.load(std::memory_order_relaxed);
}

std::uint64_t MessageRenderHook::MessageCalls() const noexcept {
  return messageCalls_;
}

std::uint64_t MessageRenderHook::TextCalls() const noexcept {
  return textCalls_;
}

std::uint64_t MessageRenderHook::ImageCalls() const noexcept {
  return imageCalls_;
}

std::uint64_t MessageRenderHook::MaskedTextCalls() const noexcept {
  return maskedTextCalls_;
}

std::uint64_t MessageRenderHook::MaskedImageCalls() const noexcept {
  return maskedImageCalls_;
}

std::uint64_t MessageRenderHook::MetadataFailures() const noexcept {
  return metadataFailures_;
}

std::error_code MessageRenderHook::MetadataError() const noexcept {
  return metadataError_;
}

std::expected<hook::VmtHook, std::error_code>
MessageRenderHook::PrepareMessageDrawHook(hook::Vtable messageTable) noexcept {
  auto messageDrawHook{
      hook::VmtHook::Create(messageTable, internal::kMessageDrawSlot)};
  if (!messageDrawHook) {
    return std::unexpected{messageDrawHook.error()};
  }
  const auto kDetourSet{messageDrawHook->SetDetour(&MessageDrawDetour)};
  if (!kDetourSet) {
    return std::unexpected{kDetourSet.error()};
  }
  return messageDrawHook;
}

MessageRenderHook& MessageRenderHook::RequireCallbackTarget() noexcept {
  auto* target{CallbackTargetStorage().load(std::memory_order_acquire)};
  if (target == nullptr) {
    std::terminate();
  }
  return *target;
}

}  // namespace rgf::integrations::telegram
