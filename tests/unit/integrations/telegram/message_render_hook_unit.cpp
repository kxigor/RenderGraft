#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <memory>
#include <rgf/core/cast.hpp>
#include <rgf/hook/error.hpp>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/integrations/telegram/internal/message.hpp>
#include <rgf/integrations/telegram/message_info.hpp>
#include <rgf/integrations/telegram/message_render_hook.hpp>
#include <rgf/integrations/telegram/message_sender_filter.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "internal/build.hpp"
#include "internal/message_layout.hpp"

namespace rgf::qt::internal {

struct Painter {
  PaintEngine* engine{nullptr};
  mutable unsigned int engineLookups{};
};

}  // namespace rgf::qt::internal

namespace rgf::integrations::telegram::internal {

struct PaintContext {};

struct Message {
  std::array<std::byte, kMessageItemOffset> prefix{};
  std::uint64_t item{};
  mutable unsigned int originalCalls{};
  mutable qt::internal::Painter* receivedPainter{nullptr};
  mutable const PaintContext* receivedContext{nullptr};
  std::function<void()> duringDraw;
  bool drawsText{true};
};

}  // namespace rgf::integrations::telegram::internal

namespace {

namespace qt = rgf::qt::internal;
namespace telegram = rgf::integrations::telegram::internal;
using rgf::integrations::telegram::MessageInfo;
using rgf::integrations::telegram::MessageRenderHook;
using rgf::integrations::telegram::MessageSenderFilter;
using RasterTable = std::array<void*, qt::kRasterVirtualCount + 1>;

constexpr SIZE_T kPageSize{4096};
constexpr std::int64_t kMessageId{-1234};
constexpr std::int64_t kNestedMessageId{-5678};
constexpr std::uint64_t kDialogPeerId{0x2000000001234};
constexpr std::uint64_t kFromPeerId{0x5678};
constexpr qt::PointF kPosition{.x = 20.0, .y = 30.0};
constexpr qt::TextItem kItem{.descent = 224, .ascent = 848, .width = 5616};
constexpr qt::RectF kImageBounds{
    .x = 10.0, .y = 20.0, .width = 80.0, .height = 60.0};
constexpr qt::RectF kImageSource{
    .x = 2.0, .y = 3.0, .width = 160.0, .height = 120.0};
constexpr std::int32_t kImageFlags{0x1234};
constexpr qt::Color kColor{.spec = 1,
                           .alpha = 0xffff,
                           .red = 0x8080,
                           .green = 0,
                           .blue = 0xffff,
                           .padding = 0};

enum class Event : unsigned char {
  Message,
  Text,
  Image,
  Callback,
  Fill,
  Finished
};

[[nodiscard]] std::uint64_t Address(const void* object) {
  return rgf::core::PointerCast<std::uintptr_t>(object);
}

struct ProbePeer {
  std::uint64_t vtable{};
  std::uint64_t id{};
};

struct ProbeHistory {
  std::array<std::byte, telegram::kHistoryPeerOffset> prefix{};
  std::uint64_t peer{};
};

struct ProbeItem {
  std::uint64_t components{};
  telegram::MessageFields fields{};
};

static_assert(offsetof(ProbePeer, id) == telegram::kPeerIdOffset);
static_assert(offsetof(ProbeHistory, peer) == telegram::kHistoryPeerOffset);
static_assert(offsetof(ProbeItem, fields) == telegram::kItemIdOffset);

struct ProbeEngine : qt::PaintEngine {
  explicit ProbeEngine(void** table) : PaintEngine{table} {}

  void Draw() {
    rgf::core::PointerCast<qt::DrawTextItemFn>(vtable[qt::kDrawTextItemSlot])(
        this, kPosition, kItem);
  }

  void DrawImage(const qt::RectF& rectangle, const qt::Image& image,
                 const qt::RectF& source, std::int32_t flags) {
    rgf::core::PointerCast<qt::DrawImageRectFn>(vtable[qt::kDrawImageRectSlot])(
        this, rectangle, image, source, flags);
  }

  std::vector<Event> events;
  qt::RectF rectangle{};
  qt::Color color{};
  unsigned int imageCalls{};
  const qt::RectF* imageBounds{nullptr};
  const qt::Image* image{nullptr};
  const qt::RectF* imageSource{nullptr};
  std::int32_t imageFlags{};
};

[[nodiscard]] ProbeEngine& Engine(qt::PaintEngine* engine) {
  // Only ProbeEngine objects are passed through this fixture's Qt bindings.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  return *static_cast<ProbeEngine*>(engine);
}

void OriginalText(qt::PaintEngine* engine, const qt::PointF& /*position*/,
                  const qt::TextItem& /*item*/) {
  Engine(engine).events.push_back(Event::Text);
}

void OriginalImage(qt::PaintEngine* engine, const qt::RectF& rectangle,
                   const qt::Image& image, const qt::RectF& source,
                   std::int32_t flags) {
  auto& probe{Engine(engine)};
  ++probe.imageCalls;
  probe.imageBounds = &rectangle;
  probe.image = &image;
  probe.imageSource = &source;
  probe.imageFlags = flags;
  probe.events.push_back(Event::Image);
}

void FillColor(qt::PaintEngine* engine, const qt::RectF& rectangle,
               const qt::Color& color) {
  auto& probe{Engine(engine)};
  probe.rectangle = rectangle;
  probe.color = color;
  probe.events.push_back(Event::Fill);
}

void UnusedVirtual() {}

[[nodiscard]] RasterTable MakeRasterTable() {
  RasterTable table{};
  table.fill(rgf::core::PointerCast<void*>(&UnusedVirtual));
  table[qt::kDrawTextItemSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalText);
  table[qt::kFillColorSlot + 1] = rgf::core::PointerCast<void*>(&FillColor);
  table[qt::kDrawImageRectSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalImage);
  return table;
}

[[nodiscard]] qt::PaintEngine* GetPaintEngine(const qt::Painter* painter) {
  ++painter->engineLookups;
  return painter->engine;
}

void OriginalMessage(const telegram::Message* message, qt::Painter& painter,
                     const telegram::PaintContext& context) {
  ++message->originalCalls;
  message->receivedPainter = &painter;
  message->receivedContext = &context;
  auto& engine{Engine(painter.engine)};
  engine.events.push_back(Event::Message);
  if (message->duringDraw) {
    message->duringDraw();
  }
  if (message->drawsText) {
    engine.Draw();
  }
  engine.events.push_back(Event::Finished);
}

class MessageRenderHookUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    page = VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE);
    ASSERT_NE(page, nullptr);
    table = static_cast<void**>(page);
    table[telegram::kMessageDrawSlot] =
        rgf::core::PointerCast<void*>(&OriginalMessage);
    DWORD previous{};
    ASSERT_NE(VirtualProtect(page, kPageSize, PAGE_READONLY, &previous), FALSE);

    auto calls{std::make_unique<unsigned int>(0)};
    callbackCalls = calls.get();
    auto createdHook{MessageRenderHook::Create(
        rgf::hook::Vtable{table}, &GetPaintEngine, rasterTable.data() + 1,
        GetCurrentThreadId(),
        [this, calls = std::move(calls)](const MessageInfo& info,
                                         const rgf::qt::RasterCanvas& canvas,
                                         const qt::RectF& bounds) noexcept {
          ++*calls;
          observedMessages.push_back(info);
          EXPECT_GE(hook->TextCalls() + hook->ImageCalls(), *calls);
          engine.events.push_back(Event::Callback);
          canvas.FillRect(bounds, kColor);
          return true;
        })};
    ASSERT_TRUE(createdHook.has_value()) << createdHook.error().message();
    hook = std::move(*createdHook);
  }

  void TearDown() override {
    if (hook && Slot() != rgf::core::PointerCast<void*>(&OriginalMessage)) {
      EXPECT_TRUE(hook->Disable().has_value());
    }
    hook.reset();
    if (page != nullptr) {
      EXPECT_NE(VirtualFree(page, 0, MEM_RELEASE), FALSE);
    }
  }

  void Draw() { Draw(message); }

  void Draw(telegram::Message& renderedMessage) {
    rgf::core::PointerCast<telegram::MessageDrawFn>(Slot())(&renderedMessage,
                                                            painter, context);
  }

  [[nodiscard]] void* Slot() const { return table[telegram::kMessageDrawSlot]; }

  [[nodiscard]] std::expected<std::unique_ptr<MessageRenderHook>,
                              std::error_code>
  CreateAnotherHook() {
    return MessageRenderHook::Create(
        rgf::hook::Vtable{table}, &GetPaintEngine, rasterTable.data() + 1,
        GetCurrentThreadId(),
        [](const MessageInfo&, const rgf::qt::RasterCanvas&,
           const qt::RectF&) noexcept { return false; });
  }

  [[nodiscard]] std::expected<void, std::error_code> UseSenderFilter() {
    hook.reset();
    const auto kFilter{MessageSenderFilter::ForUser(kFromPeerId)};
    if (!kFilter) {
      return std::unexpected{kFilter.error()};
    }
    auto calls{std::make_unique<unsigned int>(0)};
    callbackCalls = calls.get();
    auto createdHook{MessageRenderHook::Create(
        rgf::hook::Vtable{table}, &GetPaintEngine, rasterTable.data() + 1,
        GetCurrentThreadId(),
        [this, filter = *kFilter, calls = std::move(calls)](
            const MessageInfo& info, const rgf::qt::RasterCanvas& canvas,
            const qt::RectF& bounds) noexcept {
          ++*calls;
          observedMessages.push_back(info);
          if (!filter.Matches(info)) {
            return false;
          }
          engine.events.push_back(Event::Callback);
          canvas.FillRect(bounds, kColor);
          return true;
        })};
    if (!createdHook) {
      return std::unexpected{createdHook.error()};
    }
    hook = std::move(*createdHook);
    return {};
  }

  void DrawImage(ProbeEngine& imageEngine) const {
    imageEngine.DrawImage(kImageBounds, image, kImageSource, kImageFlags);
  }

  void PrepareImageMessage() {
    message.drawsText = false;
    message.duringDraw = [this] { DrawImage(engine); };
  }

  void PrepareImageMessageWithOffscreenRendering(ProbeEngine& offscreen) {
    message.drawsText = false;
    message.duringDraw = [this, &offscreen] {
      DrawImage(offscreen);
      DrawImage(engine);
    };
  }

  void DrawWithNestedImages(telegram::Message& nested) {
    message.duringDraw = [this, &nested] {
      DrawImage(engine);
      Draw(nested);
      DrawImage(engine);
    };
    nested.duringDraw = [this] { DrawImage(engine); };
    Draw();
    message.duringDraw = {};
    nested.duringDraw = {};
  }

  void ExpectOriginalImageArguments(const ProbeEngine& imageEngine) const {
    EXPECT_EQ(imageEngine.imageBounds, &kImageBounds);
    EXPECT_EQ(imageEngine.image, &image);
    EXPECT_EQ(imageEngine.imageSource, &kImageSource);
    EXPECT_EQ(imageEngine.imageFlags, kImageFlags);
  }

  [[nodiscard]] std::vector<std::int64_t> CallbackMessageIds() const {
    std::vector<std::int64_t> result;
    result.reserve(observedMessages.size());
    for (const auto& info : observedMessages) {
      result.push_back(info.MessageId());
    }
    return result;
  }

  void DrawWithNestedMessage(telegram::Message& nested) {
    message.duringDraw = [this, &nested] {
      engine.Draw();
      Draw(nested);
    };
    Draw();
    message.duringDraw = {};
  }

  void DrawWhileChangingTheModel() {
    message.duringDraw = [this] {
      item.fields.messageId = kNestedMessageId;
      item.fields.flags = 0;
      fromPeer.id = kFromPeerId + 1;
    };
    Draw();
    message.duringDraw = {};
  }

  [[nodiscard]] static std::error_code ProtectionError() {
    return {ERROR_ACCESS_DENIED, std::system_category()};
  }

  [[nodiscard]] std::error_code DisableDuringDraw(const void* detour) {
    EXPECT_TRUE(hook->IsDrawing());
    const auto kRemoved{hook->Disable()};
    EXPECT_FALSE(kRemoved.has_value());
    EXPECT_EQ(Slot(), detour);
    EXPECT_FALSE(hook->CanRelease());
    return kRemoved ? std::error_code{} : kRemoved.error();
  }

  [[nodiscard]] std::error_code DrawNestedMessage() {
    const void* const kDetour{Slot()};
    bool enteredNestedDraw{false};
    std::error_code removalError;
    message.duringDraw = [this, &enteredNestedDraw, &removalError, kDetour] {
      if (enteredNestedDraw) {
        return;
      }
      enteredNestedDraw = true;
      Draw();
      removalError = DisableDuringDraw(kDetour);
    };
    Draw();
    message.duringDraw = {};
    return removalError;
  }

  [[nodiscard]] std::expected<void, std::error_code> DisableOnForeignThread() {
    std::expected<void, std::error_code> removed{};
    {
      const std::jthread kWorker{
          [this, &removed] { removed = hook->Disable(); }};
    }
    return removed;
  }

  [[noreturn]] void ExerciseForeignThread() {
    message.item = 0;
    if (!hook->Enable()) {
      std::_Exit(1);
    }
    std::jthread worker{[this] { Draw(); }};
    worker.join();
    if (message.originalCalls != 1 || *callbackCalls != 0 ||
        hook->MessageCalls() != 0 || hook->TextCalls() != 0 ||
        hook->MetadataFailures() != 0 || painter.engineLookups != 0 ||
        hook->CanRelease() || engine.vtable != rasterTable.data() + 1) {
      std::_Exit(2);
    }
    if (!hook->Disable() || hook->CanRelease() || !hook->IsDetached() ||
        Slot() != rgf::core::PointerCast<void*>(&OriginalMessage)) {
      constexpr int kUnsafeRelease{3};
      std::_Exit(kUnsafeRelease);
    }
    std::_Exit(0);
  }

 public:
  LPVOID page{nullptr};
  void** table{nullptr};
  RasterTable rasterTable{MakeRasterTable()};
  ProbeEngine engine{rasterTable.data() + 1};
  qt::Image image{};
  qt::Painter painter{.engine = &engine};
  ProbePeer dialogPeer{.id = kDialogPeerId};
  ProbePeer fromPeer{.id = kFromPeerId};
  ProbeHistory history{.peer = Address(&dialogPeer)};
  ProbeItem item{.fields = {.messageId = kMessageId,
                            .history = Address(&history),
                            .from = Address(&fromPeer),
                            .cachedDisplayFrom = 0,
                            .flags = telegram::kOutgoingFlag}};
  telegram::Message message{.item = Address(&item)};
  telegram::PaintContext context{};
  std::vector<MessageInfo> observedMessages;
  unsigned int* callbackCalls{nullptr};
  std::unique_ptr<MessageRenderHook> hook;
};

using VirtualProtectExFn = decltype(&VirtualProtectEx);

struct ProtectionFailure {
  VirtualProtectExFn original{nullptr};
  LPVOID address{nullptr};
  DWORD protection{};
};

[[nodiscard]] ProtectionFailure& Failure() {
  static ProtectionFailure failure;
  return failure;
}

BOOL WINAPI FailNextProtection(HANDLE process, LPVOID address, SIZE_T size,
                               DWORD protection, PDWORD previous) {
  auto& failure{Failure()};
  if (address == failure.address && protection == failure.protection) {
    failure.protection = 0;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return failure.original(process, address, size, protection, previous);
}

[[nodiscard]] void** FindVirtualProtectExImport() {
  auto* image{rgf::core::PointerCast<BYTE*>(GetModuleHandleW(nullptr))};
  if (image == nullptr) {
    return nullptr;
  }
  const auto* kDos{rgf::core::PointerCast<const IMAGE_DOS_HEADER*>(image)};
  const auto* kNt{
      rgf::core::PointerCast<const IMAGE_NT_HEADERS*>(image + kDos->e_lfanew)};
  const auto& kDirectory{
      kNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]};
  const auto* descriptor{rgf::core::PointerCast<const IMAGE_IMPORT_DESCRIPTOR*>(
      image + kDirectory.VirtualAddress)};
  for (; descriptor->Name != 0; ++descriptor) {
    auto* thunk{rgf::core::PointerCast<std::uintptr_t*>(
        image + descriptor->FirstThunk)};
    for (; *thunk != 0; ++thunk) {
      if (*thunk == std::bit_cast<std::uintptr_t>(&VirtualProtectEx)) {
        return rgf::core::PointerCast<void**>(thunk);
      }
    }
  }
  return nullptr;
}

class MessageRenderHookProtectionFailureUnit : public MessageRenderHookUnit {
 protected:
  void SetUp() override {
    MessageRenderHookUnit::SetUp();
    if (HasFatalFailure()) {
      return;
    }
    InstallProtectionHook();
  }

  void TearDown() override {
    Failure().address = nullptr;
    Failure().protection = 0;
    protectionHook_.reset();
    Failure().original = nullptr;
    MessageRenderHookUnit::TearDown();
  }

  static void FailNextChange(DWORD protection) {
    Failure().protection = protection;
  }

 private:
  void InstallProtectionHook() {
    void** const kImport{FindVirtualProtectExImport()};
    ASSERT_NE(kImport, nullptr);
    auto createdHook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{kImport}, 0)};
    ASSERT_TRUE(createdHook.has_value()) << createdHook.error().message();
    protectionHook_ =
        std::make_unique<rgf::hook::VmtHook>(std::move(*createdHook));
    Failure().original = protectionHook_->Original<VirtualProtectExFn>();
    ASSERT_TRUE(protectionHook_->SetDetour(&FailNextProtection).has_value());
    ASSERT_TRUE(protectionHook_->Enable().has_value());
    Failure().address = static_cast<LPVOID>(&table[telegram::kMessageDrawSlot]);
  }

  std::unique_ptr<rgf::hook::VmtHook> protectionHook_;
};

}  // namespace

TEST_F(MessageRenderHookUnit, CreationDoesNotBindOrInterceptTheOriginal) {
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_TRUE(hook->IsDetached());
  EXPECT_FALSE(hook->IsDrawing());
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalMessage));

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 0);
  EXPECT_EQ(hook->TextCalls(), 0);
  EXPECT_EQ(*callbackCalls, 0);
}

TEST_F(MessageRenderHookUnit,
       CreationIsRejectedUntilTheRegisteredOwnerIsDestroyed) {
  ASSERT_TRUE(hook->Enable().has_value());
  const void* const kDetour{Slot()};
  const std::error_code kBusy{ERROR_BUSY, std::system_category()};

  const auto kWhileAttached{CreateAnotherHook()};

  ASSERT_FALSE(kWhileAttached.has_value());
  EXPECT_EQ(kWhileAttached.error(), kBusy);
  EXPECT_EQ(Slot(), kDetour);
  ASSERT_TRUE(hook->Disable().has_value());

  const auto kAfterDetach{CreateAnotherHook()};

  ASSERT_FALSE(kAfterDetach.has_value());
  EXPECT_EQ(kAfterDetach.error(), kWhileAttached.error());
  EXPECT_TRUE(hook->CanRelease());
  hook.reset();

  auto replacement{CreateAnotherHook()};
  ASSERT_TRUE(replacement.has_value()) << replacement.error().message();
  hook = std::move(*replacement);
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(hook->TextCalls(), 1);
}

TEST_F(MessageRenderHookUnit,
       DestroyingAnUnregisteredOwnerPreservesAnotherOwnersRouting) {
  auto unused{CreateAnotherHook()};
  ASSERT_TRUE(unused.has_value()) << unused.error().message();
  ASSERT_TRUE(hook->Enable().has_value());

  unused->reset();
  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(hook->TextCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
}

TEST_F(MessageRenderHookUnit,
       RoutesAStatefulCallbackAfterTextAndRestoresTheScopedQtHook) {
  ASSERT_TRUE(hook->Enable().has_value());
  EXPECT_FALSE(hook->CanRelease());
  EXPECT_FALSE(hook->IsDetached());

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(message.receivedPainter, &painter);
  EXPECT_EQ(message.receivedContext, &context);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(hook->TextCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
  EXPECT_FALSE(hook->IsDrawing());
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
  EXPECT_EQ(engine.events,
            (std::vector{Event::Message, Event::Text, Event::Callback,
                         Event::Fill, Event::Finished}));
  EXPECT_DOUBLE_EQ(engine.rectangle.x, 20.0);
  EXPECT_DOUBLE_EQ(engine.rectangle.y, 16.75);
  EXPECT_DOUBLE_EQ(engine.rectangle.width, 87.75);
  EXPECT_DOUBLE_EQ(engine.rectangle.height, 16.75);
  EXPECT_EQ(engine.color.red, kColor.red);
  EXPECT_EQ(engine.color.blue, kColor.blue);

  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_TRUE(hook->IsDetached());
  Draw();
  EXPECT_EQ(message.originalCalls, 2);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(hook->TextCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
}

TEST_F(MessageRenderHookUnit, CountsTextRunsSeparatelyFromMessageDraws) {
  message.duringDraw = [this] { engine.Draw(); };
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(hook->TextCalls(), 2);
  EXPECT_EQ(*callbackCalls, 2);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(MessageRenderHookUnit, CallbackReceivesTheOwningMessageModelIdentity) {
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();

  ASSERT_EQ(observedMessages.size(), 1);
  const auto& info{observedMessages.front()};
  EXPECT_EQ(info.MessageId(), kMessageId);
  EXPECT_EQ(info.DialogPeerId(), kDialogPeerId);
  EXPECT_EQ(info.FromPeerId(), kFromPeerId);
  EXPECT_TRUE(info.IsOutgoing());
  EXPECT_EQ(hook->MetadataFailures(), 0);
}

TEST_F(MessageRenderHookUnit, NestedDifferentMessageRestoresTheOuterIdentity) {
  ProbeItem nestedItem{.fields = item.fields};
  nestedItem.fields.messageId = kNestedMessageId;
  telegram::Message nested{.item = Address(&nestedItem)};
  ASSERT_TRUE(hook->Enable().has_value());

  DrawWithNestedMessage(nested);

  EXPECT_EQ(CallbackMessageIds(),
            (std::vector{kMessageId, kNestedMessageId, kMessageId}));
  EXPECT_EQ(nested.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 2);
  EXPECT_EQ(hook->TextCalls(), 3);
  EXPECT_EQ(hook->MetadataFailures(), 0);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(MessageRenderHookUnit,
       InvalidNestedMessageCannotInheritTheOuterIdentity) {
  telegram::Message nested{};
  ASSERT_TRUE(hook->Enable().has_value());

  DrawWithNestedMessage(nested);

  EXPECT_EQ(CallbackMessageIds(), (std::vector{kMessageId, kMessageId}));
  EXPECT_EQ(nested.originalCalls, 1);
  EXPECT_EQ(hook->TextCalls(), 3);
  EXPECT_EQ(hook->MetadataFailures(), 1);
  EXPECT_EQ(hook->MetadataError(),
            std::make_error_code(std::errc::bad_address));
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(MessageRenderHookUnit,
       MissingMetadataForwardsOriginalWithoutInventingMessageIds) {
  message.item = 0;
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_TRUE(observedMessages.empty());
  EXPECT_EQ(hook->TextCalls(), 1);
  EXPECT_EQ(hook->MetadataFailures(), 1);
  EXPECT_EQ(hook->MetadataError(),
            std::make_error_code(std::errc::bad_address));
}

TEST_F(MessageRenderHookUnit,
       CallbackRetainsASnapshotWhenTheOriginalChangesItsModel) {
  ASSERT_TRUE(hook->Enable().has_value());

  DrawWhileChangingTheModel();
  Draw();

  ASSERT_EQ(observedMessages.size(), 2);
  EXPECT_EQ(CallbackMessageIds(), (std::vector{kMessageId, kNestedMessageId}));
  EXPECT_EQ(observedMessages.front().FromPeerId(), kFromPeerId);
  EXPECT_TRUE(observedMessages.front().IsOutgoing());
  EXPECT_EQ(observedMessages.back().FromPeerId(), kFromPeerId + 1);
  EXPECT_FALSE(observedMessages.back().IsOutgoing());
}

TEST_F(MessageRenderHookUnit, MasksImagesOnlyForTheSelectedSender) {
  ASSERT_TRUE(UseSenderFilter().has_value());
  PrepareImageMessage();
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();
  fromPeer.id = kFromPeerId + 1;
  Draw();

  EXPECT_EQ(message.originalCalls, 2);
  EXPECT_EQ(hook->MessageCalls(), 2);
  EXPECT_EQ(hook->TextCalls(), 0);
  EXPECT_EQ(hook->ImageCalls(), 2);
  EXPECT_EQ(hook->MaskedImageCalls(), 1);
  EXPECT_EQ(engine.imageCalls, 1);
  EXPECT_EQ(*callbackCalls, 2);
  EXPECT_EQ(engine.events,
            (std::vector{Event::Message, Event::Callback, Event::Fill,
                         Event::Finished, Event::Message, Event::Image,
                         Event::Finished}));
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(MessageRenderHookUnit,
       UnselectedImagesForwardTheOriginalReferencesAndConversionFlags) {
  ASSERT_TRUE(UseSenderFilter().has_value());
  fromPeer.id = kFromPeerId + 1;
  PrepareImageMessage();
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();

  EXPECT_EQ(engine.imageCalls, 1);
  ExpectOriginalImageArguments(engine);
  EXPECT_EQ(hook->ImageCalls(), 1);
  EXPECT_EQ(hook->MaskedImageCalls(), 0);
  EXPECT_EQ(hook->MaskedTextCalls(), 0);
  EXPECT_EQ(engine.events,
            (std::vector{Event::Message, Event::Image, Event::Finished}));
}

TEST_F(MessageRenderHookUnit,
       NestedUnselectedImagesAndTextRestoreTheSelectedOuterSender) {
  ASSERT_TRUE(UseSenderFilter().has_value());
  ProbePeer nestedFrom{.id = kFromPeerId + 1};
  ProbeItem nestedItem{.fields = item.fields};
  nestedItem.fields.messageId = kNestedMessageId;
  nestedItem.fields.from = Address(&nestedFrom);
  telegram::Message nested{.item = Address(&nestedItem)};
  ASSERT_TRUE(hook->Enable().has_value());

  DrawWithNestedImages(nested);

  EXPECT_EQ(CallbackMessageIds(),
            (std::vector{kMessageId, kNestedMessageId, kNestedMessageId,
                         kMessageId, kMessageId}));
  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(nested.originalCalls, 1);
  EXPECT_EQ(hook->ImageCalls(), 3);
  EXPECT_EQ(hook->MaskedImageCalls(), 2);
  EXPECT_EQ(hook->TextCalls(), 2);
  EXPECT_EQ(hook->MaskedTextCalls(), 1);
  EXPECT_EQ(engine.imageCalls, 1);
  ExpectOriginalImageArguments(engine);
  EXPECT_EQ(
      engine.events,
      (std::vector{Event::Message, Event::Callback, Event::Fill, Event::Message,
                   Event::Image, Event::Text, Event::Finished, Event::Callback,
                   Event::Fill, Event::Text, Event::Callback, Event::Fill,
                   Event::Finished}));
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(MessageRenderHookUnit,
       InvalidNestedImageMetadataCannotBorrowTheSelectedOuterSender) {
  ASSERT_TRUE(UseSenderFilter().has_value());
  telegram::Message nested{.drawsText = false};
  message.drawsText = false;
  ASSERT_TRUE(hook->Enable().has_value());

  DrawWithNestedImages(nested);

  EXPECT_EQ(CallbackMessageIds(), (std::vector{kMessageId, kMessageId}));
  EXPECT_EQ(nested.originalCalls, 1);
  EXPECT_EQ(hook->MetadataFailures(), 1);
  EXPECT_EQ(hook->MetadataError(),
            std::make_error_code(std::errc::bad_address));
  EXPECT_EQ(hook->ImageCalls(), 3);
  EXPECT_EQ(hook->MaskedImageCalls(), 2);
  EXPECT_EQ(engine.imageCalls, 1);
  ExpectOriginalImageArguments(engine);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(MessageRenderHookUnit,
       OffscreenImageCacheRenderingRemainsUntouchedDuringTheOriginalMessage) {
  ASSERT_TRUE(UseSenderFilter().has_value());
  ProbeEngine offscreen{rasterTable.data() + 1};
  PrepareImageMessageWithOffscreenRendering(offscreen);
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();
  DrawImage(offscreen);

  EXPECT_EQ(offscreen.imageCalls, 2);
  EXPECT_EQ(offscreen.events, (std::vector{Event::Image, Event::Image}));
  ExpectOriginalImageArguments(offscreen);
  EXPECT_EQ(offscreen.vtable, rasterTable.data() + 1);
  EXPECT_EQ(hook->ImageCalls(), 1);
  EXPECT_EQ(hook->MaskedImageCalls(), 1);
  EXPECT_EQ(engine.imageCalls, 0);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(MessageRenderHookUnit, CountsMessageWithoutTextForAnotherRasterTable) {
  auto otherRasterTable{MakeRasterTable()};
  engine.vtable = otherRasterTable.data() + 1;
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(hook->TextCalls(), 0);
  EXPECT_EQ(*callbackCalls, 0);
  EXPECT_EQ(engine.events,
            (std::vector{Event::Message, Event::Text, Event::Finished}));
  EXPECT_EQ(engine.vtable, otherRasterTable.data() + 1);
  engine.vtable = rasterTable.data() + 1;
}

TEST_F(MessageRenderHookUnit,
       DisableDuringDrawDefersWithoutChangingTheBinding) {
  ASSERT_TRUE(hook->Enable().has_value());
  const void* const kDetour{Slot()};
  std::error_code nestedError;
  message.duringDraw = [this, &nestedError, kDetour] {
    nestedError = DisableDuringDraw(kDetour);
  };

  Draw();

  const std::error_code kExpected{ERROR_BUSY, std::system_category()};
  EXPECT_EQ(nestedError, kExpected);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
}

TEST_F(MessageRenderHookUnit, NestedDrawKeepsTheOuterScopeProtected) {
  ASSERT_TRUE(hook->Enable().has_value());
  const std::error_code kBusy{ERROR_BUSY, std::system_category()};

  const std::error_code kRemovalError{DrawNestedMessage()};

  EXPECT_EQ(kRemovalError, kBusy);
  EXPECT_EQ(message.originalCalls, 2);
  EXPECT_EQ(hook->MessageCalls(), 2);
  EXPECT_EQ(hook->TextCalls(), 2);
  EXPECT_EQ(*callbackCalls, 2);
  EXPECT_FALSE(hook->IsDrawing());
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
}

TEST_F(MessageRenderHookUnit, RepeatedEnableKeepsTheActiveRouting) {
  ASSERT_TRUE(hook->Enable().has_value());
  const void* const kDetour{Slot()};

  const auto kEnabledAgain{hook->Enable()};

  ASSERT_FALSE(kEnabledAgain.has_value());
  EXPECT_EQ(kEnabledAgain.error(), rgf::hook::Error::HookAlreadyApplied);
  EXPECT_EQ(Slot(), kDetour);
  EXPECT_FALSE(hook->IsDetached());
  EXPECT_FALSE(hook->CanRelease());
  Draw();
  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
}

TEST_F(MessageRenderHookUnit,
       DisableSucceedsWhenNeverAttachedOrAlreadyDetached) {
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->IsDetached());
  EXPECT_TRUE(hook->CanRelease());
  ASSERT_TRUE(hook->Enable().has_value());
  ASSERT_TRUE(hook->Disable().has_value());

  ASSERT_TRUE(hook->Disable().has_value());

  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalMessage));
  EXPECT_TRUE(hook->IsDetached());
  EXPECT_TRUE(hook->CanRelease());
  Draw();
  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 0);
  EXPECT_EQ(hook->TextCalls(), 0);
  EXPECT_EQ(*callbackCalls, 0);
}

TEST_F(MessageRenderHookUnit,
       ForeignThreadCannotDisableAnAlreadyDetachedOwner) {
  ASSERT_TRUE(hook->Enable().has_value());
  ASSERT_TRUE(hook->Disable().has_value());
  const std::error_code kWrongThread{ERROR_INVALID_THREAD_ID,
                                     std::system_category()};

  const auto kRemoved{DisableOnForeignThread()};

  ASSERT_FALSE(kRemoved.has_value());
  EXPECT_EQ(kRemoved.error(), kWrongThread);
  EXPECT_TRUE(hook->IsDetached());
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalMessage));
}

TEST_F(MessageRenderHookUnit,
       ForeignThreadForwardsOriginalAndRequiresRetention) {
  // The owner deliberately remains retained in the child. Process exit releases
  // its memory after the joined worker has verified the sticky safety latch.
  EXPECT_EXIT(ExerciseForeignThread(), ::testing::ExitedWithCode(0), "");
}

TEST_F(MessageRenderHookProtectionFailureUnit,
       EnableFailureBeforePatchingAllowsReleaseOrRetry) {
  FailNextChange(PAGE_READWRITE);

  const auto kEnabled{hook->Enable()};

  ASSERT_FALSE(kEnabled.has_value());
  EXPECT_EQ(kEnabled.error(), ProtectionError());
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalMessage));
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_TRUE(hook->IsDetached());
  Draw();
  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 0);
  EXPECT_EQ(hook->TextCalls(), 0);
  EXPECT_EQ(*callbackCalls, 0);

  ASSERT_TRUE(hook->Enable().has_value());
  EXPECT_FALSE(hook->IsDetached());
  EXPECT_FALSE(hook->CanRelease());
  Draw();
  EXPECT_EQ(message.originalCalls, 2);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(hook->TextCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
}

TEST_F(MessageRenderHookProtectionFailureUnit,
       FailedEnableReservesItsReceiverUntilTheOwnerIsDestroyed) {
  auto waiting{CreateAnotherHook()};
  ASSERT_TRUE(waiting.has_value()) << waiting.error().message();
  FailNextChange(PAGE_READWRITE);
  const auto kFailed{hook->Enable()};
  ASSERT_FALSE(kFailed.has_value());
  EXPECT_EQ(kFailed.error(), ProtectionError());
  EXPECT_TRUE(hook->CanRelease());
  auto previousOwner{std::move(hook)};
  hook = std::move(*waiting);
  const std::error_code kBusy{ERROR_BUSY, std::system_category()};

  const auto kBlocked{hook->Enable()};

  ASSERT_FALSE(kBlocked.has_value());
  EXPECT_EQ(kBlocked.error(), kBusy);
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalMessage));
  EXPECT_TRUE(hook->CanRelease());
  previousOwner.reset();
  ASSERT_TRUE(hook->Enable().has_value());

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(hook->TextCalls(), 1);
}

TEST_F(MessageRenderHookProtectionFailureUnit,
       EnableIgnoresProtectionRestoreFailureAfterPatching) {
  FailNextChange(PAGE_READONLY);

  const auto kEnabled{hook->Enable()};

  ASSERT_TRUE(kEnabled.has_value());
  EXPECT_NE(Slot(), rgf::core::PointerCast<void*>(&OriginalMessage));
  EXPECT_FALSE(hook->CanRelease());
  EXPECT_FALSE(hook->IsDetached());
  Draw();
  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
}

TEST_F(MessageRenderHookProtectionFailureUnit,
       DisableFailureBeforePatchingKeepsRoutingUntilRemovalSucceeds) {
  ASSERT_TRUE(hook->Enable().has_value());
  const void* const kDetour{Slot()};
  FailNextChange(PAGE_READWRITE);

  const auto kDisabled{hook->Disable()};

  ASSERT_FALSE(kDisabled.has_value());
  EXPECT_EQ(kDisabled.error(), ProtectionError());
  EXPECT_EQ(Slot(), kDetour);
  EXPECT_FALSE(hook->CanRelease());
  EXPECT_FALSE(hook->IsDetached());
  Draw();
  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
}

TEST_F(MessageRenderHookProtectionFailureUnit,
       DisableIgnoresProtectionRestoreFailureAndRestoresOriginalRouting) {
  ASSERT_TRUE(hook->Enable().has_value());
  FailNextChange(PAGE_READONLY);

  const auto kDisabled{hook->Disable()};

  ASSERT_TRUE(kDisabled.has_value());
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalMessage));
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_TRUE(hook->IsDetached());
  Draw();
  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 0);
  EXPECT_EQ(hook->TextCalls(), 0);
  EXPECT_EQ(*callbackCalls, 0);
}
