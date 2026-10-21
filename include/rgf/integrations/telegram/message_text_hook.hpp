#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/integrations/telegram/internal/message.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_canvas.hpp>
#include <system_error>

namespace rgf::integrations::telegram {

class MessageTextHook {
 public:
  using TextCallback = std::move_only_function<void(
      const qt::RasterCanvas&, const qt::internal::RectF&) const noexcept>;

  [[nodiscard]] static std::expected<std::unique_ptr<MessageTextHook>,
                                     std::error_code>
  Create(hook::Vtable messageTable,
         qt::internal::GetPaintEngineFn getPaintEngine, void** rasterTable,
         DWORD guiThreadId, TextCallback onText);

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
  [[nodiscard]] std::uint64_t TextCalls() const noexcept;

 private:
  MessageTextHook(hook::VmtHook messageDrawHook,
                  qt::internal::GetPaintEngineFn getPaintEngine,
                  void** rasterTable, DWORD guiThreadId,
                  TextCallback onText) noexcept;

  [[nodiscard]] static std::expected<hook::VmtHook, std::error_code>
  PrepareMessageDrawHook(hook::Vtable messageTable) noexcept;
  [[nodiscard]] static MessageTextHook& RequireCallbackTarget() noexcept;

  static void MessageDrawDetour(const internal::Message* message,
                                qt::internal::Painter& painter,
                                const internal::PaintContext& context) noexcept;
  void DrawMessage(const internal::Message* message,
                   qt::internal::Painter& painter,
                   const internal::PaintContext& context) noexcept;
  static void OnTextDrawn(const qt::RasterCanvas& canvas,
                          const qt::internal::RectF& bounds) noexcept;

  hook::VmtHook messageDrawHook_;
  const internal::MessageDrawFn kOriginalMessageDraw_;
  const qt::internal::GetPaintEngineFn kGetPaintEngine_;
  void** const kRasterTable_;
  const DWORD kGuiThreadId_;
  const TextCallback kOnText_;
  std::atomic<bool> isAttached_{false};
  unsigned int drawDepth_{};
  std::uint64_t messageCalls_{};
  std::uint64_t textCalls_{};
  std::atomic<bool> foreignThreadObserved_{false};
};

}  // namespace rgf::integrations::telegram
