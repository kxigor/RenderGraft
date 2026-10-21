#include <gtest/gtest.h>
#include <windows.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <rgf/core/cast.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/widget_updater.hpp>
#include <rgf/win/error.hpp>
#include <system_error>
#include <thread>
#include <type_traits>

namespace rgf::qt::internal {

struct Widget {
  unsigned int updateCalls{};
};

}  // namespace rgf::qt::internal

namespace {

namespace qt = rgf::qt::internal;

constexpr std::uint32_t kSeed{0x13579bdf};

using WindowOwner =
    std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)>;

[[nodiscard]] WindowOwner CreateProbeWindow() {
  auto* const kModule{GetModuleHandleW(nullptr)};
  if (kModule == nullptr) {
    return WindowOwner{nullptr, &DestroyWindow};
  }
  return WindowOwner{
      CreateWindowExW(0, L"STATIC", L"RenderGraft Qt widget updater probe",
                      WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, kModule, nullptr),
      &DestroyWindow};
}

struct MapperProbe : qt::WidgetMapper {
  MapperProbe(qt::WidgetHashData* hashData, qt::WidgetHashNode* node)
      : WidgetMapper{hashData}, result{node} {}

  mutable qt::WidgetHashNode* result;
  mutable unsigned int findCalls{};
  mutable std::uintptr_t receivedWindowId{};
  mutable std::uint32_t receivedHash{};
};

[[nodiscard]] qt::WidgetHashNode** FindNode(const qt::WidgetMapper* mapper,
                                            const std::uintptr_t& windowId,
                                            std::uint32_t hash) {
  // Test bindings only pass MapperProbe instances to the host lookup stub.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto* probe{static_cast<const MapperProbe*>(mapper)};
  ++probe->findCalls;
  probe->receivedWindowId = windowId;
  probe->receivedHash = hash;
  return &probe->result;
}

void UpdateWidget(qt::Widget* widget) { ++widget->updateCalls; }

struct FakeQtHost {
  [[nodiscard]] std::uintptr_t WindowId() const noexcept {
    return rgf::core::PointerCast<std::uintptr_t>(window.get());
  }

  void ExpectNotFound() const {
    const auto kResult{updater.Update(window.get())};
    ASSERT_FALSE(kResult.has_value());
    const std::error_code kExpected{ERROR_NOT_FOUND, std::system_category()};
    EXPECT_EQ(kResult.error(), kExpected);
    EXPECT_EQ(widget.updateCalls, 0);
  }

  WindowOwner window{CreateProbeWindow()};
  qt::Widget widget{};
  qt::WidgetHashData data{.fakeNext = nullptr,
                          .buckets = nullptr,
                          .referenceCount = 1,
                          .size = 1,
                          .nodeSize = 0,
                          .userNumBits = 0,
                          .numBits = 0,
                          .numBuckets = 1,
                          .seed = kSeed};
  qt::WidgetHashNode node{
      .next = nullptr, .hash = 0, .windowId = WindowId(), .widget = &widget};
  MapperProbe mapper{&data, &node};
  qt::WidgetMapper* currentMapper{&mapper};
  rgf::qt::WidgetUpdater updater{&currentMapper, &FindNode, &UpdateWidget};
};

void ExpectOneLookupAndUpdate(const MapperProbe& mapper,
                              const qt::Widget& widget, std::uintptr_t windowId,
                              std::uint32_t seed) {
  EXPECT_EQ(mapper.findCalls, 1);
  EXPECT_EQ(mapper.receivedWindowId, windowId);
  // The host's Qt qHash(quintptr) folds at bit 31 before applying its seed.
  constexpr unsigned int kHashShift{31};
  const std::uint32_t kExpectedHash{
      static_cast<std::uint32_t>(windowId) ^
      static_cast<std::uint32_t>(windowId >> kHashShift) ^ seed};
  EXPECT_EQ(mapper.receivedHash, kExpectedHash);
  EXPECT_EQ(widget.updateCalls, 1);
}

class WidgetUpdaterUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NE(host_.window.get(), nullptr) << rgf::win::LastError().message();
  }

  [[nodiscard]] FakeQtHost& Host() noexcept { return host_; }

 private:
  FakeQtHost host_{};
};

}  // namespace

TEST_F(WidgetUpdaterUnit, ForwardsTheWindowIdAndSeededHashAndUpdatesTheWidget) {
  auto& host{Host()};
  const auto kResult{host.updater.Update(host.window.get())};

  ASSERT_TRUE(kResult.has_value());
  ExpectOneLookupAndUpdate(host.mapper, host.widget, host.WindowId(), kSeed);
}

TEST_F(WidgetUpdaterUnit, UsesTheCurrentMapperAndItsSeedForEveryUpdate) {
  auto& host{Host()};
  ASSERT_TRUE(host.updater.Update(host.window.get()).has_value());
  qt::Widget otherWidget{};
  qt::WidgetHashData otherData{host.data};
  constexpr std::uint32_t kOtherSeed{0x2468ace0};
  otherData.seed = kOtherSeed;
  qt::WidgetHashNode otherNode{host.node};
  otherNode.widget = &otherWidget;
  MapperProbe otherMapper{&otherData, &otherNode};
  host.currentMapper = &otherMapper;

  const auto kResult{host.updater.Update(host.window.get())};

  ASSERT_TRUE(kResult.has_value());
  ExpectOneLookupAndUpdate(host.mapper, host.widget, host.WindowId(), kSeed);
  ExpectOneLookupAndUpdate(otherMapper, otherWidget, host.WindowId(),
                           kOtherSeed);
  EXPECT_EQ(otherMapper.receivedHash ^ host.mapper.receivedHash,
            kOtherSeed ^ kSeed);
}

TEST_F(WidgetUpdaterUnit, RejectsAMissingMapperWithoutCallingHostFunctions) {
  auto& host{Host()};
  host.currentMapper = nullptr;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 0);
}

TEST_F(WidgetUpdaterUnit, RejectsAMapperWithoutHashData) {
  auto& host{Host()};
  host.mapper.data = nullptr;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 0);
}

TEST_F(WidgetUpdaterUnit, RejectsAnEmptyWidgetMap) {
  auto& host{Host()};
  host.data.size = 0;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 0);
}

TEST_F(WidgetUpdaterUnit, RejectsAWidgetMapWithoutBuckets) {
  auto& host{Host()};
  host.data.numBuckets = 0;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 0);
}

TEST_F(WidgetUpdaterUnit, RejectsAMissingNode) {
  auto& host{Host()};
  host.mapper.result = nullptr;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 1);
}

TEST_F(WidgetUpdaterUnit, RejectsTheHashSentinelAsAMissingNode) {
  auto& host{Host()};
  host.mapper.result = rgf::core::PointerCast<qt::WidgetHashNode*>(&host.data);

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 1);
}

TEST_F(WidgetUpdaterUnit, RejectsANodeForAnotherWindow) {
  auto& host{Host()};
  host.node.windowId = host.WindowId() + 1;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 1);
}

TEST_F(WidgetUpdaterUnit, RejectsANodeWithoutAWidget) {
  auto& host{Host()};
  host.node.widget = nullptr;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 1);
}

TEST_F(WidgetUpdaterUnit, InvalidWindowPreservesTheWin32ThreadQueryError) {
  auto& host{Host()};
  SetLastError(ERROR_SUCCESS);

  const auto kResult{host.updater.Update(nullptr)};
  const std::error_code kError{rgf::win::LastError()};

  ASSERT_FALSE(kResult.has_value());
  EXPECT_NE(kError.value(), 0);
  EXPECT_EQ(kResult.error(), kError);
  EXPECT_EQ(&kResult.error().category(), &std::system_category());
  EXPECT_EQ(host.mapper.findCalls, 0);
  EXPECT_EQ(host.widget.updateCalls, 0);
}

TEST_F(WidgetUpdaterUnit, RejectsAnotherThreadBeforeCallingHostFunctions) {
  auto& host{Host()};
  std::expected<void, std::error_code> result{};
  {
    const std::jthread kWorker{
        [&host, &result] { result = host.updater.Update(host.window.get()); }};
  }

  ASSERT_FALSE(result.has_value());
  const std::error_code kExpected{ERROR_INVALID_THREAD_ID,
                                  std::system_category()};
  EXPECT_EQ(result.error(), kExpected);
  EXPECT_EQ(host.mapper.findCalls, 0);
  EXPECT_EQ(host.widget.updateCalls, 0);
}
