#include <gtest/gtest.h>
#include <windows.h>

#include <array>
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

void UpdateWidget(qt::Widget* widget) { ++widget->updateCalls; }

struct FakeQtHost {
  FakeQtHost() {
    for (auto& span : spans) {
      span.offsets.fill(qt::kWidgetHashUnusedEntry);
    }
    spans.front().offsets.back() = 1;
  }

  [[nodiscard]] std::uintptr_t WindowId() const noexcept {
    return rgf::core::PointerCast<std::uintptr_t>(window.get());
  }

  void ExpectFailure(DWORD error) const {
    const auto kResult{updater.Update(window.get())};
    ASSERT_FALSE(kResult.has_value());
    const std::error_code kExpected{static_cast<int>(error),
                                    std::system_category()};
    EXPECT_EQ(kResult.error(), kExpected);
    EXPECT_EQ(widget.updateCalls, 0);
  }

  WindowOwner window{CreateProbeWindow()};
  qt::Widget widget{};
  std::array<qt::WidgetHashNode, 2> entries{
      qt::WidgetHashNode{.windowId = WindowId() + 1, .widget = nullptr},
      qt::WidgetHashNode{.windowId = WindowId(), .widget = &widget}};
  std::array<qt::WidgetHashSpan, 2> spans{
      qt::WidgetHashSpan{.offsets = {},
                         .entries = entries.data(),
                         .allocated = 2,
                         .nextFree = 2},
      qt::WidgetHashSpan{}};
  qt::WidgetHashData data{.referenceCount = 1,
                          .size = 1,
                          .numBuckets = qt::kWidgetHashSpanBucketCount,
                          .seed = 0,
                          .spans = spans.data()};
  qt::WidgetMapper mapper{&data};
  qt::WidgetMapper* currentMapper{&mapper};
  rgf::qt::WidgetUpdater updater{&currentMapper, &UpdateWidget};
};

class WidgetUpdaterUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NE(host_.window.get(), nullptr) << rgf::win::LastError().message();
  }

  [[nodiscard]] FakeQtHost& Host() noexcept { return host_; }

 private:
  FakeQtHost host_;
};

}  // namespace

TEST_F(WidgetUpdaterUnit, UsesTheLiveBucketOffsetToFindAndUpdateTheWidget) {
  auto& host{Host()};

  const auto kResult{host.updater.Update(host.window.get())};

  ASSERT_TRUE(kResult.has_value());
  EXPECT_EQ(host.widget.updateCalls, 1);
}

TEST_F(WidgetUpdaterUnit, UsesTheCurrentMapperAndHashDataForEveryUpdate) {
  auto& host{Host()};
  ASSERT_TRUE(host.updater.Update(host.window.get()).has_value());
  qt::Widget otherWidget{};
  qt::WidgetHashNode otherNode{.windowId = host.WindowId(),
                               .widget = &otherWidget};
  qt::WidgetHashSpan otherSpan{};
  otherSpan.offsets.fill(qt::kWidgetHashUnusedEntry);
  otherSpan.offsets.back() = 0;
  otherSpan.entries = &otherNode;
  otherSpan.allocated = 1;
  qt::WidgetHashData otherData{host.data};
  otherData.spans = &otherSpan;
  qt::WidgetMapper otherMapper{&otherData};
  host.currentMapper = &otherMapper;

  const auto kResult{host.updater.Update(host.window.get())};

  ASSERT_TRUE(kResult.has_value());
  EXPECT_EQ(host.widget.updateCalls, 1);
  EXPECT_EQ(otherWidget.updateCalls, 1);
}

TEST_F(WidgetUpdaterUnit, FindsAWidgetInALaterSpan) {
  auto& host{Host()};
  host.spans.front().offsets.fill(qt::kWidgetHashUnusedEntry);
  host.spans.back().offsets.back() = 1;
  host.spans.back().entries = host.entries.data();
  host.spans.back().allocated = 2;
  host.data.numBuckets = host.spans.size() * qt::kWidgetHashSpanBucketCount;

  const auto kResult{host.updater.Update(host.window.get())};

  ASSERT_TRUE(kResult.has_value());
  EXPECT_EQ(host.widget.updateCalls, 1);
}

TEST_F(WidgetUpdaterUnit, DoesNotVisitSpansOutsideTheHostBucketCount) {
  auto& host{Host()};
  host.spans.front().offsets.fill(qt::kWidgetHashUnusedEntry);
  host.spans.back().offsets[0] = 1;
  host.spans.back().entries = host.entries.data();
  host.spans.back().allocated = 2;

  host.ExpectFailure(ERROR_NOT_FOUND);
}

TEST_F(WidgetUpdaterUnit, IgnoresAllocatedEntriesWithoutALiveBucket) {
  auto& host{Host()};
  host.spans.front().offsets.fill(qt::kWidgetHashUnusedEntry);

  host.ExpectFailure(ERROR_NOT_FOUND);
}

TEST_F(WidgetUpdaterUnit, RejectsAMissingMapper) {
  auto& host{Host()};
  host.currentMapper = nullptr;

  host.ExpectFailure(ERROR_NOT_FOUND);
}

TEST_F(WidgetUpdaterUnit, RejectsAMapperWithoutHashData) {
  auto& host{Host()};
  host.mapper.data = nullptr;

  host.ExpectFailure(ERROR_NOT_FOUND);
}

TEST_F(WidgetUpdaterUnit, RejectsAnEmptyWidgetMap) {
  auto& host{Host()};
  host.data.size = 0;
  host.data.spans = nullptr;

  host.ExpectFailure(ERROR_NOT_FOUND);
}

TEST_F(WidgetUpdaterUnit, RejectsAWidgetMapWithoutSpans) {
  auto& host{Host()};
  host.data.spans = nullptr;

  host.ExpectFailure(ERROR_INVALID_DATA);
}

TEST_F(WidgetUpdaterUnit, RejectsAWidgetMapWithoutBuckets) {
  auto& host{Host()};
  host.data.numBuckets = 0;

  host.ExpectFailure(ERROR_INVALID_DATA);
}

TEST_F(WidgetUpdaterUnit, RejectsABucketCountSmallerThanOneSpan) {
  auto& host{Host()};
  host.data.numBuckets = qt::kWidgetHashSpanBucketCount / 2;

  host.ExpectFailure(ERROR_INVALID_DATA);
}

TEST_F(WidgetUpdaterUnit, RejectsANonPowerOfTwoBucketCount) {
  auto& host{Host()};
  constexpr std::size_t kNonPowerOfTwoSpans{3};
  host.data.numBuckets = qt::kWidgetHashSpanBucketCount * kNonPowerOfTwoSpans;

  host.ExpectFailure(ERROR_INVALID_DATA);
}

TEST_F(WidgetUpdaterUnit, RejectsASizeLargerThanTheBucketCount) {
  auto& host{Host()};
  host.data.size = host.data.numBuckets + 1;

  host.ExpectFailure(ERROR_INVALID_DATA);
}

TEST_F(WidgetUpdaterUnit, RejectsALiveBucketWithoutEntryStorage) {
  auto& host{Host()};
  host.spans.front().entries = nullptr;

  host.ExpectFailure(ERROR_INVALID_DATA);
}

TEST_F(WidgetUpdaterUnit, RejectsAnEntryOffsetOutsideAllocatedStorage) {
  auto& host{Host()};
  host.spans.front().allocated = 1;

  host.ExpectFailure(ERROR_INVALID_DATA);
}

TEST_F(WidgetUpdaterUnit, RejectsAnAllocationLargerThanASpan) {
  auto& host{Host()};
  host.spans.front().allocated = qt::kWidgetHashSpanBucketCount + 1;

  host.ExpectFailure(ERROR_INVALID_DATA);
}

TEST_F(WidgetUpdaterUnit, RejectsANodeForAnotherWindow) {
  auto& host{Host()};
  ++host.entries.back().windowId;

  host.ExpectFailure(ERROR_NOT_FOUND);
}

TEST_F(WidgetUpdaterUnit, RejectsANodeWithoutAWidget) {
  auto& host{Host()};
  host.entries.back().widget = nullptr;

  host.ExpectFailure(ERROR_NOT_FOUND);
}

TEST_F(WidgetUpdaterUnit, InvalidWindowPreservesTheWin32ThreadQueryError) {
  auto& host{Host()};
  // A deliberately unreadable mapper proves the thread query precedes lookup.
  host.currentMapper =
      rgf::core::PointerCast<qt::WidgetMapper*>(std::uintptr_t{1});
  SetLastError(ERROR_SUCCESS);

  const auto kResult{host.updater.Update(nullptr)};
  const std::error_code kError{rgf::win::LastError()};

  ASSERT_FALSE(kResult.has_value());
  EXPECT_NE(kError.value(), 0);
  EXPECT_EQ(kResult.error(), kError);
  EXPECT_EQ(&kResult.error().category(), &std::system_category());
  EXPECT_EQ(host.widget.updateCalls, 0);
}

TEST_F(WidgetUpdaterUnit, RejectsAnotherThreadBeforeAccessingTheMapper) {
  auto& host{Host()};
  // A deliberately unreadable mapper makes the ordering observable.
  host.currentMapper =
      rgf::core::PointerCast<qt::WidgetMapper*>(std::uintptr_t{1});
  std::expected<void, std::error_code> result{};
  {
    const std::jthread kWorker{
        [&host, &result] { result = host.updater.Update(host.window.get()); }};
  }

  ASSERT_FALSE(result.has_value());
  const std::error_code kExpected{ERROR_INVALID_THREAD_ID,
                                  std::system_category()};
  EXPECT_EQ(result.error(), kExpected);
  EXPECT_EQ(host.widget.updateCalls, 0);
}
