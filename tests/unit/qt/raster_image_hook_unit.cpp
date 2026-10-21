#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <rgf/core/cast.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_image_hook.hpp>
#include <span>
#include <vector>

namespace {

namespace qt = rgf::qt::internal;
using rgf::qt::RasterCanvas;
using rgf::qt::RasterImageHook;
using Table = std::array<void*, qt::kRasterVirtualCount + 1>;

constexpr qt::Color kColorA{.spec = 1,
                            .alpha = 0xffff,
                            .red = 0xabcd,
                            .green = 0,
                            .blue = 0xffff,
                            .padding = 0};
constexpr qt::Color kColorB{.spec = 1,
                            .alpha = 0xffff,
                            .red = 0x1234,
                            .green = 0,
                            .blue = 0xffff,
                            .padding = 0};
constexpr std::int32_t kImageFlags{0x1234};
constexpr std::int32_t kPixelWidth{150};
constexpr std::int32_t kPixelHeight{90};
constexpr std::int32_t kPixelDepth{32};
constexpr double kPixelRatio{2.5};

struct ProbeEngine : qt::PaintEngine {
  explicit ProbeEngine(void** table) : PaintEngine{table} {}

  unsigned int originalCalls{};
  const qt::RectF* originalRectangle{nullptr};
  const qt::Image* originalImage{nullptr};
  const qt::Pixmap* originalPixmap{nullptr};
  const qt::PointF* originalPosition{nullptr};
  const qt::PointF* originalTileOrigin{nullptr};
  const qt::RectF* originalSource{nullptr};
  std::int32_t originalFlags{};
  std::vector<qt::RectF> masks;
  std::vector<qt::Color> colors;
};

[[nodiscard]] ProbeEngine& Probe(qt::PaintEngine* engine) {
  // All synthetic vtables in these tests are owned by ProbeEngine objects.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  return *static_cast<ProbeEngine*>(engine);
}

void OriginalPixmapRect(qt::PaintEngine* engine, const qt::RectF& rectangle,
                        const qt::Pixmap& pixmap, const qt::RectF& source) {
  auto& probe{Probe(engine)};
  ++probe.originalCalls;
  probe.originalRectangle = &rectangle;
  probe.originalPixmap = &pixmap;
  probe.originalSource = &source;
}

void OriginalTiledPixmap(qt::PaintEngine* engine, const qt::RectF& rectangle,
                         const qt::Pixmap& pixmap, const qt::PointF& source) {
  auto& probe{Probe(engine)};
  ++probe.originalCalls;
  probe.originalRectangle = &rectangle;
  probe.originalPixmap = &pixmap;
  probe.originalTileOrigin = &source;
}

void OriginalImageRect(qt::PaintEngine* engine, const qt::RectF& rectangle,
                       const qt::Image& image, const qt::RectF& source,
                       std::int32_t flags) {
  auto& probe{Probe(engine)};
  ++probe.originalCalls;
  probe.originalRectangle = &rectangle;
  probe.originalImage = &image;
  probe.originalSource = &source;
  probe.originalFlags = flags;
}

void OriginalPixmapPoint(qt::PaintEngine* engine, const qt::PointF& position,
                         const qt::Pixmap& pixmap) {
  auto& probe{Probe(engine)};
  ++probe.originalCalls;
  probe.originalPosition = &position;
  probe.originalPixmap = &pixmap;
}

void OriginalImagePoint(qt::PaintEngine* engine, const qt::PointF& position,
                        const qt::Image& image) {
  auto& probe{Probe(engine)};
  ++probe.originalCalls;
  probe.originalPosition = &position;
  probe.originalImage = &image;
}

void FillColor(qt::PaintEngine* engine, const qt::RectF& bounds,
               const qt::Color& color) {
  auto& probe{Probe(engine)};
  probe.masks.push_back(bounds);
  probe.colors.push_back(color);
}

bool MaskA(const RasterCanvas& canvas, const qt::RectF& bounds) noexcept {
  canvas.FillRect(bounds, kColorA);
  return true;
}

bool MaskB(const RasterCanvas& canvas, const qt::RectF& bounds) noexcept {
  canvas.FillRect(bounds, kColorB);
  return true;
}

bool PassThrough([[maybe_unused]] const RasterCanvas& canvas,
                 [[maybe_unused]] const qt::RectF& bounds) noexcept {
  return false;
}

bool MaskPositiveX(const RasterCanvas& canvas,
                   const qt::RectF& bounds) noexcept {
  if (bounds.x < 0.0) {
    return false;
  }
  return MaskA(canvas, bounds);
}

void UnusedVirtual() {}

[[nodiscard]] Table MakeTable() {
  Table table{};
  table.fill(rgf::core::PointerCast<void*>(&UnusedVirtual));
  table[qt::kDrawPixmapRectSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalPixmapRect);
  table[qt::kDrawTiledPixmapSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalTiledPixmap);
  table[qt::kDrawImageRectSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalImageRect);
  table[qt::kDrawPixmapPointSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalPixmapPoint);
  table[qt::kDrawImagePointSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalImagePoint);
  table[qt::kFillColorSlot + 1] = rgf::core::PointerCast<void*>(&FillColor);
  return table;
}

struct ProbePixmap : qt::PlatformPixmap {
  explicit ProbePixmap(void** table)
      : PlatformPixmap{.vtable = table,
                       .referenceCount = 1,
                       .width = kPixelWidth,
                       .height = kPixelHeight,
                       .depth = kPixelDepth} {}

  double ratio{kPixelRatio};
  mutable unsigned int ratioCalls{};
};

[[nodiscard]] double DevicePixelRatio(const qt::PlatformPixmap* pixmap) {
  // The test platform getter is bound exclusively to ProbePixmap objects.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto* probe{static_cast<const ProbePixmap*>(pixmap)};
  ++probe->ratioCalls;
  return probe->ratio;
}

[[nodiscard]] std::array<void*, qt::kPixmapDevicePixelRatioSlot + 1>
MakePixmapTable() {
  std::array<void*, qt::kPixmapDevicePixelRatioSlot + 1> table{};
  table[qt::kPixmapDevicePixelRatioSlot] =
      rgf::core::PointerCast<void*>(&DevicePixelRatio);
  return table;
}

void DrawImageRect(ProbeEngine& engine, const qt::RectF& bounds,
                   const qt::Image& image, const qt::RectF& source) {
  rgf::core::PointerCast<qt::DrawImageRectFn>(
      engine.vtable[qt::kDrawImageRectSlot])(&engine, bounds, image, source,
                                             kImageFlags);
}

void DrawImagePoint(ProbeEngine& engine, const qt::PointF& position,
                    const qt::Image& image) {
  rgf::core::PointerCast<qt::DrawImagePointFn>(
      engine.vtable[qt::kDrawImagePointSlot])(&engine, position, image);
}

void DrawPixmapPoint(ProbeEngine& engine, const qt::PointF& position,
                     const qt::Pixmap& pixmap) {
  rgf::core::PointerCast<qt::DrawPixmapPointFn>(
      engine.vtable[qt::kDrawPixmapPointSlot])(&engine, position, pixmap);
}

void DrawAllRoutes(ProbeEngine& engine, const qt::RectF& bounds,
                   const qt::Image& image, const qt::Pixmap& pixmap,
                   const qt::PointF& position) {
  DrawImageRect(engine, bounds, image, bounds);
  rgf::core::PointerCast<qt::DrawPixmapRectFn>(
      engine.vtable[qt::kDrawPixmapRectSlot])(&engine, bounds, pixmap, bounds);
  rgf::core::PointerCast<qt::DrawTiledPixmapFn>(
      engine.vtable[qt::kDrawTiledPixmapSlot])(&engine, bounds, pixmap,
                                               position);
  DrawImagePoint(engine, position, image);
  DrawPixmapPoint(engine, position, pixmap);
}

void ExpectBounds(const qt::RectF& actual, const qt::RectF& expected) {
  EXPECT_DOUBLE_EQ(actual.x, expected.x);
  EXPECT_DOUBLE_EQ(actual.y, expected.y);
  EXPECT_DOUBLE_EQ(actual.width, expected.width);
  EXPECT_DOUBLE_EQ(actual.height, expected.height);
}

class RasterImageHookUnit : public ::testing::Test {
 protected:
  Table table_{MakeTable()};
  ProbeEngine engine_{table_.data() + 1};
  std::array<void*, qt::kPixmapDevicePixelRatioSlot + 1> pixmapTable_{
      MakePixmapTable()};
  ProbePixmap platform_{pixmapTable_.data()};
  qt::Pixmap pixmap_{.device = {}, .data = &platform_};
  qt::ImageData imageData_{.referenceCount = 1,
                           .width = kPixelWidth,
                           .height = kPixelHeight,
                           .depth = kPixelDepth,
                           .byteCount = 0,
                           .devicePixelRatio = kPixelRatio};
  qt::Image image_{.device = {}, .data = &imageData_};
  const qt::PointF kPosition_{.x = -7.125, .y = 30.5};
  const qt::RectF kBounds_{.x = 3.5, .y = -4.25, .width = 75.0, .height = 42.5};
  const qt::RectF kSource_{.x = 1.0, .y = 2.0, .width = 50.0, .height = 40.0};
};

}  // namespace

TEST_F(RasterImageHookUnit, ReplacesEveryImageRouteWithoutRenderingOriginals) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  ASSERT_TRUE(kHook.IsActive());

  DrawAllRoutes(engine_, kBounds_, image_, pixmap_, kPosition_);

  EXPECT_EQ(engine_.originalCalls, 0);
  ASSERT_EQ(engine_.masks.size(), 5);
  constexpr std::size_t kRectRouteCount{3};
  for (std::size_t index{}; index < kRectRouteCount; ++index) {
    ExpectBounds(engine_.masks[index], kBounds_);
  }
  const qt::RectF kPointBounds{
      .x = kPosition_.x, .y = kPosition_.y, .width = 60.0, .height = 36.0};
  ExpectBounds(engine_.masks.at(kRectRouteCount), kPointBounds);
  ExpectBounds(engine_.masks.at(kRectRouteCount + 1), kPointBounds);
  EXPECT_EQ(platform_.ratioCalls, 1);
}

TEST_F(RasterImageHookUnit, DeclinedImageRectPreservesArgumentsAndFlags) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &PassThrough};

  DrawImageRect(engine_, kBounds_, image_, kSource_);

  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_EQ(engine_.originalRectangle, &kBounds_);
  EXPECT_EQ(engine_.originalImage, &image_);
  EXPECT_EQ(engine_.originalSource, &kSource_);
  EXPECT_EQ(engine_.originalFlags, kImageFlags);
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit, DeclinedPixmapRectPreservesArguments) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &PassThrough};

  rgf::core::PointerCast<qt::DrawPixmapRectFn>(
      engine_.vtable[qt::kDrawPixmapRectSlot])(&engine_, kBounds_, pixmap_,
                                               kSource_);

  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_EQ(engine_.originalRectangle, &kBounds_);
  EXPECT_EQ(engine_.originalPixmap, &pixmap_);
  EXPECT_EQ(engine_.originalSource, &kSource_);
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit, DeclinedTiledPixmapPreservesArguments) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &PassThrough};

  rgf::core::PointerCast<qt::DrawTiledPixmapFn>(
      engine_.vtable[qt::kDrawTiledPixmapSlot])(&engine_, kBounds_, pixmap_,
                                                kPosition_);

  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_EQ(engine_.originalRectangle, &kBounds_);
  EXPECT_EQ(engine_.originalPixmap, &pixmap_);
  EXPECT_EQ(engine_.originalTileOrigin, &kPosition_);
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit, DeclinedImagePointPreservesArgumentsAndPixelRatio) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &PassThrough};

  DrawImagePoint(engine_, kPosition_, image_);

  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_EQ(engine_.originalPosition, &kPosition_);
  EXPECT_EQ(engine_.originalImage, &image_);
  EXPECT_DOUBLE_EQ(imageData_.devicePixelRatio, kPixelRatio);
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit,
       DeclinedPixmapPointPreservesArgumentsAndPixelRatio) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &PassThrough};

  DrawPixmapPoint(engine_, kPosition_, pixmap_);

  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_EQ(engine_.originalPosition, &kPosition_);
  EXPECT_EQ(engine_.originalPixmap, &pixmap_);
  EXPECT_DOUBLE_EQ(platform_.ratio, kPixelRatio);
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit, AppliesTheCallbackDecisionToEachOperation) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskPositiveX};
  qt::RectF declined{kBounds_};
  declined.x = -declined.x;

  DrawImageRect(engine_, kBounds_, image_, kSource_);
  DrawImageRect(engine_, declined, image_, kSource_);

  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_EQ(engine_.originalRectangle, &declined);
  ASSERT_EQ(engine_.masks.size(), 1);
  ExpectBounds(engine_.masks.front(), kBounds_);
}

TEST_F(RasterImageHookUnit, PointImageUsesItsCurrentDimensionsAndRatio) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  constexpr std::int32_t kWidth{160};
  constexpr std::int32_t kHeight{100};
  constexpr double kRatio{2.0};
  imageData_.width = kWidth;
  imageData_.height = kHeight;
  imageData_.devicePixelRatio = kRatio;

  DrawImagePoint(engine_, kPosition_, image_);

  ASSERT_EQ(engine_.masks.size(), 1);
  const qt::RectF kExpected{
      .x = kPosition_.x, .y = kPosition_.y, .width = 80.0, .height = 50.0};
  ExpectBounds(engine_.masks.front(), kExpected);
}

TEST_F(RasterImageHookUnit, PointPixmapUsesItsPlatformDimensionsAndRatio) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  constexpr std::int32_t kWidth{160};
  constexpr std::int32_t kHeight{100};
  constexpr double kRatio{2.0};
  platform_.width = kWidth;
  platform_.height = kHeight;
  platform_.ratio = kRatio;

  DrawPixmapPoint(engine_, kPosition_, pixmap_);

  ASSERT_EQ(engine_.masks.size(), 1);
  const qt::RectF kExpected{
      .x = kPosition_.x, .y = kPosition_.y, .width = 80.0, .height = 50.0};
  ExpectBounds(engine_.masks.front(), kExpected);
}

TEST_F(RasterImageHookUnit, NullPointImagesAndPixmapsDoNotReadDataOrDrawMasks) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  const qt::Image kImage{};
  const qt::Pixmap kPixmap{};

  DrawImagePoint(engine_, kPosition_, kImage);
  DrawPixmapPoint(engine_, kPosition_, kPixmap);

  EXPECT_TRUE(engine_.masks.empty());
  EXPECT_EQ(engine_.originalCalls, 2);
}

TEST_F(RasterImageHookUnit, InvalidPointRatiosDoNotProduceMaskGeometry) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  const std::array kRatios{0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()};
  for (const double kRatio : kRatios) {
    imageData_.devicePixelRatio = kRatio;
    platform_.ratio = kRatio;
    DrawImagePoint(engine_, kPosition_, image_);
    DrawPixmapPoint(engine_, kPosition_, pixmap_);
  }

  EXPECT_TRUE(engine_.masks.empty());
  EXPECT_EQ(engine_.originalCalls, kRatios.size() * 2);
}

TEST_F(RasterImageHookUnit, EmptyDestinationDoesNotCallTheMaskCallback) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  const qt::RectF kEmpty{.x = 0.0, .y = 0.0, .width = 0.0, .height = 20.0};

  DrawImageRect(engine_, kEmpty, image_, kSource_);

  EXPECT_TRUE(engine_.masks.empty());
  EXPECT_EQ(engine_.originalCalls, 1);
}

TEST_F(RasterImageHookUnit, NonFiniteDestinationPassesThroughWithoutAMask) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  constexpr double kNan{std::numeric_limits<double>::quiet_NaN()};
  constexpr double kInfinity{std::numeric_limits<double>::infinity()};
  const std::array kDestinations{
      qt::RectF{.x = kNan, .y = 0.0, .width = 1.0, .height = 1.0},
      qt::RectF{.x = 0.0, .y = kInfinity, .width = 1.0, .height = 1.0},
      qt::RectF{.x = 0.0, .y = 0.0, .width = kInfinity, .height = 1.0},
      qt::RectF{.x = 0.0, .y = 0.0, .width = 1.0, .height = kNan}};

  for (const auto& destination : kDestinations) {
    DrawImageRect(engine_, destination, image_, kSource_);
  }

  EXPECT_EQ(engine_.originalCalls, kDestinations.size());
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit, NonFinitePointPositionPreservesTheOriginalCall) {
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  const qt::PointF kPosition{.x = std::numeric_limits<double>::quiet_NaN(),
                             .y = kPosition_.y};

  DrawImagePoint(engine_, kPosition, image_);

  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_EQ(engine_.originalPosition, &kPosition);
  EXPECT_EQ(engine_.originalImage, &image_);
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit, RestoresOriginalArgumentsAndFlagsAfterItsScope) {
  void** const kOriginalTable{engine_.vtable};
  {
    const RasterImageHook kHook{&engine_, kOriginalTable, &MaskA};
    DrawImageRect(engine_, kBounds_, image_, kSource_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalTable);
  DrawImageRect(engine_, kBounds_, image_, kSource_);
  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_EQ(engine_.originalRectangle, &kBounds_);
  EXPECT_EQ(engine_.originalImage, &image_);
  EXPECT_EQ(engine_.originalSource, &kSource_);
  EXPECT_EQ(engine_.originalFlags, kImageFlags);
  EXPECT_EQ(engine_.masks.size(), 1);
}

TEST_F(RasterImageHookUnit, PreservesRttiOtherVirtualsAndTheSharedTable) {
  const Table kOriginal{table_};
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};
  ASSERT_NE(engine_.vtable, table_.data() + 1);
  Table actual{};
  std::ranges::copy(std::span{engine_.vtable - 1, actual.size()},
                    actual.begin());
  const std::array kSlots{qt::kDrawPixmapRectSlot, qt::kDrawTiledPixmapSlot,
                          qt::kDrawImageRectSlot, qt::kDrawPixmapPointSlot,
                          qt::kDrawImagePointSlot};
  for (const auto kSlot : kSlots) {
    EXPECT_NE(actual.at(kSlot + 1), kOriginal.at(kSlot + 1));
    actual.at(kSlot + 1) = kOriginal.at(kSlot + 1);
  }
  EXPECT_EQ(actual, kOriginal);
  EXPECT_EQ(table_, kOriginal);
}

TEST_F(RasterImageHookUnit, LeavesOffscreenEnginesSharingTheTableUntouched) {
  ProbeEngine offscreen{engine_.vtable};
  const RasterImageHook kHook{&engine_, table_.data() + 1, &MaskA};

  DrawImageRect(offscreen, kBounds_, image_, kSource_);

  EXPECT_EQ(offscreen.vtable, table_.data() + 1);
  EXPECT_EQ(offscreen.originalCalls, 1);
  EXPECT_TRUE(offscreen.masks.empty());
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit, NullEngineOrBindingsRemainInactive) {
  const RasterImageHook kNoEngine{nullptr, nullptr, &MaskA};
  const RasterImageHook kNoTable{&engine_, nullptr, &MaskA};
  const RasterImageHook kNoCallback{&engine_, table_.data() + 1, nullptr};

  EXPECT_FALSE(kNoEngine.IsActive());
  EXPECT_FALSE(kNoTable.IsActive());
  EXPECT_FALSE(kNoCallback.IsActive());
  EXPECT_EQ(engine_.vtable, table_.data() + 1);
}

TEST_F(RasterImageHookUnit, AnotherEngineKindRemainsInactive) {
  Table other{MakeTable()};
  const RasterImageHook kHook{&engine_, other.data() + 1, &MaskA};

  EXPECT_FALSE(kHook.IsActive());
  DrawImageRect(engine_, kBounds_, image_, kSource_);
  EXPECT_EQ(engine_.originalCalls, 1);
  EXPECT_TRUE(engine_.masks.empty());
}

TEST_F(RasterImageHookUnit, NestedSameEngineUsesItsMaskThenRestoresTheOuter) {
  void** const kOriginalTable{engine_.vtable};
  {
    const RasterImageHook kOuter{&engine_, kOriginalTable, &MaskA};
    void* const* const kOuterTable{engine_.vtable};
    {
      const RasterImageHook kInner{&engine_, kOriginalTable, &MaskB};
      EXPECT_TRUE(kInner.IsActive());
      EXPECT_NE(engine_.vtable, kOuterTable);
      DrawImageRect(engine_, kBounds_, image_, kSource_);
    }
    EXPECT_EQ(engine_.vtable, kOuterTable);
    DrawImageRect(engine_, kBounds_, image_, kSource_);
  }
  EXPECT_EQ(engine_.vtable, kOriginalTable);
  ASSERT_EQ(engine_.colors.size(), 2);
  EXPECT_EQ(engine_.colors[0].red, kColorB.red);
  EXPECT_EQ(engine_.colors[1].red, kColorA.red);
}

TEST_F(RasterImageHookUnit, NestedEnginesKeepTheirOwnMasksAndRestoreTables) {
  void** const kOriginalTable{engine_.vtable};
  ProbeEngine other{kOriginalTable};
  {
    const RasterImageHook kOuter{&engine_, kOriginalTable, &MaskA};
    {
      const RasterImageHook kInner{&other, kOriginalTable, &MaskB};
      ASSERT_TRUE(kInner.IsActive());
      DrawImageRect(engine_, kBounds_, image_, kSource_);
      DrawImageRect(other, kBounds_, image_, kSource_);
    }
    EXPECT_EQ(other.vtable, kOriginalTable);
    DrawImageRect(engine_, kBounds_, image_, kSource_);
    DrawImageRect(other, kBounds_, image_, kSource_);
  }
  EXPECT_EQ(engine_.vtable, kOriginalTable);
  ASSERT_EQ(engine_.colors.size(), 2);
  ASSERT_EQ(other.colors.size(), 1);
  EXPECT_EQ(engine_.colors[0].red, kColorA.red);
  EXPECT_EQ(engine_.colors[1].red, kColorA.red);
  EXPECT_EQ(other.colors[0].red, kColorB.red);
  EXPECT_EQ(other.originalCalls, 1);
}

TEST_F(RasterImageHookUnit, DeclinedNestedScopeCallsHostInsteadOfTheOuterMask) {
  void** const kOriginalTable{engine_.vtable};
  const RasterImageHook kOuter{&engine_, kOriginalTable, &MaskA};
  void* const* const kOuterTable{engine_.vtable};
  {
    const RasterImageHook kInner{&engine_, kOriginalTable, &PassThrough};
    ASSERT_TRUE(kInner.IsActive());

    DrawAllRoutes(engine_, kBounds_, image_, pixmap_, kPosition_);

    constexpr unsigned int kRouteCount{5};
    EXPECT_EQ(engine_.originalCalls, kRouteCount);
    EXPECT_TRUE(engine_.masks.empty());
  }
  EXPECT_EQ(engine_.vtable, kOuterTable);
  DrawImageRect(engine_, kBounds_, image_, kSource_);
  ASSERT_EQ(engine_.colors.size(), 1);
  EXPECT_EQ(engine_.colors.front().red, kColorA.red);
}

TEST_F(RasterImageHookUnit, DeclinedInnerEngineKeepsTheOuterEngineMasked) {
  void** const kOriginalTable{engine_.vtable};
  ProbeEngine other{kOriginalTable};
  const RasterImageHook kOuter{&engine_, kOriginalTable, &MaskA};
  {
    const RasterImageHook kInner{&other, kOriginalTable, &PassThrough};

    DrawAllRoutes(other, kBounds_, image_, pixmap_, kPosition_);
    DrawImageRect(engine_, kBounds_, image_, kSource_);

    constexpr unsigned int kRouteCount{5};
    EXPECT_EQ(other.originalCalls, kRouteCount);
    EXPECT_TRUE(other.masks.empty());
    EXPECT_EQ(engine_.originalCalls, 0);
    EXPECT_EQ(engine_.masks.size(), 1);
  }
  EXPECT_EQ(other.vtable, kOriginalTable);
  DrawImageRect(other, kBounds_, image_, kSource_);
  constexpr unsigned int kCallsAfterRemoval{6};
  EXPECT_EQ(other.originalCalls, kCallsAfterRemoval);
}
