#include <exception>
#include <new>
#include <rgf/core/cast.hpp>
#include <rgf/qt/raster_text_hook.hpp>
#include <rgf/telegram/message_text_hook.hpp>
#include <rgf/win/error.hpp>
#include <tuple>
#include <utility>

#include "internal/build.hpp"

namespace rgf::telegram {

namespace {

[[nodiscard]] std::expected<const void*, std::error_code> ReadSlot(
    void* const* slot) noexcept {
  const void* function{};
  SIZE_T read{};
  if (ReadProcessMemory(/*hProcess = */ GetCurrentProcess(),
                        /*lpBaseAddress = */ static_cast<LPCVOID>(slot),
                        /*lpBuffer = */ static_cast<LPVOID>(&function),
                        /*nSize = */ sizeof(function),
                        /*lpNumberOfBytesRead = */ &read) == FALSE) {
    return std::unexpected{win::LastError()};
  }
  if (read != sizeof(function)) {
    return std::unexpected{
        std::error_code{ERROR_PARTIAL_COPY, std::system_category()}};
  }
  return function;
}

}  // namespace

struct MessageTextHook::MessageBinding {
  hook::VmtHook hook;
  void** slot;
};

std::expected<std::unique_ptr<MessageTextHook>, std::error_code>
MessageTextHook::Create(hook::Vtable messageTable,
                        qt::internal::GetPaintEngine getter, void** rasterTable,
                        DWORD guiThread, TextCallback onText) {
  if (messageTable.Get() == nullptr || getter == nullptr ||
      rasterTable == nullptr || guiThread == 0 || !onText) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  if (Active().load(std::memory_order_acquire) != nullptr) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  auto messageHook{
      hook::VmtHook::Create(messageTable, internal::kMessageDrawSlot)};
  if (!messageHook) {
    return std::unexpected{messageHook.error()};
  }
  const auto kDetour{messageHook->SetDetour(&Draw)};
  if (!kDetour) {
    return std::unexpected{kDetour.error()};
  }
  std::unique_ptr<MessageTextHook> hook{new (std::nothrow) MessageTextHook{
      MessageBinding{.hook = std::move(*messageHook),
                     .slot = messageTable.Slot(internal::kMessageDrawSlot)},
      getter, rasterTable, guiThread, std::move(onText)}};
  if (!hook) {
    return std::unexpected{std::make_error_code(std::errc::not_enough_memory)};
  }
  return hook;
}

MessageTextHook::MessageTextHook(MessageBinding binding,
                                 qt::internal::GetPaintEngine getter,
                                 void** rasterTable, DWORD guiThread,
                                 TextCallback onText) noexcept
    : messageHook_{std::move(binding.hook)},
      kMessageSlot_{binding.slot},
      kOriginal_{messageHook_.Original<internal::MessageDraw>()},
      kGetPaintEngine_{getter},
      kRasterTable_{rasterTable},
      kGuiThread_{guiThread},
      kOnText_{std::move(onText)} {}

MessageTextHook::~MessageTextHook() {
  if (!CanRelease()) {
    std::terminate();
  }
  auto* expected{this};
  std::ignore = Active().compare_exchange_strong(
      expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire);
}

std::atomic<MessageTextHook*>& MessageTextHook::Active() noexcept {
  static std::atomic<MessageTextHook*> active{nullptr};
  return active;
}

MessageTextHook& MessageTextHook::Current() noexcept {
  auto* hook{Active().load(std::memory_order_acquire)};
  if (hook == nullptr) {
    std::terminate();
  }
  return *hook;
}

std::expected<void, std::error_code> MessageTextHook::InspectSlot() noexcept {
  attachment_.store(Attachment::Unknown, std::memory_order_release);
  const auto kFunction{ReadSlot(kMessageSlot_)};
  if (!kFunction) {
    return std::unexpected{kFunction.error()};
  }
  if (*kFunction == core::PointerCast<void*>(kOriginal_)) {
    attachment_.store(Attachment::Detached, std::memory_order_release);
    return {};
  }
  if (*kFunction == core::PointerCast<void*>(&Draw)) {
    attachment_.store(Attachment::Attached, std::memory_order_release);
    return {};
  }
  return std::unexpected{
      std::make_error_code(std::errc::state_not_recoverable)};
}

std::expected<void, std::error_code> MessageTextHook::Enable() noexcept {
  if (GetCurrentThreadId() != kGuiThread_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  auto* expected{static_cast<MessageTextHook*>(nullptr)};
  if (!Active().compare_exchange_strong(expected, this,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire) &&
      expected != this) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  attachment_.store(Attachment::Unknown, std::memory_order_release);
  const auto kEnabled{messageHook_.Enable()};
  const auto kInspected{InspectSlot()};
  if (!kEnabled) {
    return std::unexpected{kEnabled.error()};
  }
  if (!kInspected) {
    return std::unexpected{kInspected.error()};
  }
  if (attachment_.load(std::memory_order_acquire) != Attachment::Attached) {
    return std::unexpected{
        std::make_error_code(std::errc::state_not_recoverable)};
  }
  return {};
}

std::expected<void, std::error_code> MessageTextHook::Disable() noexcept {
  if (GetCurrentThreadId() != kGuiThread_) {
    return std::unexpected{
        std::error_code{ERROR_INVALID_THREAD_ID, std::system_category()}};
  }
  if (IsDrawing()) {
    return std::unexpected{std::error_code{ERROR_BUSY, std::system_category()}};
  }
  if (IsDetached()) {
    return {};
  }
  attachment_.store(Attachment::Unknown, std::memory_order_release);
  const auto kDisabled{messageHook_.Disable()};
  const auto kInspected{InspectSlot()};
  if (!kDisabled) {
    return std::unexpected{kDisabled.error()};
  }
  if (!kInspected) {
    return std::unexpected{kInspected.error()};
  }
  if (!IsDetached()) {
    return std::unexpected{
        std::make_error_code(std::errc::state_not_recoverable)};
  }
  return {};
}

bool MessageTextHook::IsDrawing() const noexcept { return drawDepth_ != 0; }

bool MessageTextHook::IsDetached() const noexcept {
  return attachment_.load(std::memory_order_acquire) == Attachment::Detached;
}

bool MessageTextHook::CanRelease() const noexcept {
  return IsDetached() && !IsDrawing() &&
         !unexpectedThread_.load(std::memory_order_relaxed);
}

std::uint64_t MessageTextHook::MessageCalls() const noexcept {
  return messageCalls_;
}

void MessageTextHook::Draw(const internal::Message* message,
                           qt::internal::Painter& painter,
                           const internal::PaintContext& context) noexcept {
  auto& hook{Current()};
  if (GetCurrentThreadId() != hook.kGuiThread_) {
    hook.unexpectedThread_.store(true, std::memory_order_relaxed);
    hook.kOriginal_(message, painter, context);
    return;
  }
  ++hook.drawDepth_;
  ++hook.messageCalls_;
  {
    const qt::RasterTextHook kTextHook{painter, hook.kGetPaintEngine_,
                                       hook.kRasterTable_, &OnTextDrawn};
    hook.kOriginal_(message, painter, context);
  }
  --hook.drawDepth_;
}

void MessageTextHook::OnTextDrawn(const qt::RasterCanvas& canvas,
                                  const qt::internal::RectF& bounds) noexcept {
  Current().kOnText_(canvas, bounds);
}

}  // namespace rgf::telegram
