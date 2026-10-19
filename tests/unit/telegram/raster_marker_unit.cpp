#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <rgf/core/cast.hpp>
#include <rgf/qt/internal/abi.hpp>
#include <rgf/qt/raster_text_hook.hpp>
#include <vector>

#include "raster_marker.hpp"

namespace rgf::qt::internal {

struct Painter {
  PaintEngine* engine;
};

}  // namespace rgf::qt::internal

namespace {

namespace qt = rgf::qt::internal;
using Table = std::array<void*, qt::kRasterVirtualCount + 1>;

enum class Event : std::uint8_t { Text, Marker };

struct ProbeEngine : qt::PaintEngine {
  explicit ProbeEngine(void** table) : PaintEngine{table} {}

  std::vector<Event> events;
  qt::RectF markerRectangle{};
  qt::Color markerColor{};
};

void OriginalDraw(qt::PaintEngine* engine, const qt::PointF& /*position*/,
                  const qt::TextItem& /*item*/) {
  // Test callbacks only receive ProbeEngine instances.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* probe{static_cast<ProbeEngine*>(engine)};
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

[[nodiscard]] qt::PaintEngine* GetPaintEngine(const qt::Painter* painter) {
  return painter->engine;
}

}  // namespace

TEST(RasterMarkerUnit, CoversTheTextRunWithOpaquePurpleAfterDrawing) {
  Table table{};
  table[qt::kDrawTextItemSlot + 1] =
      rgf::core::PointerCast<void*>(&OriginalDraw);
  table[qt::kFillColorSlot + 1] = rgf::core::PointerCast<void*>(&FillColor);
  ProbeEngine engine{table.data() + 1};
  qt::Painter painter{&engine};
  const qt::PointF kPosition{.x = 20.0, .y = 30.0};
  const qt::TextItem kItem{.descent = 224, .ascent = 848, .width = 5616};
  const rgf::qt::RasterTextHook kHook{painter, &GetPaintEngine,
                                      table.data() + 1,
                                      &rgf::telegram::DrawRasterMarker};

  rgf::core::PointerCast<qt::DrawTextItem>(
      engine.vtable[qt::kDrawTextItemSlot])(&engine, kPosition, kItem);

  EXPECT_EQ(engine.events, (std::vector{Event::Text, Event::Marker}));
  EXPECT_DOUBLE_EQ(engine.markerRectangle.x, 18.0);
  EXPECT_DOUBLE_EQ(engine.markerRectangle.y, 14.75);
  EXPECT_DOUBLE_EQ(engine.markerRectangle.width, 91.75);
  EXPECT_DOUBLE_EQ(engine.markerRectangle.height, 20.75);
  EXPECT_EQ(engine.markerColor.spec, 1);
  EXPECT_EQ(engine.markerColor.alpha, 0xffff);
  EXPECT_EQ(engine.markerColor.red, 0xa0a0);
  EXPECT_EQ(engine.markerColor.green, 0x2020);
  EXPECT_EQ(engine.markerColor.blue, 0xf0f0);
}
