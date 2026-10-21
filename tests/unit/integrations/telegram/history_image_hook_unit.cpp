#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <memory>
#include <rgf/core/cast.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/integrations/telegram/history_image_hook.hpp>
#include <rgf/integrations/telegram/internal/history.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "internal/build.hpp"

namespace {

namespace qt = rgf::qt::internal;
namespace telegram = rgf::integrations::telegram::internal;
using rgf::integrations::telegram::HistoryImageHook;
using RasterTable = std::array<void*, qt::kRasterVirtualCount + 1>;
using DeviceTable = std::array<void*, qt::kPaintDeviceRedirectedSlot + 1>;

constexpr SIZE_T kPageSize{4096};
constexpr qt::RectF kBounds{
    .x = 12.0, .y = 24.0, .width = 96.0, .height = 64.0};
constexpr qt::Color kColor{.spec = 1,
                           .alpha = 0xffff,
                           .red = 0x8080,
                           .green = 0,
                           .blue = 0xffff,
                           .padding = 0};

enum class Event : unsigned char { Paint, Image, Callback, Fill, Finished };

struct ProbeEngine : qt::PaintEngine {
  explicit ProbeEngine(void** table) : PaintEngine{table} {}

  void DrawImage() {
    const qt::Image kImage{};
    rgf::core::PointerCast<qt::DrawImageRectFn>(vtable[qt::kDrawImageRectSlot])(
        this, kBounds, kImage, kBounds, 0);
  }

  std::vector<Event> events;
  unsigned int originalImageCalls{};
  qt::RectF rectangle{};
  qt::Color color{};
};

[[nodiscard]] ProbeEngine& Engine(qt::PaintEngine* engine) {
  // Only ProbeEngine instances enter the fixture's image bindings.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  return *static_cast<ProbeEngine*>(engine);
}

void OriginalImage(qt::PaintEngine* engine, const qt::RectF& /*rectangle*/,
                   const qt::Image& /*image*/, const qt::RectF& /*source*/,
                   std::int32_t /*flags*/) {
  auto& probe{Engine(engine)};
  ++probe.originalImageCalls;
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
  table[qt::kDrawImageRectSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalImage);
  table[qt::kFillColorSlot + 1] = rgf::core::PointerCast<void*>(&FillColor);
  return table;
}

struct ProbeDevice {
  qt::PaintDevice device;
  qt::PaintDevice* redirected{};
  qt::PaintEngine* engine{};
  mutable unsigned int redirectCalls{};
  mutable unsigned int engineCalls{};
};

[[nodiscard]] qt::PaintDevice* RedirectedDevice(const qt::PaintDevice* device,
                                                qt::Point* offset) {
  const auto* kProbe{rgf::core::PointerCast<const ProbeDevice*>(device)};
  ++kProbe->redirectCalls;
  EXPECT_EQ(offset, nullptr);
  return kProbe->redirected;
}

[[nodiscard]] qt::PaintEngine* DevicePaintEngine(
    const qt::PaintDevice* device) {
  const auto* kProbe{rgf::core::PointerCast<const ProbeDevice*>(device)};
  ++kProbe->engineCalls;
  return kProbe->engine;
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

struct PaintState {
  ProbeEngine* engine;
  unsigned int originalCalls{};
  qt::PaintEvent* receivedEvent{};
  std::function<void()> duringPaint;
};

struct ProbeWidget {
  std::array<std::byte, qt::kWidgetPaintDeviceOffset> primary{};
  ProbeDevice device;
  PaintState* paint{nullptr};
};

static_assert(offsetof(ProbeWidget, device) == qt::kWidgetPaintDeviceOffset);

void OriginalPaint(qt::Widget* widget, qt::PaintEvent* event) {
  auto& state{*rgf::core::PointerCast<ProbeWidget*>(widget)->paint};
  ++state.originalCalls;
  state.receivedEvent = event;
  state.engine->events.push_back(Event::Paint);
  if (state.duringPaint) {
    state.duringPaint();
  }
  state.engine->DrawImage();
  state.engine->events.push_back(Event::Finished);
}

class HistoryImageHookUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    page = VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE);
    ASSERT_NE(page, nullptr);
    table = static_cast<void**>(page);
    table[telegram::kHistoryPaintSlot] =
        rgf::core::PointerCast<void*>(&OriginalPaint);
    DWORD previous{};
    ASSERT_NE(VirtualProtect(page, kPageSize, PAGE_READONLY, &previous), FALSE);
    CreateHook();
  }

  void TearDown() override {
    if (hook && !hook->IsDetached()) {
      EXPECT_TRUE(hook->Disable().has_value());
    }
    hook.reset();
    if (page != nullptr) {
      EXPECT_NE(VirtualFree(page, 0, MEM_RELEASE), FALSE);
    }
  }

  void CreateHook() {
    auto calls{std::make_unique<unsigned int>(0)};
    callbackCalls = calls.get();
    auto createdHook{HistoryImageHook::Create(
        rgf::hook::Vtable{table}, rasterTable.data() + 1, GetCurrentThreadId(),
        [this, calls = std::move(calls)](const rgf::qt::RasterCanvas& canvas,
                                         const qt::RectF& bounds) noexcept {
          ++*calls;
          EXPECT_EQ(hook->ImageCalls(), *calls);
          engine.events.push_back(Event::Callback);
          canvas.FillRect(bounds, kColor);
        })};
    ASSERT_TRUE(createdHook.has_value()) << createdHook.error().message();
    hook = std::move(*createdHook);
  }

  void Paint() {
    rgf::core::PointerCast<telegram::HistoryPaintFn>(Slot())(
        rgf::core::PointerCast<qt::Widget*>(&widget),
        rgf::core::PointerCast<qt::PaintEvent*>(&eventToken));
  }

  [[nodiscard]] void* Slot() const {
    return table[telegram::kHistoryPaintSlot];
  }

  [[nodiscard]] std::error_code DetachDuringPaint(const void* detour) {
    EXPECT_TRUE(hook->IsDrawing());
    EXPECT_FALSE(hook->CanRelease());
    EXPECT_NE(engine.vtable, rasterTable.data() + 1);
    const auto kRemoved{hook->Disable()};
    EXPECT_FALSE(kRemoved.has_value());
    EXPECT_EQ(Slot(), detour);
    return kRemoved ? std::error_code{} : kRemoved.error();
  }

  [[noreturn]] void ExerciseForeignThread() {
    if (!hook->Enable()) {
      std::_Exit(1);
    }
    std::jthread worker{[this] { Paint(); }};
    worker.join();
    if (paint.originalCalls != 1 || engine.originalImageCalls != 1 ||
        widget.device.redirectCalls != 0 || backing.engineCalls != 0 ||
        hook->PaintCalls() != 0 || hook->ImageCalls() != 0 ||
        *callbackCalls != 0 || hook->CanRelease()) {
      std::_Exit(2);
    }
    if (!hook->Disable() || hook->CanRelease() || !hook->IsDetached() ||
        Slot() != rgf::core::PointerCast<void*>(&OriginalPaint)) {
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
  DeviceTable deviceTable{MakeDeviceTable()};
  ProbeDevice backing{.device = {.vtable = deviceTable.data()},
                      .engine = &engine};
  PaintState paint{.engine = &engine};
  ProbeWidget widget{.device = {.device = {.vtable = deviceTable.data()},
                                .redirected = &backing.device},
                     .paint = &paint};
  int eventToken{};
  unsigned int* callbackCalls{nullptr};
  std::unique_ptr<HistoryImageHook> hook;
};

}  // namespace

TEST_F(HistoryImageHookUnit, CreationLeavesOriginalPaintUnchanged) {
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_TRUE(hook->IsDetached());
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalPaint));

  Paint();

  EXPECT_EQ(paint.originalCalls, 1);
  EXPECT_EQ(engine.originalImageCalls, 1);
  EXPECT_EQ(widget.device.redirectCalls, 0);
  EXPECT_EQ(hook->PaintCalls(), 0);
  EXPECT_EQ(*callbackCalls, 0);
}

TEST_F(HistoryImageHookUnit,
       MasksImagesDuringPaintAndRestoresTheBackingEngine) {
  ASSERT_TRUE(hook->Enable().has_value());

  Paint();

  EXPECT_EQ(paint.originalCalls, 1);
  EXPECT_EQ(paint.receivedEvent,
            rgf::core::PointerCast<qt::PaintEvent*>(&eventToken));
  EXPECT_EQ(widget.device.redirectCalls, 1);
  EXPECT_EQ(backing.engineCalls, 1);
  EXPECT_EQ(hook->PaintCalls(), 1);
  EXPECT_EQ(hook->ImageCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
  EXPECT_EQ(hook->SkippedPaints(), 0);
  EXPECT_FALSE(hook->PaintError());
  EXPECT_EQ(engine.originalImageCalls, 0);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
  EXPECT_EQ(engine.events, (std::vector{Event::Paint, Event::Callback,
                                        Event::Fill, Event::Finished}));
  EXPECT_DOUBLE_EQ(engine.rectangle.x, kBounds.x);
  EXPECT_DOUBLE_EQ(engine.rectangle.y, kBounds.y);
  EXPECT_DOUBLE_EQ(engine.rectangle.width, kBounds.width);
  EXPECT_DOUBLE_EQ(engine.rectangle.height, kBounds.height);
  EXPECT_EQ(engine.color.red, kColor.red);
  EXPECT_EQ(engine.color.blue, kColor.blue);
  EXPECT_FALSE(hook->IsDrawing());
  EXPECT_FALSE(hook->CanRelease());
}

TEST_F(HistoryImageHookUnit, MissingRedirectionReportsFailureAndRunsOriginal) {
  widget.device.redirected = nullptr;
  ASSERT_TRUE(hook->Enable().has_value());

  Paint();

  EXPECT_EQ(paint.originalCalls, 1);
  EXPECT_EQ(engine.originalImageCalls, 1);
  EXPECT_EQ(widget.device.redirectCalls, 1);
  EXPECT_EQ(backing.engineCalls, 0);
  EXPECT_EQ(hook->PaintCalls(), 1);
  EXPECT_EQ(hook->SkippedPaints(), 1);
  EXPECT_EQ(hook->PaintError(), std::make_error_code(std::errc::not_supported));
  EXPECT_EQ(hook->ImageCalls(), 0);
  EXPECT_EQ(*callbackCalls, 0);
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
}

TEST_F(HistoryImageHookUnit, OtherPaintEngineRemainsUnchangedAndIsReported) {
  auto otherRasterTable{MakeRasterTable()};
  engine.vtable = otherRasterTable.data() + 1;
  ASSERT_TRUE(hook->Enable().has_value());

  Paint();

  EXPECT_EQ(paint.originalCalls, 1);
  EXPECT_EQ(engine.originalImageCalls, 1);
  EXPECT_EQ(hook->SkippedPaints(), 1);
  EXPECT_EQ(hook->PaintError(), std::make_error_code(std::errc::not_supported));
  EXPECT_EQ(*callbackCalls, 0);
  EXPECT_EQ(engine.vtable, otherRasterTable.data() + 1);
  engine.vtable = rasterTable.data() + 1;
}

TEST_F(HistoryImageHookUnit, CannotDetachUntilPaintAndItsImageScopeReturn) {
  ASSERT_TRUE(hook->Enable().has_value());
  const void* const kDetour{Slot()};
  std::error_code detachError;
  paint.duringPaint = [this, &detachError, kDetour] {
    detachError = DetachDuringPaint(kDetour);
  };

  Paint();

  const std::error_code kBusy{ERROR_BUSY, std::system_category()};
  EXPECT_EQ(detachError, kBusy);
  EXPECT_FALSE(hook->IsDrawing());
  EXPECT_EQ(engine.vtable, rasterTable.data() + 1);
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
}

TEST_F(HistoryImageHookUnit, DetachedOwnerRestoresPaintingAndCanBeRecreated) {
  ASSERT_TRUE(hook->Enable().has_value());
  Paint();
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->CanRelease());
  EXPECT_EQ(Slot(), rgf::core::PointerCast<void*>(&OriginalPaint));

  Paint();

  EXPECT_EQ(paint.originalCalls, 2);
  EXPECT_EQ(engine.originalImageCalls, 1);
  EXPECT_EQ(hook->PaintCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
  hook.reset();
  CreateHook();
  ASSERT_NE(hook, nullptr);
  ASSERT_TRUE(hook->Enable().has_value());

  Paint();

  EXPECT_EQ(paint.originalCalls, 3);
  EXPECT_EQ(engine.originalImageCalls, 1);
  EXPECT_EQ(hook->ImageCalls(), 1);
  EXPECT_EQ(*callbackCalls, 1);
}

TEST_F(HistoryImageHookUnit, ForeignThreadForwardsWithoutLookingUpQtObjects) {
  // The child intentionally retains an owner that observed a foreign thread.
  EXPECT_EXIT(ExerciseForeignThread(), ::testing::ExitedWithCode(0), "");
}
