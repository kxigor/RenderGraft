#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <rgf/core/cast.hpp>
#include <rgf/hook/error.hpp>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/telegram/internal/message.hpp>
#include <rgf/telegram/message_text_hook.hpp>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "internal/build.hpp"

namespace rgf::qt::internal {

struct Painter {
  PaintEngine* engine;
};

}  // namespace rgf::qt::internal

namespace rgf::telegram::internal {

struct PaintContext {};

struct Message {
  mutable unsigned int originalCalls{};
  mutable qt::internal::Painter* receivedPainter{nullptr};
  mutable const PaintContext* receivedContext{nullptr};
  std::function<void()> duringDraw;
};

}  // namespace rgf::telegram::internal

namespace {

namespace qt = rgf::qt::internal;
namespace telegram = rgf::telegram::internal;
using rgf::telegram::MessageTextHook;
using RasterTable = std::array<void*, qt::kRasterVirtualCount + 1>;

constexpr SIZE_T kPageSize{4096};
constexpr qt::PointF kPosition{.x = 20.0, .y = 30.0};
constexpr qt::TextItem kItem{.descent = 224, .ascent = 848, .width = 5616};
constexpr qt::Color kColor{.spec = 1,
                           .alpha = 0xffff,
                           .red = 0x8080,
                           .green = 0,
                           .blue = 0xffff,
                           .padding = 0};

enum class Event : unsigned char { Message, Text, Callback, Fill, Finished };

struct ProbeEngine : qt::PaintEngine {
  explicit ProbeEngine(void** table) : PaintEngine{table} {}

  void Draw() {
    rgf::core::PointerCast<qt::DrawTextItem>(vtable[qt::kDrawTextItemSlot])(
        this, kPosition, kItem);
  }

  std::vector<Event> events;
  qt::RectF rectangle{};
  qt::Color color{};
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
  return table;
}

[[nodiscard]] qt::PaintEngine* GetPaintEngine(const qt::Painter* painter) {
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
  engine.Draw();
  engine.events.push_back(Event::Finished);
}

class MessageTextHookUnit : public ::testing::Test {
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
    auto createdHook{MessageTextHook::Create(
        rgf::hook::Vtable{table}, &GetPaintEngine, rasterTable.data() + 1,
        GetCurrentThreadId(),
        [this, calls = std::move(calls)](const rgf::qt::RasterCanvas& canvas,
                                         const qt::RectF& bounds) noexcept {
          ++*calls;
          engine.events.push_back(Event::Callback);
          canvas.FillRect(bounds, kColor);
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

  void Draw() {
    rgf::core::PointerCast<telegram::MessageDraw>(Slot())(&message, painter,
                                                          context);
  }

  [[nodiscard]] void* Slot() const { return table[telegram::kMessageDrawSlot]; }

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

  [[noreturn]] void ExerciseForeignThread() {
    if (!hook->Enable()) {
      std::_Exit(1);
    }
    std::jthread worker{[this] { Draw(); }};
    worker.join();
    if (message.originalCalls != 1 || *callbackCalls != 0 ||
        hook->MessageCalls() != 0 || hook->CanRelease() ||
        engine.vtable != rasterTable.data() + 1) {
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
  qt::Painter painter{&engine};
  telegram::Message message{};
  telegram::PaintContext context{};
  unsigned int* callbackCalls{nullptr};
  std::unique_ptr<MessageTextHook> hook;
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

class MessageTextHookProtectionFailureUnit : public MessageTextHookUnit {
 protected:
  void SetUp() override {
    MessageTextHookUnit::SetUp();
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
    MessageTextHookUnit::TearDown();
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

TEST_F(MessageTextHookUnit, CreationDoesNotBindOrInterceptTheOriginal) {
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_TRUE(hook->IsDetached());
  EXPECT_FALSE(hook->IsDrawing());
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalMessage));

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(hook->MessageCalls(), 0);
  EXPECT_EQ(*callbackCalls, 0);
}

TEST_F(MessageTextHookUnit,
       RoutesAStatefulCallbackAfterTextAndRestoresTheScopedQtHook) {
  ASSERT_TRUE(hook->Enable().has_value());
  EXPECT_FALSE(hook->CanRelease());
  EXPECT_FALSE(hook->IsDetached());

  Draw();

  EXPECT_EQ(message.originalCalls, 1);
  EXPECT_EQ(message.receivedPainter, &painter);
  EXPECT_EQ(message.receivedContext, &context);
  EXPECT_EQ(hook->MessageCalls(), 1);
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
  EXPECT_EQ(*callbackCalls, 1);
}

TEST_F(MessageTextHookUnit, DisableDuringDrawDefersWithoutChangingTheBinding) {
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

TEST_F(MessageTextHookUnit, RepeatedEnableKeepsTheActiveRouting) {
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

TEST_F(MessageTextHookUnit, DisableSucceedsWhenNeverAttachedOrAlreadyDetached) {
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
  EXPECT_EQ(*callbackCalls, 0);
}

TEST_F(MessageTextHookUnit, ForeignThreadForwardsOriginalAndRequiresRetention) {
  // The owner deliberately remains retained in the child. Process exit releases
  // its memory after the joined worker has verified the sticky safety latch.
  EXPECT_EXIT(ExerciseForeignThread(), ::testing::ExitedWithCode(0), "");
}

TEST_F(MessageTextHookProtectionFailureUnit,
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
  EXPECT_EQ(*callbackCalls, 0);

  ASSERT_TRUE(hook->Enable().has_value());
  EXPECT_FALSE(hook->IsDetached());
  EXPECT_FALSE(hook->CanRelease());
  Draw();
  EXPECT_EQ(message.originalCalls, 2);
  EXPECT_EQ(hook->MessageCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
}

TEST_F(MessageTextHookProtectionFailureUnit,
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

TEST_F(MessageTextHookProtectionFailureUnit,
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

TEST_F(MessageTextHookProtectionFailureUnit,
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
  EXPECT_EQ(*callbackCalls, 0);
}
