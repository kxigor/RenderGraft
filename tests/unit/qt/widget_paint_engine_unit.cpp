#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <rgf/core/cast.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/widget_paint_engine.hpp>
#include <system_error>
#include <type_traits>

namespace {

namespace qt = rgf::qt::internal;
using DeviceTable = std::array<void*, qt::kPaintDeviceRedirectedSlot + 1>;

struct ProbeDevice {
  qt::PaintDevice object{};
  qt::PaintDevice* redirected{};
  qt::PaintEngine* engine{};
  mutable const qt::PaintDevice* redirectedThis{};
  mutable const qt::PaintDevice* engineThis{};
  mutable bool receivedNullOffset{false};
  mutable unsigned int engineCalls{};
};

struct ProbeWidget {
  void** primaryVtable{};
  std::array<std::byte, qt::kWidgetPaintDeviceOffset - sizeof(void**)>
      padding{};
  ProbeDevice secondary;
  mutable unsigned int primaryCalls{};
};

static_assert(std::is_standard_layout_v<ProbeDevice>);
static_assert(std::is_standard_layout_v<ProbeWidget>);
static_assert(offsetof(ProbeDevice, object) == 0);
static_assert(offsetof(ProbeWidget, secondary) == qt::kWidgetPaintDeviceOffset);

qt::PaintDevice* Redirected(const qt::PaintDevice* device, qt::Point* offset) {
  const auto* kProbe{rgf::core::PointerCast<const ProbeDevice*>(device)};
  kProbe->redirectedThis = device;
  kProbe->receivedNullOffset = offset == nullptr;
  return kProbe->redirected;
}

qt::PaintEngine* Engine(const qt::PaintDevice* device) {
  const auto* kProbe{rgf::core::PointerCast<const ProbeDevice*>(device)};
  kProbe->engineThis = device;
  ++kProbe->engineCalls;
  return kProbe->engine;
}

qt::PaintDevice* PrimaryRedirected(const qt::PaintDevice* device,
                                   qt::Point* /*offset*/) {
  const auto* kWidget{rgf::core::PointerCast<const ProbeWidget*>(device)};
  ++kWidget->primaryCalls;
  return kWidget->secondary.redirected;
}

qt::PaintEngine* PrimaryEngine(const qt::PaintDevice* device) {
  const auto* kWidget{rgf::core::PointerCast<const ProbeWidget*>(device)};
  ++kWidget->primaryCalls;
  return kWidget->secondary.engine;
}

struct FakeQtHost {
  FakeQtHost() {
    primaryTable[qt::kPaintDeviceRedirectedSlot] =
        rgf::core::PointerCast<void*>(&PrimaryRedirected);
    primaryTable[qt::kPaintDevicePaintEngineSlot] =
        rgf::core::PointerCast<void*>(&PrimaryEngine);
    deviceTable[qt::kPaintDeviceRedirectedSlot] =
        rgf::core::PointerCast<void*>(&Redirected);
    deviceTable[qt::kPaintDevicePaintEngineSlot] =
        rgf::core::PointerCast<void*>(&Engine);
    widget.primaryVtable = primaryTable.data();
    widget.secondary.object.vtable = deviceTable.data();
    widget.secondary.redirected = &target.object;
    widget.secondary.engine = &wrongEngine;
    target.object.vtable = deviceTable.data();
    target.engine = &backingEngine;
  }

  [[nodiscard]] const qt::Widget& Widget() const noexcept {
    // The helper projects this synthetic MSVC QWidget layout through its ABI.
    return *rgf::core::PointerCast<const qt::Widget*>(&widget);
  }

  DeviceTable primaryTable{};
  DeviceTable deviceTable{};
  ProbeWidget widget{};
  ProbeDevice target{};
  qt::PaintEngine wrongEngine{};
  qt::PaintEngine backingEngine{};
};

}  // namespace

TEST(WidgetPaintEngineUnit, UsesTheSecondaryBaseAndItsRedirectedPaintEngine) {
  const FakeQtHost kHost{};

  const auto kEngine{rgf::qt::WidgetPaintEngine(kHost.Widget())};

  ASSERT_TRUE(kEngine.has_value());
  EXPECT_EQ(*kEngine, &kHost.backingEngine);
  EXPECT_EQ(kHost.widget.secondary.redirectedThis,
            &kHost.widget.secondary.object);
  EXPECT_TRUE(kHost.widget.secondary.receivedNullOffset);
  EXPECT_EQ(kHost.target.engineThis, &kHost.target.object);
  EXPECT_EQ(kHost.target.engineCalls, 1);
  EXPECT_EQ(kHost.widget.secondary.engineCalls, 0);
  EXPECT_EQ(kHost.widget.primaryCalls, 0);
}

TEST(WidgetPaintEngineUnit, RejectsAWidgetOutsideRedirectedPainting) {
  FakeQtHost host{};
  host.widget.secondary.redirected = nullptr;

  const auto kEngine{rgf::qt::WidgetPaintEngine(host.Widget())};

  ASSERT_FALSE(kEngine.has_value());
  EXPECT_EQ(kEngine.error(), std::make_error_code(std::errc::not_supported));
  EXPECT_EQ(host.target.engineCalls, 0);
  EXPECT_EQ(host.widget.secondary.engineCalls, 0);
  EXPECT_EQ(host.widget.primaryCalls, 0);
}

TEST(WidgetPaintEngineUnit, RejectsARedirectedDeviceWithoutAPaintEngine) {
  FakeQtHost host{};
  host.target.engine = nullptr;

  const auto kEngine{rgf::qt::WidgetPaintEngine(host.Widget())};

  ASSERT_FALSE(kEngine.has_value());
  EXPECT_EQ(kEngine.error(), std::make_error_code(std::errc::not_supported));
  EXPECT_EQ(host.target.engineCalls, 1);
  EXPECT_EQ(host.widget.primaryCalls, 0);
}

TEST(WidgetPaintEngineUnit, ResolvesTheCurrentBackingStoreOnEveryCall) {
  FakeQtHost host{};
  const auto kFirst{rgf::qt::WidgetPaintEngine(host.Widget())};
  ASSERT_TRUE(kFirst.has_value());
  EXPECT_EQ(*kFirst, &host.backingEngine);

  qt::PaintEngine replacementEngine{};
  ProbeDevice replacement{};
  replacement.object.vtable = host.deviceTable.data();
  replacement.engine = &replacementEngine;
  host.widget.secondary.redirected = &replacement.object;

  const auto kSecond{rgf::qt::WidgetPaintEngine(host.Widget())};

  ASSERT_TRUE(kSecond.has_value());
  EXPECT_EQ(*kSecond, &replacementEngine);
  EXPECT_EQ(replacement.engineThis, &replacement.object);
  EXPECT_EQ(replacement.engineCalls, 1);
  EXPECT_EQ(host.target.engineCalls, 1);
  EXPECT_EQ(host.widget.primaryCalls, 0);
}
