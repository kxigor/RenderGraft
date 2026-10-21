#include <exception>
#include <new>
#include <rgf/integrations/telegram/message_text_hook.hpp>
#include <rgf/qt/raster_text_hook.hpp>
#include <tuple>
#include <utility>

#include "internal/build.hpp"

namespace rgf::integrations::telegram {

namespace {

[[nodiscard]] std::atomic<MessageTextHook*>& CallbackTargetStorage() noexcept {
  static std::atomic<MessageTextHook*> target{nullptr};
  return target;
}

}  // namespace

std::expected<std::unique_ptr<MessageTextHook>, std::error_code>
MessageTextHook::Create(hook::Vtable messageTable,
                        qt::internal::GetPaintEngineFn getPaintEngine,
                        void** rasterTable, DWORD guiThreadId,
                        TextCallback onText) {
  if (messageTable.Get() == nullptr || getPaintEngine == nullptr ||
      rasterTable == nullptr || guiThreadId == 0 || !onText) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  if (CallbackTargetStorage().load(std::memory_order_acquire) != nullptr) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  auto messageDrawHook{PrepareMessageDrawHook(messageTable)};
  if (!messageDrawHook) {
    return std::unexpected{messageDrawHook.error()};
  }
  std::unique_ptr<MessageTextHook> textHook{new (std::nothrow) MessageTextHook{
      std::move(*messageDrawHook), getPaintEngine, rasterTable, guiThreadId,
      std::move(onText)}};
  if (!textHook) {
    return std::unexpected{std::make_error_code(std::errc::not_enough_memory)};
  }
  return textHook;
}

MessageTextHook::MessageTextHook(hook::VmtHook messageDrawHook,
                                 qt::internal::GetPaintEngineFn getPaintEngine,
                                 void** rasterTable, DWORD guiThreadId,
                                 TextCallback onText) noexcept
    : messageDrawHook_{std::move(messageDrawHook)},
      kOriginalMessageDraw_{
          messageDrawHook_.Original<internal::MessageDrawFn>()},
      kGetPaintEngine_{getPaintEngine},
      kRasterTable_{rasterTable},
      kGuiThreadId_{guiThreadId},
      kOnText_{std::move(onText)} {}

MessageTextHook::~MessageTextHook() {
  if (!CanRelease()) {
    std::terminate();
  }
  auto* previousTarget{this};
  std::ignore = CallbackTargetStorage().compare_exchange_strong(
      previousTarget, nullptr, std::memory_order_acq_rel,
      std::memory_order_acquire);
}

std::expected<void, std::error_code> MessageTextHook::Enable() noexcept {
  if (GetCurrentThreadId() != kGuiThreadId_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  auto* previousTarget{static_cast<MessageTextHook*>(nullptr)};
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

std::expected<void, std::error_code> MessageTextHook::Disable() noexcept {
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

void MessageTextHook::MessageDrawDetour(
    const internal::Message* message, qt::internal::Painter& painter,
    const internal::PaintContext& context) noexcept {
  RequireCallbackTarget().DrawMessage(message, painter, context);
}

void MessageTextHook::DrawMessage(
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
    const qt::RasterTextHook kTextHook{painter, kGetPaintEngine_, kRasterTable_,
                                       &OnTextDrawn};
    kOriginalMessageDraw_(message, painter, context);
  }
  messageInfo_ = kPreviousInfo;
  --drawDepth_;
}

void MessageTextHook::OnTextDrawn(const qt::RasterCanvas& canvas,
                                  const qt::internal::RectF& bounds) noexcept {
  auto& target{RequireCallbackTarget()};
  ++target.textCalls_;
  if (target.messageInfo_ != nullptr) {
    target.kOnText_(*target.messageInfo_, canvas, bounds);
  }
}

bool MessageTextHook::IsDrawing() const noexcept { return drawDepth_ != 0; }

bool MessageTextHook::IsDetached() const noexcept {
  return !isAttached_.load(std::memory_order_acquire);
}

bool MessageTextHook::CanRelease() const noexcept {
  return IsDetached() && !IsDrawing() &&
         !foreignThreadObserved_.load(std::memory_order_relaxed);
}

std::uint64_t MessageTextHook::MessageCalls() const noexcept {
  return messageCalls_;
}

std::uint64_t MessageTextHook::TextCalls() const noexcept { return textCalls_; }

std::uint64_t MessageTextHook::MetadataFailures() const noexcept {
  return metadataFailures_;
}

std::error_code MessageTextHook::MetadataError() const noexcept {
  return metadataError_;
}

std::expected<hook::VmtHook, std::error_code>
MessageTextHook::PrepareMessageDrawHook(hook::Vtable messageTable) noexcept {
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

MessageTextHook& MessageTextHook::RequireCallbackTarget() noexcept {
  auto* target{CallbackTargetStorage().load(std::memory_order_acquire)};
  if (target == nullptr) {
    std::terminate();
  }
  return *target;
}

}  // namespace rgf::integrations::telegram
