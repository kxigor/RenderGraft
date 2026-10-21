#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <rgf/core/cast.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_canvas.hpp>
#include <rgf/qt/raster_image_hook.hpp>
#include <rgf/qt/raster_text_hook.hpp>
#include <span>
#include <vector>

namespace rgf::qt::internal {

struct Painter {
  PaintEngine* engine;
};

}  // namespace rgf::qt::internal

namespace {

namespace qt = rgf::qt::internal;
using rgf::qt::RasterCanvas;
using rgf::qt::RasterImageHook;
using rgf::qt::RasterTextHook;
using Table = std::array<void*, qt::kRasterVirtualCount + 1>;

enum class Event : std::uint8_t { Text, Fill };

constexpr double kImagePixelRatio{2.0};

constexpr qt::Color kColorA{.spec = 1,
                            .alpha = 0x4321,
                            .red = 0x1234,
                            .green = 0x5678,
                            .blue = 0x9abc,
                            .padding = 0};
constexpr qt::Color kColorB{.spec = 1,
                            .alpha = 0xffff,
                            .red = 0xabcd,
                            .green = 0,
                            .blue = 0xffff,
                            .padding = 0};

struct ProbeEngine : qt::PaintEngine {
  explicit ProbeEngine(void** table) : PaintEngine{table} {}

  void Draw(const qt::PointF& position, const qt::TextItem& item) {
    rgf::core::PointerCast<qt::DrawTextItemFn>(vtable[qt::kDrawTextItemSlot])(
        this, position, item);
  }

  std::vector<Event> events;
  const qt::PointF* originalPosition{nullptr};
  const qt::TextItem* originalItem{nullptr};
  qt::RectF rectangle{};
  std::vector<qt::Color> colors;
};

void OriginalDraw(qt::PaintEngine* engine, const qt::PointF& position,
                  const qt::TextItem& item) {
  // Test callbacks only receive ProbeEngine instances.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* probe{static_cast<ProbeEngine*>(engine)};
  probe->originalPosition = &position;
  probe->originalItem = &item;
  probe->events.push_back(Event::Text);
}

void FillColor(qt::PaintEngine* engine, const qt::RectF& rectangle,
               const qt::Color& color) {
  // Test callbacks only receive ProbeEngine instances.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* probe{static_cast<ProbeEngine*>(engine)};
  probe->rectangle = rectangle;
  probe->colors.push_back(color);
  probe->events.push_back(Event::Fill);
}

void AfterTextA(const RasterCanvas& canvas, const qt::RectF& bounds) noexcept {
  canvas.FillRect(bounds, kColorA);
}

void AfterTextB(const RasterCanvas& canvas, const qt::RectF& bounds) noexcept {
  canvas.FillRect(bounds, kColorB);
}

void MaskImage(const RasterCanvas& canvas, const qt::RectF& bounds) noexcept {
  canvas.FillRect(bounds, kColorB);
}

double PixmapRatio([[maybe_unused]] const qt::PlatformPixmap* pixmap) {
  return kImagePixelRatio;
}

void DrawImages(ProbeEngine& engine) {
  constexpr std::int32_t kPixelWidth{80};
  constexpr std::int32_t kPixelHeight{40};
  constexpr std::int32_t kPixelDepth{32};
  const qt::RectF kBounds{.x = 2.0, .y = 1.0, .width = 2.0, .height = 1.0};
  const qt::PointF kPosition{.x = 2.0, .y = 1.0};
  qt::ImageData imageData{.referenceCount = 1,
                          .width = kPixelWidth,
                          .height = kPixelHeight,
                          .depth = kPixelDepth,
                          .byteCount = 0,
                          .devicePixelRatio = kImagePixelRatio};
  const qt::Image kImage{.device = {}, .data = &imageData};
  std::array<void*, qt::kPixmapDevicePixelRatioSlot + 1> pixmapTable{};
  pixmapTable[qt::kPixmapDevicePixelRatioSlot] =
      rgf::core::PointerCast<void*>(&PixmapRatio);
  qt::PlatformPixmap pixmapData{.vtable = pixmapTable.data(),
                                .referenceCount = 1,
                                .width = kPixelWidth,
                                .height = kPixelHeight,
                                .depth = kPixelDepth};
  const qt::Pixmap kPixmap{.device = {}, .data = &pixmapData};

  rgf::core::PointerCast<qt::DrawPixmapRectFn>(
      engine.vtable[qt::kDrawPixmapRectSlot])(&engine, kBounds, kPixmap,
                                              kBounds);
  rgf::core::PointerCast<qt::DrawTiledPixmapFn>(
      engine.vtable[qt::kDrawTiledPixmapSlot])(&engine, kBounds, kPixmap,
                                               kPosition);
  rgf::core::PointerCast<qt::DrawImageRectFn>(
      engine.vtable[qt::kDrawImageRectSlot])(&engine, kBounds, kImage, kBounds,
                                             0);
  rgf::core::PointerCast<qt::DrawPixmapPointFn>(
      engine.vtable[qt::kDrawPixmapPointSlot])(&engine, kPosition, kPixmap);
  rgf::core::PointerCast<qt::DrawImagePointFn>(
      engine.vtable[qt::kDrawImagePointSlot])(&engine, kPosition, kImage);
}

[[nodiscard]] Table CopyTable(void** table) {
  Table result{};
  std::ranges::copy(std::span{table - 1, result.size()}, result.begin());
  return result;
}

void UnusedVirtual() {}

[[nodiscard]] Table MakeTable() {
  Table table{};
  table.fill(rgf::core::PointerCast<void*>(&UnusedVirtual));
  table[qt::kDrawTextItemSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalDraw);
  table[qt::kFillColorSlot + 1] = rgf::core::PointerCast<void*>(&FillColor);
  return table;
}

[[nodiscard]] qt::PaintEngine* GetPaintEngine(const qt::Painter* painter) {
  return painter->engine;
}

class RasterTextHookUnit : public ::testing::Test {
 protected:
  Table table_{MakeTable()};
  ProbeEngine engine_{table_.data() + 1};
  qt::Painter painter_{&engine_};
  const qt::PointF kPosition_{.x = 20.0, .y = 30.0};
  const qt::TextItem kItem_{.descent = 224, .ascent = 848, .width = 5616};
};

}  // namespace

TEST_F(RasterTextHookUnit, ForwardsOriginalArgumentsBeforeCallingAfterText) {
  const RasterTextHook kHook{painter_, &GetPaintEngine, table_.data() + 1,
                             &AfterTextA};

  engine_.Draw(kPosition_, kItem_);

  EXPECT_EQ(engine_.originalPosition, &kPosition_);
  EXPECT_EQ(engine_.originalItem, &kItem_);
  EXPECT_EQ(engine_.events, (std::vector{Event::Text, Event::Fill}));
  EXPECT_DOUBLE_EQ(engine_.rectangle.x, kPosition_.x);
  EXPECT_DOUBLE_EQ(engine_.rectangle.y, 16.75);
}

TEST_F(RasterTextHookUnit, CanvasForwardsTheRequestedRectangleAndColor) {
  const RasterTextHook kHook{painter_, &GetPaintEngine, table_.data() + 1,
                             &AfterTextA};

  engine_.Draw(kPosition_, kItem_);

  EXPECT_DOUBLE_EQ(engine_.rectangle.x, kPosition_.x);
  EXPECT_DOUBLE_EQ(engine_.rectangle.y, 16.75);
  EXPECT_DOUBLE_EQ(engine_.rectangle.width, 87.75);
  EXPECT_DOUBLE_EQ(engine_.rectangle.height, 16.75);
  ASSERT_EQ(engine_.colors.size(), 1);
  const auto& color{engine_.colors.front()};
  EXPECT_EQ(color.spec, kColorA.spec);
  EXPECT_EQ(color.alpha, kColorA.alpha);
  EXPECT_EQ(color.red, kColorA.red);
  EXPECT_EQ(color.green, kColorA.green);
  EXPECT_EQ(color.blue, kColorA.blue);
  EXPECT_EQ(color.padding, kColorA.padding);
}

TEST_F(RasterTextHookUnit,
       BoundsFollowEachTextRunsMetricsAndFractionalBaseline) {
  const RasterTextHook kHook{painter_, &GetPaintEngine, table_.data() + 1,
                             &AfterTextA};

  engine_.Draw(kPosition_, kItem_);

  EXPECT_DOUBLE_EQ(engine_.rectangle.x, 20.0);
  EXPECT_DOUBLE_EQ(engine_.rectangle.y, 16.75);
  EXPECT_DOUBLE_EQ(engine_.rectangle.width, 87.75);
  EXPECT_DOUBLE_EQ(engine_.rectangle.height, 16.75);

  const qt::PointF kOtherPosition{.x = -7.125, .y = 101.5};
  const qt::TextItem kOtherItem{.descent = 112, .ascent = 408, .width = 1200};
  engine_.Draw(kOtherPosition, kOtherItem);

  EXPECT_EQ(engine_.originalPosition, &kOtherPosition);
  EXPECT_EQ(engine_.originalItem, &kOtherItem);
  EXPECT_DOUBLE_EQ(engine_.rectangle.x, -7.125);
  EXPECT_DOUBLE_EQ(engine_.rectangle.y, 95.125);
  EXPECT_DOUBLE_EQ(engine_.rectangle.width, 18.75);
  EXPECT_DOUBLE_EQ(engine_.rectangle.height, 8.125);
  EXPECT_EQ(engine_.events,
            (std::vector{Event::Text, Event::Fill, Event::Text, Event::Fill}));
}

TEST_F(RasterTextHookUnit,
       PreservesRttiAndOtherVirtualMethodsWithoutPatchingTable) {
  const Table kOriginal{table_};
  void** const kOriginalVtable{engine_.vtable};
  const RasterTextHook kHook{painter_, &GetPaintEngine, kOriginalVtable,
                             &AfterTextA};

  ASSERT_NE(engine_.vtable, kOriginalVtable);
  Table actual{};
  std::ranges::copy(std::span{engine_.vtable - 1, table_.size()},
                    actual.begin());
  EXPECT_NE(actual[qt::kDrawTextItemSlot + 1],
            kOriginal[qt::kDrawTextItemSlot + 1]);
  actual[qt::kDrawTextItemSlot + 1] = kOriginal[qt::kDrawTextItemSlot + 1];
  EXPECT_EQ(actual, kOriginal);
  EXPECT_EQ(table_, kOriginal);
}

TEST_F(RasterTextHookUnit, RestoresTheEngineAndStopsCallbacksOnDestruction) {
  void** const kOriginalVtable{engine_.vtable};
  {
    const RasterTextHook kHook{painter_, &GetPaintEngine, kOriginalVtable,
                               &AfterTextA};
    engine_.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  engine_.Draw(kPosition_, kItem_);
  EXPECT_EQ(engine_.events,
            (std::vector{Event::Text, Event::Fill, Event::Text}));
}

TEST_F(RasterTextHookUnit, LeavesOtherObjectsSharingTheVtableUnchanged) {
  ProbeEngine other{engine_.vtable};
  void** const kOriginalVtable{engine_.vtable};
  const RasterTextHook kHook{painter_, &GetPaintEngine, kOriginalVtable,
                             &AfterTextA};

  other.Draw(kPosition_, kItem_);

  EXPECT_EQ(other.vtable, kOriginalVtable);
  EXPECT_EQ(other.events, (std::vector{Event::Text}));
}

TEST_F(RasterTextHookUnit, SkipsAnEngineWithAnotherVtable) {
  Table other{MakeTable()};
  void* const* const kOriginalVtable{engine_.vtable};
  {
    const RasterTextHook kHook{painter_, &GetPaintEngine, other.data() + 1,
                               &AfterTextA};
    EXPECT_EQ(engine_.vtable, kOriginalVtable);
    engine_.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  EXPECT_EQ(engine_.events, (std::vector{Event::Text}));
}

TEST_F(RasterTextHookUnit, AcceptsAPainterWithoutAnActiveEngine) {
  qt::Painter painter{nullptr};
  void** const kOriginalVtable{engine_.vtable};
  {
    const RasterTextHook kHook{painter, &GetPaintEngine, kOriginalVtable,
                               &AfterTextA};
    engine_.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  EXPECT_EQ(engine_.events, (std::vector{Event::Text}));
}

TEST_F(RasterTextHookUnit, NestedHooksOnOneEngineKeepTheOuterCallbackAndTable) {
  void** const kOriginalVtable{engine_.vtable};
  {
    const RasterTextHook kOuter{painter_, &GetPaintEngine, kOriginalVtable,
                                &AfterTextA};
    void* const* const kOuterVtable{engine_.vtable};
    {
      const RasterTextHook kInner{painter_, &GetPaintEngine, kOriginalVtable,
                                  &AfterTextB};
      EXPECT_EQ(engine_.vtable, kOuterVtable);
      engine_.Draw(kPosition_, kItem_);
    }
    EXPECT_EQ(engine_.vtable, kOuterVtable);
    engine_.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  EXPECT_EQ(engine_.events,
            (std::vector{Event::Text, Event::Fill, Event::Text, Event::Fill}));
  ASSERT_EQ(engine_.colors.size(), 2);
  EXPECT_EQ(engine_.colors[0].red, kColorA.red);
  EXPECT_EQ(engine_.colors[1].red, kColorA.red);
}

TEST_F(RasterTextHookUnit,
       NestedEnginesRouteTheirOwnCallbacksAndRestoreTables) {
  void** const kOriginalVtable{engine_.vtable};
  ProbeEngine other{kOriginalVtable};
  qt::Painter otherPainter{&other};
  {
    const RasterTextHook kOuter{painter_, &GetPaintEngine, kOriginalVtable,
                                &AfterTextA};
    void* const* const kOuterVtable{engine_.vtable};
    {
      const RasterTextHook kInner{otherPainter, &GetPaintEngine,
                                  kOriginalVtable, &AfterTextB};
      EXPECT_NE(other.vtable, engine_.vtable);
      other.Draw(kPosition_, kItem_);
      engine_.Draw(kPosition_, kItem_);
    }
    EXPECT_EQ(other.vtable, kOriginalVtable);
    EXPECT_EQ(engine_.vtable, kOuterVtable);
    engine_.Draw(kPosition_, kItem_);
    other.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  EXPECT_EQ(other.vtable, kOriginalVtable);
  EXPECT_EQ(other.events, (std::vector{Event::Text, Event::Fill, Event::Text}));
  EXPECT_EQ(engine_.events,
            (std::vector{Event::Text, Event::Fill, Event::Text, Event::Fill}));
  ASSERT_EQ(engine_.colors.size(), 2);
  ASSERT_EQ(other.colors.size(), 1);
  EXPECT_EQ(engine_.colors[0].red, kColorA.red);
  EXPECT_EQ(engine_.colors[1].red, kColorA.red);
  EXPECT_EQ(other.colors[0].red, kColorB.red);
}

TEST_F(RasterTextHookUnit, PreservesImageDetoursAndRttiInsideTheTextScope) {
  const Table kOriginal{table_};
  const RasterImageHook kImages{&engine_, table_.data() + 1, &MaskImage};
  void** const kImageVtable{engine_.vtable};
  const Table kImageTable{CopyTable(kImageVtable)};
  const RasterTextHook kText{painter_, &GetPaintEngine, table_.data() + 1,
                             &AfterTextA};

  ASSERT_NE(engine_.vtable, kImageVtable);
  Table actual{CopyTable(engine_.vtable)};
  EXPECT_NE(actual[qt::kDrawTextItemSlot + 1],
            kImageTable[qt::kDrawTextItemSlot + 1]);
  actual[qt::kDrawTextItemSlot + 1] = kImageTable[qt::kDrawTextItemSlot + 1];
  EXPECT_EQ(actual, kImageTable);
  EXPECT_EQ(table_, kOriginal);
}

TEST_F(RasterTextHookUnit, KeepsAllImageOperationsMaskedInsideATextScope) {
  const RasterImageHook kImages{&engine_, table_.data() + 1, &MaskImage};
  const RasterTextHook kText{painter_, &GetPaintEngine, table_.data() + 1,
                             &AfterTextA};

  engine_.Draw(kPosition_, kItem_);
  DrawImages(engine_);

  EXPECT_EQ(engine_.originalPosition, &kPosition_);
  EXPECT_EQ(engine_.originalItem, &kItem_);
  EXPECT_EQ(engine_.events,
            (std::vector{Event::Text, Event::Fill, Event::Fill, Event::Fill,
                         Event::Fill, Event::Fill, Event::Fill}));
  constexpr std::size_t kColorCount{6};
  ASSERT_EQ(engine_.colors.size(), kColorCount);
  EXPECT_EQ(engine_.colors.front().red, kColorA.red);
  EXPECT_TRUE(std::ranges::all_of(
      std::span{engine_.colors}.subspan(1),
      [](const qt::Color& color) { return color.red == kColorB.red; }));
}

TEST_F(RasterTextHookUnit, RestoresTheImageScopeAndRecognizesItsExactEngine) {
  void** const kOriginalVtable{engine_.vtable};
  EXPECT_FALSE(RasterImageHook::IsInstalledOn(engine_));
  {
    const RasterImageHook kImages{&engine_, kOriginalVtable, &MaskImage};
    void** const kImageVtable{engine_.vtable};
    const ProbeEngine kOther{kImageVtable};
    EXPECT_TRUE(RasterImageHook::IsInstalledOn(engine_));
    EXPECT_FALSE(RasterImageHook::IsInstalledOn(kOther));
    {
      const RasterTextHook kText{painter_, &GetPaintEngine, kOriginalVtable,
                                 &AfterTextA};
      EXPECT_FALSE(RasterImageHook::IsInstalledOn(engine_));
      engine_.Draw(kPosition_, kItem_);
    }
    EXPECT_EQ(engine_.vtable, kImageVtable);
    EXPECT_TRUE(RasterImageHook::IsInstalledOn(engine_));
    DrawImages(engine_);
  }
  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  EXPECT_FALSE(RasterImageHook::IsInstalledOn(engine_));
}

TEST_F(RasterTextHookUnit, NestedTextScopesWithinImagesKeepTheOuterCallback) {
  const RasterImageHook kImages{&engine_, table_.data() + 1, &MaskImage};
  void* const* const kImageVtable{engine_.vtable};
  {
    const RasterTextHook kOuter{painter_, &GetPaintEngine, table_.data() + 1,
                                &AfterTextA};
    void* const* const kTextVtable{engine_.vtable};
    {
      const RasterTextHook kInner{painter_, &GetPaintEngine, table_.data() + 1,
                                  &AfterTextB};
      EXPECT_EQ(engine_.vtable, kTextVtable);
      engine_.Draw(kPosition_, kItem_);
    }
    EXPECT_EQ(engine_.vtable, kTextVtable);
    DrawImages(engine_);
  }
  EXPECT_EQ(engine_.vtable, kImageVtable);
  EXPECT_EQ(engine_.events.front(), Event::Text);
  constexpr std::size_t kColorCount{6};
  ASSERT_EQ(engine_.colors.size(), kColorCount);
  EXPECT_EQ(engine_.colors.front().red, kColorA.red);
}
