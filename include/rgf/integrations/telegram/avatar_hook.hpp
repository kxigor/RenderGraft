#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <rgf/hook/inline_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/integrations/telegram/history_image_hook.hpp>
#include <rgf/integrations/telegram/internal/userpic.hpp>
#include <rgf/qt/raster_canvas.hpp>
#include <span>
#include <system_error>

namespace rgf::integrations::telegram {

class AvatarHook {
 public:
  using MaskCallback =
      std::move_only_function<bool(std::uint64_t, const qt::RasterCanvas&,
                                   const qt::internal::RectF&) const noexcept>;

  [[nodiscard]] static std::expected<std::unique_ptr<AvatarHook>,
                                     std::error_code>
  Create(std::span<const hook::Vtable> historyTables,
         internal::PaintPeerUserpicFn paintUserpic, void** rasterTable,
         DWORD guiThreadId, MaskCallback mask);

  AvatarHook(const AvatarHook&) = delete;
  AvatarHook& operator=(const AvatarHook&) = delete;
  AvatarHook(AvatarHook&&) = delete;
  AvatarHook& operator=(AvatarHook&&) = delete;

  ~AvatarHook();

  [[nodiscard]] std::expected<void, std::error_code> Enable() noexcept;
  [[nodiscard]] std::expected<void, std::error_code> Disable() noexcept;
  [[nodiscard]] bool IsDrawing() const noexcept;
  [[nodiscard]] bool IsDetached() const noexcept;
  [[nodiscard]] bool CanRelease() const noexcept;
  [[nodiscard]] DWORD GuiThreadId() const noexcept;
  [[nodiscard]] std::uint64_t PaintCalls() const noexcept;
  [[nodiscard]] std::uint64_t HistoryPaintCalls() const noexcept;
  [[nodiscard]] std::uint64_t ImageCalls() const noexcept;
  [[nodiscard]] std::uint64_t MaskedCalls() const noexcept;
  [[nodiscard]] std::uint64_t OwnerFailures() const noexcept;
  [[nodiscard]] std::error_code OwnerError() const noexcept;
  [[nodiscard]] std::uint64_t SkippedPaints() const noexcept;
  [[nodiscard]] std::error_code PaintError() const noexcept;

 private:
  AvatarHook(hook::InlineHook paintHook,
             std::unique_ptr<HistoryImageHook> historyHook, DWORD guiThreadId,
             MaskCallback mask) noexcept;

  [[nodiscard]] static AvatarHook& RequireCallbackTarget() noexcept;
  static void PaintUserpicDetour(
      const internal::PeerData* peer, qt::internal::Painter& painter,
      internal::PeerUserpicView& view,
      internal::PaintUserpicContext& context) noexcept;
  void PaintUserpic(const internal::PeerData* peer,
                    qt::internal::Painter& painter,
                    internal::PeerUserpicView& view,
                    internal::PaintUserpicContext& context) noexcept;
  [[nodiscard]] static bool OnImageDraw(
      const qt::RasterCanvas& canvas,
      const qt::internal::RectF& bounds) noexcept;

  hook::InlineHook paintHook_;
  const std::unique_ptr<HistoryImageHook> kHistoryHook_;
  const internal::PaintPeerUserpicFn kOriginalPaint_;
  const DWORD kGuiThreadId_;
  const MaskCallback kMask_;
  std::atomic<bool> peerAttached_{false};
  std::atomic<bool> foreignThreadObserved_{false};
  unsigned int drawDepth_{};
  std::optional<std::uint64_t> currentOwner_;
  std::uint64_t paintCalls_{};
  std::uint64_t imageCalls_{};
  std::uint64_t maskedCalls_{};
  std::uint64_t ownerFailures_{};
  std::error_code ownerError_;
};

}  // namespace rgf::integrations::telegram
