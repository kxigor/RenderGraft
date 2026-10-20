#include <gtest/gtest.h>
#include <windows.h>

#include <cstdint>
#include <rgf/core/cast.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/widget_updater.hpp>
#include <system_error>

namespace rgf::qt::internal {

struct Widget {
  unsigned int updateCalls{};
};

}  // namespace rgf::qt::internal

namespace {

namespace qt = rgf::qt::internal;

constexpr std::uintptr_t kWindowId{
    static_cast<std::uintptr_t>(0x0123456789abcdefULL)};
constexpr std::uint32_t kSeed{0x13579bdf};
constexpr std::uint32_t kExpectedHash{
    sizeof(std::uintptr_t) == 8 ? 0x98badcffU : 0x9afc5631U};

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
  void ExpectNotFound() const {
    const auto kResult{updater.Update(kWindowId)};
    ASSERT_FALSE(kResult.has_value());
    const std::error_code kExpected{ERROR_NOT_FOUND, std::system_category()};
    EXPECT_EQ(kResult.error(), kExpected);
    EXPECT_EQ(widget.updateCalls, 0);
  }

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
      .next = nullptr, .hash = 0, .windowId = kWindowId, .widget = &widget};
  MapperProbe mapper{&data, &node};
  qt::WidgetMapper* currentMapper{&mapper};
  rgf::qt::WidgetUpdater updater{&currentMapper, &FindNode, &UpdateWidget};
};

void ExpectOneLookupAndUpdate(const MapperProbe& mapper,
                              const qt::Widget& widget,
                              std::uint32_t expectedHash) {
  EXPECT_EQ(mapper.findCalls, 1);
  EXPECT_EQ(mapper.receivedWindowId, kWindowId);
  EXPECT_EQ(mapper.receivedHash, expectedHash);
  EXPECT_EQ(widget.updateCalls, 1);
}

class WidgetUpdaterUnit : public ::testing::Test {
 protected:
  [[nodiscard]] FakeQtHost& Host() noexcept { return host_; }

 private:
  FakeQtHost host_{};
};

}  // namespace

TEST_F(WidgetUpdaterUnit, ForwardsTheWindowIdAndSeededHashAndUpdatesTheWidget) {
  auto& host{Host()};
  const auto kResult{host.updater.Update(kWindowId)};

  ASSERT_TRUE(kResult.has_value());
  ExpectOneLookupAndUpdate(host.mapper, host.widget, kExpectedHash);
}

TEST_F(WidgetUpdaterUnit, UsesTheCurrentMapperAndItsSeedForEveryUpdate) {
  auto& host{Host()};
  ASSERT_TRUE(host.updater.Update(kWindowId).has_value());
  qt::Widget otherWidget{};
  qt::WidgetHashData otherData{host.data};
  constexpr std::uint32_t kOtherSeed{0x2468ace0};
  otherData.seed = kOtherSeed;
  qt::WidgetHashNode otherNode{host.node};
  otherNode.widget = &otherWidget;
  MapperProbe otherMapper{&otherData, &otherNode};
  host.currentMapper = &otherMapper;

  const auto kResult{host.updater.Update(kWindowId)};

  ASSERT_TRUE(kResult.has_value());
  constexpr std::uint32_t kOtherHash{sizeof(std::uintptr_t) == 8 ? 0xaf85ebc0U
                                                                 : 0xadc3610eU};
  ExpectOneLookupAndUpdate(host.mapper, host.widget, kExpectedHash);
  ExpectOneLookupAndUpdate(otherMapper, otherWidget, kOtherHash);
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
  host.node.windowId = kWindowId + 1;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 1);
}

TEST_F(WidgetUpdaterUnit, RejectsANodeWithoutAWidget) {
  auto& host{Host()};
  host.node.widget = nullptr;

  host.ExpectNotFound();

  EXPECT_EQ(host.mapper.findCalls, 1);
}
