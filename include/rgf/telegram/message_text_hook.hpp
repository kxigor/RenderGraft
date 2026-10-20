#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_canvas.hpp>
#include <rgf/telegram/internal/message.hpp>
#include <system_error>

namespace rgf::telegram {

class MessageTextHook {
  struct MessageBinding;
  enum class Attachment : std::uint8_t { Detached, Attached, Unknown };

 public:
  using TextCallback = std::move_only_function<void(
      const qt::RasterCanvas&, const qt::internal::RectF&) const noexcept>;

  [[nodiscard]] static std::expected<std::unique_ptr<MessageTextHook>,
                                     std::error_code>
  Create(hook::Vtable messageTable, qt::internal::GetPaintEngine getter,
         void** rasterTable, DWORD guiThread, TextCallback onText);

  MessageTextHook(const MessageTextHook&) = delete;
  MessageTextHook& operator=(const MessageTextHook&) = delete;
  MessageTextHook(MessageTextHook&&) = delete;
  MessageTextHook& operator=(MessageTextHook&&) = delete;

  ~MessageTextHook();

  [[nodiscard]] std::expected<void, std::error_code> Enable() noexcept;
  [[nodiscard]] std::expected<void, std::error_code> Disable() noexcept;

  [[nodiscard]] bool IsDrawing() const noexcept;
  [[nodiscard]] bool IsDetached() const noexcept;
  [[nodiscard]] bool CanRelease() const noexcept;
  [[nodiscard]] std::uint64_t MessageCalls() const noexcept;

 private:
  MessageTextHook(MessageBinding binding, qt::internal::GetPaintEngine getter,
                  void** rasterTable, DWORD guiThread,
                  TextCallback onText) noexcept;

  [[nodiscard]] static std::atomic<MessageTextHook*>& Active() noexcept;
  [[nodiscard]] static MessageTextHook& Current() noexcept;
  [[nodiscard]] std::expected<void, std::error_code> InspectSlot() noexcept;

  static void Draw(const internal::Message* message,
                   qt::internal::Painter& painter,
                   const internal::PaintContext& context) noexcept;
  static void OnTextDrawn(const qt::RasterCanvas& canvas,
                          const qt::internal::RectF& bounds) noexcept;

  const std::unique_ptr<hook::VmtHook> kMessageHook_;
  void** const kMessageSlot_;
  const internal::MessageDraw kOriginal_;
  const qt::internal::GetPaintEngine kGetPaintEngine_;
  void** const kRasterTable_;
  const DWORD kGuiThread_;
  const TextCallback kOnText_;
  std::atomic<Attachment> attachment_{Attachment::Detached};
  unsigned int drawDepth_{};
  std::uint64_t messageCalls_{};
  std::atomic<bool> unexpectedThread_{false};
};

}  // namespace rgf::telegram
