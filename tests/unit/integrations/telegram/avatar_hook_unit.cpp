#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <rgf/core/cast.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/integrations/telegram/avatar_hook.hpp>
#include <rgf/integrations/telegram/internal/history.hpp>
#include <rgf/integrations/telegram/internal/userpic.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/win/remote_memory.hpp>
#include <span>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "internal/build.hpp"
#include "internal/peer.hpp"

namespace {

namespace qt = rgf::qt::internal;
namespace telegram = rgf::integrations::telegram::internal;
using rgf::integrations::telegram::AvatarHook;
using RasterTable = std::array<void*, qt::kRasterVirtualCount + 1>;
using DeviceTable = std::array<void*, qt::kPaintDeviceRedirectedSlot + 1>;

constexpr SIZE_T kPageSize{4096};
constexpr std::size_t kCodeSize{32};
constexpr std::size_t kJumpOffset{16};
constexpr std::size_t kContextPaddingSize{3};
constexpr std::uint8_t kNop{0x90};
constexpr std::uint8_t kMoveEax{0xb8};
constexpr std::uint8_t kRexW{0x48};
constexpr std::uint8_t kJumpPrefix{0xff};
constexpr std::uint8_t kJumpRegister{0xe0};
constexpr std::uint64_t kSelectedId{0x5678};
constexpr std::uint64_t kOtherId{0x1234};
constexpr std::int32_t kImageFlags{0x2468};
constexpr qt::RectF kBounds{
    .x = 12.0, .y = 24.0, .width = 48.0, .height = 48.0};
constexpr qt::RectF kSource{.x = 2.0, .y = 3.0, .width = 24.0, .height = 24.0};
constexpr qt::Image kPhotoImage{};
constexpr qt::Image kInitialsImage{};
constexpr qt::Color kColor{.spec = 1,
                           .alpha = 0xffff,
                           .red = 0xa0a0,
                           .green = 0x2020,
                           .blue = 0xf0f0,
                           .padding = 0};

enum class Event : unsigned char { Image, Text, Mask };

struct ProbeEngine : qt::PaintEngine {
  explicit ProbeEngine(void** table) : PaintEngine{table} {}

  void DrawImage(const qt::Image& image = kPhotoImage) {
    rgf::core::PointerCast<qt::DrawImageRectFn>(vtable[qt::kDrawImageRectSlot])(
        this, kBounds, image, kSource, kImageFlags);
  }

  void DrawInitials() {
    const qt::PointF kPosition{.x = kBounds.x, .y = kBounds.y};
    const qt::TextItem kItem{.descent = 128, .ascent = 512, .width = 1024};
    rgf::core::PointerCast<qt::DrawTextItemFn>(vtable[qt::kDrawTextItemSlot])(
        this, kPosition, kItem);
  }

  unsigned int imageCalls{};
  unsigned int textCalls{};
  const qt::RectF* rectangle{nullptr};
  const qt::Image* image{nullptr};
  const qt::RectF* source{nullptr};
  std::int32_t flags{};
  std::vector<Event> events;
};

[[nodiscard]] ProbeEngine& Engine(qt::PaintEngine* engine) {
  // Only ProbeEngine instances use this fixture's synthetic raster table.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  return *static_cast<ProbeEngine*>(engine);
}

void OriginalImage(qt::PaintEngine* engine, const qt::RectF& rectangle,
                   const qt::Image& image, const qt::RectF& source,
                   std::int32_t flags) {
  auto& probe{Engine(engine)};
  ++probe.imageCalls;
  probe.rectangle = &rectangle;
  probe.image = &image;
  probe.source = &source;
  probe.flags = flags;
  probe.events.push_back(Event::Image);
}

void OriginalText(qt::PaintEngine* engine, const qt::PointF& /*position*/,
                  const qt::TextItem& /*item*/) {
  auto& probe{Engine(engine)};
  ++probe.textCalls;
  probe.events.push_back(Event::Text);
}

void FillColor(qt::PaintEngine* engine, const qt::RectF& /*rectangle*/,
               const qt::Color& /*color*/) {
  Engine(engine).events.push_back(Event::Mask);
}

void UnusedVirtual() {}

[[nodiscard]] RasterTable MakeRasterTable() {
  RasterTable table{};
  table.fill(rgf::core::PointerCast<void*>(&UnusedVirtual));
  table[qt::kDrawImageRectSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalImage);
  table[qt::kDrawTextItemSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalText);
  table[qt::kFillColorSlot + 1] = rgf::core::PointerCast<void*>(&FillColor);
  return table;
}

struct ProbeDevice {
  qt::PaintDevice device;
  qt::PaintDevice* redirected{nullptr};
  qt::PaintEngine* engine{nullptr};
  mutable unsigned int redirectCalls{};
  mutable unsigned int engineCalls{};
};

[[nodiscard]] qt::PaintDevice* RedirectedDevice(const qt::PaintDevice* device,
                                                qt::Point* /*offset*/) {
  const auto* probe{rgf::core::PointerCast<const ProbeDevice*>(device)};
  ++probe->redirectCalls;
  return probe->redirected;
}

[[nodiscard]] qt::PaintEngine* DevicePaintEngine(
    const qt::PaintDevice* device) {
  const auto* probe{rgf::core::PointerCast<const ProbeDevice*>(device)};
  ++probe->engineCalls;
  return probe->engine;
}

[[nodiscard]] DeviceTable MakeDeviceTable() {
  DeviceTable table{};
  table.fill(rgf::core::PointerCast<void*>(&UnusedVirtual));
  table[qt::kPaintDeviceRedirectedSlot] =
      rgf::core::PointerCast<void*>(&RedirectedDevice);
  table[qt::kPaintDevicePaintEngineSlot] =
      rgf::core::PointerCast<void*>(&DevicePaintEngine);
  return table;
}

struct PeerRecord {
  std::uint64_t vtable{};
  std::uint64_t id{kSelectedId};
};

static_assert(offsetof(PeerRecord, id) == telegram::kPeerIdOffset);

struct PainterRecord {
  ProbeEngine* engine;
};

struct ContextRecord {
  qt::Point position{.x = 12, .y = 24};
  std::int32_t size{48};
  std::uint8_t shape{};
  std::array<std::byte, kContextPaddingSize> padding{};
};

struct ViewRecord {
  bool hasPhoto{true};
  ProbeEngine* cache{nullptr};
  unsigned int originalCalls{};
  const telegram::PeerData* receivedPeer{nullptr};
  qt::Painter* receivedPainter{nullptr};
  telegram::PeerUserpicView* receivedView{nullptr};
  telegram::PaintUserpicContext* receivedContext{nullptr};
  std::function<void()> duringPaint;
};

void OriginalPeerPaint(const telegram::PeerData* peer, qt::Painter& painter,
                       telegram::PeerUserpicView& view,
                       telegram::PaintUserpicContext& context) {
  auto& state{*rgf::core::PointerCast<ViewRecord*>(&view)};
  ++state.originalCalls;
  state.receivedPeer = peer;
  state.receivedPainter = &painter;
  state.receivedView = &view;
  state.receivedContext = &context;
  if (state.cache != nullptr) {
    if (state.hasPhoto) {
      state.cache->DrawImage();
    } else {
      state.cache->DrawInitials();
    }
  }
  if (state.duringPaint) {
    state.duringPaint();
  }
  auto& target{*rgf::core::PointerCast<PainterRecord*>(&painter)->engine};
  target.DrawImage(state.hasPhoto ? kPhotoImage : kInitialsImage);
}

[[nodiscard]] std::array<std::uint8_t, kCodeSize> EntryCode() {
  // A real executable NOP entry followed by a tail jump preserves both the
  // x64 register arguments and x86 stack arguments while exercising MinHook.
  std::array<std::uint8_t, kCodeSize> code{};
  code.fill(kNop);
  std::size_t offset{kJumpOffset};
  if constexpr (sizeof(void*) == sizeof(std::uint64_t)) {
    code.at(offset++) = kRexW;
  }
  code.at(offset++) = kMoveEax;
  const auto kBody{rgf::core::PointerCast<std::uintptr_t>(&OriginalPeerPaint)};
  std::memcpy(code.data() + offset, &kBody, sizeof(kBody));
  offset += sizeof(kBody);
  code.at(offset++) = kJumpPrefix;
  code.at(offset) = kJumpRegister;
  return code;
}

struct PaintState {
  unsigned int originalCalls{};
  unsigned int topicCalls{};
  std::function<void()> body;
};

struct ProbeWidget {
  void** vtable{nullptr};
  std::array<std::byte, qt::kWidgetPaintDeviceOffset - sizeof(void**)>
      primary{};
  ProbeDevice device;
  PaintState* paint{nullptr};
};

static_assert(offsetof(ProbeWidget, device) == qt::kWidgetPaintDeviceOffset);

void OriginalHistoryPaint(qt::Widget* widget, qt::PaintEvent* /*event*/) {
  auto& state{*rgf::core::PointerCast<ProbeWidget*>(widget)->paint};
  ++state.originalCalls;
  if (state.body) {
    state.body();
  }
}

void OriginalTopicPaint(qt::Widget* widget, qt::PaintEvent* event) {
  ++rgf::core::PointerCast<ProbeWidget*>(widget)->paint->topicCalls;
  OriginalHistoryPaint(widget, event);
}

class AvatarHookUnit : public ::testing::Test {
 public:
  void SetUp() override {
    AllocateHistoryTable(historyMemory, &OriginalHistoryPaint);
    table = rgf::core::PointerCast<void**>(historyMemory.Get());
    widget.vtable = table;
    AllocatePeerEntry();
    CreateHook(std::array{rgf::hook::Vtable{table}});
  }

  void CreateHook(std::span<const rgf::hook::Vtable> historyTables) {
    auto created{AvatarHook::Create(
        historyTables, Target(), rasterTable.data() + 1, GetCurrentThreadId(),
        [this](std::uint64_t owner, const rgf::qt::RasterCanvas& canvas,
               const qt::RectF& bounds) noexcept {
          observedOwners.push_back(owner);
          if (owner != kSelectedId) {
            return false;
          }
          canvas.FillRect(bounds, kColor);
          return true;
        })};
    ASSERT_TRUE(created.has_value()) << created.error().message();
    hook = std::move(*created);
  }

  void TearDown() override {
    if (hook && !hook->IsDetached()) {
      EXPECT_TRUE(hook->Disable().has_value());
    }
    hook.reset();
  }

  static void AllocateHistoryTable(rgf::win::RemoteMemory& allocation,
                                   telegram::HistoryPaintFn original) {
    void* page{VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE)};
    ASSERT_NE(page, nullptr);
    allocation.Reset(GetCurrentProcess(), page);
    auto** allocatedTable{static_cast<void**>(page)};
    allocatedTable[telegram::kHistoryPaintSlot] =
        rgf::core::PointerCast<void*>(original);
    DWORD previous{};
    ASSERT_NE(VirtualProtect(page, kPageSize, PAGE_READONLY, &previous), FALSE);
  }

  void BindTopicHistory() {
    hook.reset();
    AllocateHistoryTable(topicMemory, &OriginalTopicPaint);
    topicTable = rgf::core::PointerCast<void**>(topicMemory.Get());
    topicWidget.vtable = topicTable;
    CreateHook(
        std::array{rgf::hook::Vtable{table}, rgf::hook::Vtable{topicTable}});
  }

  void AllocatePeerEntry() {
    void* page{VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE)};
    ASSERT_NE(page, nullptr);
    peerMemory.Reset(GetCurrentProcess(), page);
    const auto kCode{EntryCode()};
    std::memcpy(page, kCode.data(), kCode.size());
    DWORD previous{};
    ASSERT_NE(VirtualProtect(page, kPageSize, PAGE_EXECUTE_READ, &previous),
              FALSE);
    ASSERT_NE(FlushInstructionCache(GetCurrentProcess(), page, kPageSize),
              FALSE);
  }

  [[nodiscard]] telegram::PaintPeerUserpicFn Target() const noexcept {
    return rgf::core::PointerCast<telegram::PaintPeerUserpicFn>(
        peerMemory.Get());
  }

  [[nodiscard]] void* Slot() const noexcept {
    return table[telegram::kHistoryPaintSlot];
  }

  [[nodiscard]] std::array<std::uint8_t, kCodeSize> EntryBytes() const {
    std::array<std::uint8_t, kCodeSize> bytes{};
    std::memcpy(bytes.data(), peerMemory.Get(), bytes.size());
    return bytes;
  }

  void ExpectOriginalBindings(
      const std::array<std::uint8_t, kCodeSize>& originalEntry) const {
    EXPECT_EQ(EntryBytes(), originalEntry);
    EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalHistoryPaint));
    EXPECT_TRUE(hook->CanRelease());
  }

  [[nodiscard]] qt::Painter& Painter() noexcept {
    return *rgf::core::PointerCast<qt::Painter*>(&painter);
  }

  void PaintHistory() {
    rgf::core::PointerCast<telegram::HistoryPaintFn>(Slot())(
        rgf::core::PointerCast<qt::Widget*>(&widget), nullptr);
  }

  void PaintTopic() {
    rgf::core::PointerCast<telegram::HistoryPaintFn>(
        topicTable[telegram::kHistoryPaintSlot])(
        rgf::core::PointerCast<qt::Widget*>(&topicWidget), nullptr);
  }

  void PaintPeer(const PeerRecord& peer, ViewRecord& targetView) {
    PaintPeer(rgf::core::PointerCast<const telegram::PeerData*>(&peer),
              targetView);
  }

  void PaintPeer(const telegram::PeerData* peer, ViewRecord& targetView) {
    Target()(peer, Painter(),
             *rgf::core::PointerCast<telegram::PeerUserpicView*>(&targetView),
             *rgf::core::PointerCast<telegram::PaintUserpicContext*>(&context));
  }

  void PrepareOneAvatar(const PeerRecord& peer) {
    paint.body = [this, &peer] { PaintPeer(peer, view); };
  }

  void PaintNested(const telegram::PeerData* nestedPeer,
                   ViewRecord& nestedView) {
    view.duringPaint = [this, nestedPeer, &nestedView] {
      engine.DrawImage();
      PaintPeer(nestedPeer, nestedView);
    };
    PrepareOneAvatar(selected);
    PaintHistory();
    view.duringPaint = {};
  }

  void PaintWithBackgroundImages() {
    paint.body = [this] {
      engine.DrawImage();
      PaintPeer(selected, view);
      engine.DrawImage();
    };
    PaintHistory();
  }

  [[nodiscard]] std::error_code TryDisableDuringPeerPaint() {
    std::error_code error;
    view.duringPaint = [this, &error] {
      EXPECT_TRUE(hook->IsDrawing());
      const auto kDisabled{hook->Disable()};
      EXPECT_FALSE(kDisabled.has_value());
      error = kDisabled ? std::error_code{} : kDisabled.error();
    };
    PaintPeer(selected, view);
    view.duringPaint = {};
    return error;
  }

  void ExpectOriginalArguments(const PeerRecord& peer) {
    EXPECT_EQ(view.receivedPeer,
              rgf::core::PointerCast<const telegram::PeerData*>(&peer));
    EXPECT_EQ(view.receivedPainter, &Painter());
    EXPECT_EQ(view.receivedView,
              rgf::core::PointerCast<telegram::PeerUserpicView*>(&view));
    EXPECT_EQ(view.receivedContext,
              rgf::core::PointerCast<telegram::PaintUserpicContext*>(&context));
  }

  [[noreturn]] void ExerciseForeignThread() {
    if (!hook->Enable()) {
      std::_Exit(1);
    }
    paint.body = [this] { PaintPeer(nullptr, view); };
    std::jthread worker{[this] { PaintHistory(); }};
    worker.join();
    if (view.originalCalls != 1 || engine.imageCalls != 1 ||
        widget.device.redirectCalls != 0 || backing.engineCalls != 0 ||
        hook->PaintCalls() != 0 || hook->ImageCalls() != 0 ||
        hook->OwnerFailures() != 0 || !observedOwners.empty() ||
        hook->CanRelease()) {
      std::_Exit(2);
    }
    if (!hook->Disable() || hook->CanRelease() || !hook->IsDetached() ||
        Slot() != rgf::core::PointerCast<void*>(&OriginalHistoryPaint)) {
      constexpr int kUnsafeRelease{3};
      std::_Exit(kUnsafeRelease);
    }
    std::_Exit(0);
  }

  rgf::win::RemoteMemory historyMemory;
  rgf::win::RemoteMemory topicMemory;
  rgf::win::RemoteMemory peerMemory;
  void** table{nullptr};
  void** topicTable{nullptr};
  RasterTable rasterTable{MakeRasterTable()};
  ProbeEngine engine{rasterTable.data() + 1};
  DeviceTable deviceTable{MakeDeviceTable()};
  ProbeDevice backing{.device = {.vtable = deviceTable.data()},
                      .engine = &engine};
  PaintState paint;
  ProbeWidget widget{.device = {.device = {.vtable = deviceTable.data()},
                                .redirected = &backing.device},
                     .paint = &paint};
  ProbeWidget topicWidget{.device = {.device = {.vtable = deviceTable.data()},
                                     .redirected = &backing.device},
                          .paint = &paint};
  PainterRecord painter{.engine = &engine};
  PeerRecord selected;
  PeerRecord other{.id = kOtherId};
  ContextRecord context;
  ViewRecord view;
  std::vector<std::uint64_t> observedOwners;
  std::unique_ptr<AvatarHook> hook;
};

}  // namespace

TEST_F(AvatarHookUnit, CreationLeavesBothOriginalBindingsUnchanged) {
  const auto kEntry{EntryBytes()};
  PrepareOneAvatar(selected);

  PaintHistory();

  EXPECT_EQ(EntryBytes(), kEntry);
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalHistoryPaint));
  EXPECT_EQ(view.originalCalls, 1);
  EXPECT_EQ(engine.imageCalls, 1);
  EXPECT_TRUE(observedOwners.empty());
  EXPECT_EQ(hook->PaintCalls(), 0);
  EXPECT_TRUE(hook->CanRelease());
}

TEST_F(AvatarHookUnit,
       MasksPhotosAndInitialsWithoutChangingTheirOffscreenCaches) {
  ProbeEngine cache{rasterTable.data() + 1};
  view.cache = &cache;
  PrepareOneAvatar(selected);
  ASSERT_TRUE(hook->Enable().has_value());

  PaintHistory();
  view.hasPhoto = false;
  PaintHistory();

  EXPECT_EQ(view.originalCalls, 2);
  EXPECT_EQ(cache.imageCalls, 1);
  EXPECT_EQ(cache.textCalls, 1);
  EXPECT_EQ(cache.vtable, rasterTable.data() + 1);
  EXPECT_EQ(engine.imageCalls, 0);
  EXPECT_EQ(engine.events, (std::vector{Event::Mask, Event::Mask}));
  EXPECT_EQ(hook->MaskedCalls(), 2);
  EXPECT_EQ(hook->OwnerFailures(), 0);
  EXPECT_EQ(hook->SkippedPaints(), 0);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(AvatarHookUnit, TopicsUseTheirOwnPaintOriginalAndTheSameOwnerFilter) {
  BindTopicHistory();
  PrepareOneAvatar(selected);
  ASSERT_TRUE(hook->Enable().has_value());

  PaintHistory();
  view.hasPhoto = false;
  PaintTopic();
  PrepareOneAvatar(other);
  PaintTopic();

  EXPECT_EQ(paint.originalCalls, 3);
  EXPECT_EQ(paint.topicCalls, 2);
  EXPECT_EQ(observedOwners, (std::vector{kSelectedId, kSelectedId, kOtherId}));
  EXPECT_EQ(hook->MaskedCalls(), 2);
  EXPECT_EQ(engine.imageCalls, 1);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(AvatarHookUnit, ForwardsOtherOwnersAndAllOriginalArguments) {
  PrepareOneAvatar(other);
  ASSERT_TRUE(hook->Enable().has_value());

  PaintHistory();

  ExpectOriginalArguments(other);
  EXPECT_EQ(observedOwners, (std::vector{kOtherId}));
  EXPECT_EQ(engine.imageCalls, 1);
  EXPECT_EQ(engine.rectangle, &kBounds);
  EXPECT_EQ(engine.image, &kPhotoImage);
  EXPECT_EQ(engine.source, &kSource);
  EXPECT_EQ(engine.flags, kImageFlags);
  EXPECT_EQ(hook->MaskedCalls(), 0);
}

TEST_F(AvatarHookUnit,
       LeavesNonAvatarHistoryImagesVisibleBeforeAndAfterAnAvatar) {
  ASSERT_TRUE(hook->Enable().has_value());

  PaintWithBackgroundImages();

  EXPECT_EQ(observedOwners, (std::vector{kSelectedId}));
  EXPECT_EQ(engine.events,
            (std::vector{Event::Image, Event::Mask, Event::Image}));
  EXPECT_EQ(engine.imageCalls, 2);
  EXPECT_EQ(hook->ImageCalls(), 1);
  EXPECT_EQ(hook->MaskedCalls(), 1);
}

TEST_F(AvatarHookUnit, LeavesAvatarsOutsideTheHistoryScopeVisible) {
  ASSERT_TRUE(hook->Enable().has_value());

  PaintPeer(selected, view);

  ExpectOriginalArguments(selected);
  EXPECT_EQ(view.originalCalls, 1);
  EXPECT_EQ(engine.imageCalls, 1);
  EXPECT_TRUE(observedOwners.empty());
  EXPECT_EQ(hook->PaintCalls(), 1);
  EXPECT_EQ(hook->ImageCalls(), 0);
}

TEST_F(AvatarHookUnit, RestoresTheSelectedOuterOwnerAfterANestedOtherOwner) {
  ViewRecord nestedView;
  ASSERT_TRUE(hook->Enable().has_value());

  PaintNested(rgf::core::PointerCast<const telegram::PeerData*>(&other),
              nestedView);

  EXPECT_EQ(observedOwners, (std::vector{kSelectedId, kOtherId, kSelectedId}));
  EXPECT_EQ(hook->PaintCalls(), 2);
  EXPECT_EQ(hook->ImageCalls(), 3);
  EXPECT_EQ(hook->MaskedCalls(), 2);
  EXPECT_EQ(engine.imageCalls, 1);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
  EXPECT_FALSE(hook->IsDrawing());
}

TEST_F(AvatarHookUnit, MissingNestedPeerCannotBorrowTheOuterOwner) {
  ViewRecord nestedView;
  ASSERT_TRUE(hook->Enable().has_value());

  PaintNested(nullptr, nestedView);

  EXPECT_EQ(observedOwners, (std::vector{kSelectedId, kSelectedId}));
  EXPECT_EQ(hook->OwnerFailures(), 1);
  EXPECT_EQ(hook->OwnerError(), std::make_error_code(std::errc::bad_address));
  EXPECT_EQ(hook->MaskedCalls(), 2);
  EXPECT_EQ(engine.imageCalls, 1);
  EXPECT_EQ(nestedView.originalCalls, 1);
}

TEST_F(AvatarHookUnit, MissingHistoryRedirectionKeepsTheOriginalAvatarVisible) {
  widget.device.redirected = nullptr;
  PrepareOneAvatar(selected);
  ASSERT_TRUE(hook->Enable().has_value());

  PaintHistory();

  EXPECT_EQ(view.originalCalls, 1);
  EXPECT_EQ(engine.imageCalls, 1);
  EXPECT_EQ(hook->PaintCalls(), 1);
  EXPECT_EQ(hook->SkippedPaints(), 1);
  EXPECT_EQ(hook->PaintError(), std::make_error_code(std::errc::not_supported));
  EXPECT_TRUE(observedOwners.empty());
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(AvatarHookUnit, ForeignControlThreadCannotChangeTheBindings) {
  const auto kOriginalEntry{EntryBytes()};
  std::expected<void, std::error_code> enabled;
  std::expected<void, std::error_code> disabled;
  std::jthread worker{[this, &enabled, &disabled] {
    enabled = hook->Enable();
    disabled = hook->Disable();
  }};
  worker.join();

  const std::error_code kWrongThread{ERROR_INVALID_THREAD_ID,
                                     std::system_category()};
  ASSERT_FALSE(enabled.has_value());
  ASSERT_FALSE(disabled.has_value());
  EXPECT_EQ(enabled.error(), kWrongThread);
  EXPECT_EQ(disabled.error(), kWrongThread);
  ExpectOriginalBindings(kOriginalEntry);
}

TEST_F(AvatarHookUnit, CannotDisableWhileTheInlineOwnerCallbackIsRunning) {
  ASSERT_TRUE(hook->Enable().has_value());

  const auto kError{TryDisableDuringPeerPaint()};

  const std::error_code kBusy{ERROR_BUSY, std::system_category()};
  EXPECT_EQ(kError, kBusy);
  EXPECT_FALSE(hook->IsDrawing());
  EXPECT_FALSE(hook->IsDetached());
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
}

TEST_F(AvatarHookUnit, DisablingRestoresInlineBytesAndTheHistoryVtable) {
  const auto kOriginalEntry{EntryBytes()};
  ASSERT_TRUE(hook->Enable().has_value());
  EXPECT_NE(EntryBytes(), kOriginalEntry);
  EXPECT_NE(Slot(), rgf::core::PointerCast<void*>(&OriginalHistoryPaint));

  ASSERT_TRUE(hook->Disable().has_value());

  EXPECT_EQ(EntryBytes(), kOriginalEntry);
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalHistoryPaint));
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_TRUE(hook->Disable().has_value());
}

TEST_F(AvatarHookUnit, DisablingRestoresBothHistoryBindingsAndInlineEntry) {
  BindTopicHistory();
  const auto kOriginalEntry{EntryBytes()};
  ASSERT_TRUE(hook->Enable().has_value());
  EXPECT_NE(Slot(), rgf::core::PointerCast<void*>(&OriginalHistoryPaint));
  EXPECT_NE(topicTable[telegram::kHistoryPaintSlot],
            rgf::core::PointerCast<void*>(&OriginalTopicPaint));

  ASSERT_TRUE(hook->Disable().has_value());

  ExpectOriginalBindings(kOriginalEntry);
  EXPECT_EQ(topicTable[telegram::kHistoryPaintSlot],
            rgf::core::PointerCast<void*>(&OriginalTopicPaint));
}

TEST_F(AvatarHookUnit, RepeatedEnablePreservesTheActiveHookAndCanBeDisabled) {
  ASSERT_TRUE(hook->Enable().has_value());

  const auto kAgain{hook->Enable()};

  ASSERT_FALSE(kAgain.has_value());
  EXPECT_EQ(kAgain.error().message(), "MH_ERROR_ENABLED");
  EXPECT_FALSE(hook->IsDetached());
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
  ASSERT_TRUE(hook->Enable().has_value());
  ASSERT_TRUE(hook->Disable().has_value());
}

TEST_F(AvatarHookUnit, ForeignThreadSkipsOwnerAndQtReadsAndRequiresRetention) {
  // The child retains the owner after verifying the foreign-thread safety
  // latch.
  EXPECT_EXIT(ExerciseForeignThread(), ::testing::ExitedWithCode(0), "");
}
