#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <rgf/core/cast.hpp>
#include <span>
#include <vector>

#include "internal/qt.hpp"
#include "raster_marker.hpp"

namespace rgf::telegram::internal {

struct Painter {
  PaintEngine* engine;
};

struct TextItem {};

}  // namespace rgf::telegram::internal

namespace {

namespace qt = rgf::telegram::internal;
using rgf::telegram::RasterMarker;
using Table = std::array<void*, qt::kRasterVirtualCount + 1>;

enum class Event : std::uint8_t { Text, Marker };

struct ProbeEngine : qt::PaintEngine {
  explicit ProbeEngine(void** table) : PaintEngine{table} {}

  void Draw(const qt::PointF& position, const qt::TextItem& item) {
    rgf::core::PointerCast<qt::DrawTextItem>(vtable[qt::kDrawTextItemSlot])(
        this, position, item);
  }

  std::vector<Event> events;
  const qt::PointF* originalPosition{nullptr};
  const qt::TextItem* originalItem{nullptr};
  qt::RectF markerRectangle{};
  qt::Color markerColor{};
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
  probe->markerRectangle = rectangle;
  probe->markerColor = color;
  probe->events.push_back(Event::Marker);
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

class RasterMarkerUnit : public ::testing::Test {
 protected:
  Table table_{MakeTable()};
  ProbeEngine engine_{table_.data() + 1};
  qt::Painter painter_{&engine_};
  const qt::PointF kPosition_{.x = 20.0, .y = 30.0};
  const qt::TextItem kItem_{};
};

}  // namespace

TEST_F(RasterMarkerUnit, ForwardsTheSameArgumentsThenDrawsAnOpaqueMarker) {
  const auto kTextCalls{RasterMarker::TextCalls()};
  const auto kMarkers{RasterMarker::MarkersDrawn()};
  const RasterMarker kMarker{painter_, &GetPaintEngine, table_.data() + 1};

  engine_.Draw(kPosition_, kItem_);

  EXPECT_EQ(engine_.originalPosition, &kPosition_);
  EXPECT_EQ(engine_.originalItem, &kItem_);
  EXPECT_EQ(engine_.events, (std::vector{Event::Text, Event::Marker}));
  EXPECT_DOUBLE_EQ(engine_.markerRectangle.x, kPosition_.x);
  EXPECT_DOUBLE_EQ(engine_.markerRectangle.y + engine_.markerRectangle.height,
                   kPosition_.y);
  EXPECT_GT(engine_.markerRectangle.width, 0.0);
  EXPECT_GT(engine_.markerRectangle.height, 0.0);
  EXPECT_EQ(engine_.markerColor.spec, 1);
  EXPECT_EQ(engine_.markerColor.alpha, 0xffff);
  EXPECT_EQ(engine_.markerColor.red, 0xffff);
  EXPECT_EQ(engine_.markerColor.green, 0);
  EXPECT_EQ(engine_.markerColor.blue, 0xffff);
  EXPECT_EQ(RasterMarker::TextCalls(), kTextCalls + 1);
  EXPECT_EQ(RasterMarker::MarkersDrawn(), kMarkers + 1);
}

TEST_F(RasterMarkerUnit,
       PreservesRttiAndOtherVirtualMethodsWithoutPatchingTable) {
  const Table kOriginal{table_};
  void** const kOriginalVtable{engine_.vtable};
  const RasterMarker kMarker{painter_, &GetPaintEngine, kOriginalVtable};

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

TEST_F(RasterMarkerUnit, RestoresTheEngineAndStopsDrawingMarkersOnDestruction) {
  void** const kOriginalVtable{engine_.vtable};
  {
    const RasterMarker kMarker{painter_, &GetPaintEngine, kOriginalVtable};
    engine_.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  const auto kMarkers{RasterMarker::MarkersDrawn()};
  engine_.Draw(kPosition_, kItem_);
  EXPECT_EQ(engine_.events,
            (std::vector{Event::Text, Event::Marker, Event::Text}));
  EXPECT_EQ(RasterMarker::MarkersDrawn(), kMarkers);
}

TEST_F(RasterMarkerUnit, LeavesOtherObjectsSharingTheVtableUnchanged) {
  ProbeEngine other{engine_.vtable};
  void** const kOriginalVtable{engine_.vtable};
  const RasterMarker kMarker{painter_, &GetPaintEngine, kOriginalVtable};

  other.Draw(kPosition_, kItem_);

  EXPECT_EQ(other.vtable, kOriginalVtable);
  EXPECT_EQ(other.events, (std::vector{Event::Text}));
}

TEST_F(RasterMarkerUnit, SkipsAnEngineWithAnotherVtable) {
  Table other{MakeTable()};
  void* const* const kOriginalVtable{engine_.vtable};
  const auto kTextCalls{RasterMarker::TextCalls()};
  {
    const RasterMarker kMarker{painter_, &GetPaintEngine, other.data() + 1};
    EXPECT_EQ(engine_.vtable, kOriginalVtable);
    engine_.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  EXPECT_EQ(engine_.events, (std::vector{Event::Text}));
  EXPECT_EQ(RasterMarker::TextCalls(), kTextCalls);
}

TEST_F(RasterMarkerUnit, AcceptsAPainterWithoutAnActiveEngine) {
  qt::Painter painter{nullptr};
  const auto kMarkers{RasterMarker::MarkersDrawn()};
  {
    const RasterMarker kMarker{painter, &GetPaintEngine, engine_.vtable};
  }
  EXPECT_EQ(RasterMarker::MarkersDrawn(), kMarkers);
}

TEST_F(RasterMarkerUnit, NestedGuardsOnOneEngineDoNotRestoreTheOuterHookEarly) {
  void** const kOriginalVtable{engine_.vtable};
  {
    const RasterMarker kOuter{painter_, &GetPaintEngine, kOriginalVtable};
    void* const* const kOuterVtable{engine_.vtable};
    {
      const RasterMarker kInner{painter_, &GetPaintEngine, kOriginalVtable};
      EXPECT_EQ(engine_.vtable, kOuterVtable);
      engine_.Draw(kPosition_, kItem_);
    }
    EXPECT_EQ(engine_.vtable, kOuterVtable);
    engine_.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  EXPECT_EQ(engine_.events, (std::vector{Event::Text, Event::Marker,
                                         Event::Text, Event::Marker}));
}

TEST_F(RasterMarkerUnit, NestedEnginesDispatchAndRestoreTheirOwnTables) {
  void** const kOriginalVtable{engine_.vtable};
  ProbeEngine other{kOriginalVtable};
  qt::Painter otherPainter{&other};
  {
    const RasterMarker kOuter{painter_, &GetPaintEngine, kOriginalVtable};
    void* const* const kOuterVtable{engine_.vtable};
    {
      const RasterMarker kInner{otherPainter, &GetPaintEngine, kOriginalVtable};
      EXPECT_NE(other.vtable, engine_.vtable);
      other.Draw(kPosition_, kItem_);
      engine_.Draw(kPosition_, kItem_);
    }
    EXPECT_EQ(other.vtable, kOriginalVtable);
    EXPECT_EQ(engine_.vtable, kOuterVtable);
    engine_.Draw(kPosition_, kItem_);
  }

  EXPECT_EQ(engine_.vtable, kOriginalVtable);
  EXPECT_EQ(other.vtable, kOriginalVtable);
  EXPECT_EQ(other.events, (std::vector{Event::Text, Event::Marker}));
  EXPECT_EQ(engine_.events, (std::vector{Event::Text, Event::Marker,
                                         Event::Text, Event::Marker}));
}
