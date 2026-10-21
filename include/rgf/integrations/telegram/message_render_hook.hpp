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
#include <rgf/integrations/telegram/message_info.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_canvas.hpp>
#include <system_error>

namespace rgf::integrations::telegram {

class MessageRenderHook {
 public:
  using MaskCallback =
      std::move_only_function<bool(const MessageInfo&, const qt::RasterCanvas&,
                                   const qt::internal::RectF&) const noexcept>;

  [[nodiscard]] static std::expected<std::unique_ptr<MessageRenderHook>,
                                     std::error_code>
  Create(hook::Vtable messageTable,
         qt::internal::GetPaintEngineFn getPaintEngine, void** rasterTable,
         DWORD guiThreadId, MaskCallback mask);

  MessageRenderHook(const MessageRenderHook&) = delete;
  MessageRenderHook& operator=(const MessageRenderHook&) = delete;
  MessageRenderHook(MessageRenderHook&&) = delete;
  MessageRenderHook& operator=(MessageRenderHook&&) = delete;

  ~MessageRenderHook();

  [[nodiscard]] std::expected<void, std::error_code> Enable() noexcept;
  [[nodiscard]] std::expected<void, std::error_code> Disable() noexcept;

  [[nodiscard]] bool IsDrawing() const noexcept;
  [[nodiscard]] bool IsDetached() const noexcept;
  [[nodiscard]] bool CanRelease() const noexcept;
  [[nodiscard]] std::uint64_t MessageCalls() const noexcept;
  [[nodiscard]] std::uint64_t TextCalls() const noexcept;
  [[nodiscard]] std::uint64_t ImageCalls() const noexcept;
  [[nodiscard]] std::uint64_t MaskedTextCalls() const noexcept;
  [[nodiscard]] std::uint64_t MaskedImageCalls() const noexcept;
  [[nodiscard]] std::uint64_t MetadataFailures() const noexcept;
  [[nodiscard]] std::error_code MetadataError() const noexcept;

 private:
  MessageRenderHook(hook::VmtHook messageDrawHook,
                    qt::internal::GetPaintEngineFn getPaintEngine,
                    void** rasterTable, DWORD guiThreadId,
                    MaskCallback mask) noexcept;

  [[nodiscard]] static std::expected<hook::VmtHook, std::error_code>
  PrepareMessageDrawHook(hook::Vtable messageTable) noexcept;
  [[nodiscard]] static MessageRenderHook& RequireCallbackTarget() noexcept;

  static void MessageDrawDetour(const internal::Message* message,
                                qt::internal::Painter& painter,
                                const internal::PaintContext& context) noexcept;
  void DrawMessage(const internal::Message* message,
                   qt::internal::Painter& painter,
                   const internal::PaintContext& context) noexcept;
  static void OnTextDrawn(const qt::RasterCanvas& canvas,
                          const qt::internal::RectF& bounds) noexcept;
  [[nodiscard]] static bool OnImageDraw(
      const qt::RasterCanvas& canvas,
      const qt::internal::RectF& bounds) noexcept;
  [[nodiscard]] bool TryMask(const qt::RasterCanvas& canvas,
                             const qt::internal::RectF& bounds) const noexcept;

  hook::VmtHook messageDrawHook_;
  const internal::MessageDrawFn kOriginalMessageDraw_;
  const qt::internal::GetPaintEngineFn kGetPaintEngine_;
  void** const kRasterTable_;
  const DWORD kGuiThreadId_;
  const MaskCallback kMask_;
  std::atomic<bool> isAttached_{false};
  unsigned int drawDepth_{};
  std::uint64_t messageCalls_{};
  std::uint64_t textCalls_{};
  std::uint64_t imageCalls_{};
  std::uint64_t maskedTextCalls_{};
  std::uint64_t maskedImageCalls_{};
  std::uint64_t metadataFailures_{};
  std::error_code metadataError_;
  const MessageInfo* messageInfo_{nullptr};
  std::atomic<bool> foreignThreadObserved_{false};
};

}  // namespace rgf::integrations::telegram
