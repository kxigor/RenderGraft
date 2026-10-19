#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rgf::qt::internal {

// Foreign ABI verified against Qt 5.15.19 built with MSVC for Windows x64.
// Layouts, function signatures, and virtual slots describe that host build.
// Executable addresses and build validation belong to the adapter.

struct Painter;
struct Widget;

// QTextItemInt starts with these signed QFixed metrics (26.6 fixed point).
// Only the metric prefix is represented; the host object continues past it.
struct TextItem {
  std::int32_t descent;
  std::int32_t ascent;
  std::int32_t width;
};

struct PaintEngine {
  void** vtable;
};

struct PointF {
  double x;
  double y;
};

struct RectF {
  double x;
  double y;
  double width;
  double height;
};

struct Color {
  std::int32_t spec;
  std::uint16_t alpha;
  std::uint16_t red;
  std::uint16_t green;
  std::uint16_t blue;
  std::uint16_t padding;
};

struct WidgetHashNode {
  WidgetHashNode* next;
  std::uint32_t hash;
  std::uintptr_t windowId;
  Widget* widget;
};

struct WidgetHashData {
  WidgetHashNode* fakeNext;
  WidgetHashNode** buckets;
  std::int32_t referenceCount;
  std::int32_t size;
  std::int32_t nodeSize;
  std::int16_t userNumBits;
  std::int16_t numBits;
  std::int32_t numBuckets;
  std::uint32_t seed;
};

struct WidgetMapper {
  WidgetHashData* data;
};

using GetPaintEngine = PaintEngine* (*)(const Painter* painter);
using DrawTextItem = void (*)(PaintEngine* engine, const PointF& position,
                              const TextItem& item);
using FillColor = void (*)(PaintEngine* engine, const RectF& rectangle,
                           const Color& color);
using FindWidgetNode = WidgetHashNode** (*)(const WidgetMapper* mapper,
                                            const std::uintptr_t& windowId,
                                            std::uint32_t hash);
using UpdateWidget = void (*)(Widget* widget);

inline constexpr std::size_t kRasterVirtualCount{54};
inline constexpr std::size_t kDrawTextItemSlot{16};
inline constexpr std::size_t kFillColorSlot{37};

inline constexpr double kFixedScale{64.0};
inline constexpr std::size_t kTextItemMetricsSize{12};
inline constexpr std::size_t kTextItemAscentOffset{4};
inline constexpr std::size_t kTextItemWidthOffset{8};

inline constexpr std::size_t kPointFSize{16};
inline constexpr std::size_t kPointFYOffset{8};
inline constexpr std::size_t kRectFSize{32};
inline constexpr std::size_t kRectFHeightOffset{24};
inline constexpr std::size_t kColorSize{16};
inline constexpr std::size_t kColorAlphaOffset{4};
inline constexpr std::size_t kColorRedOffset{6};
inline constexpr std::size_t kColorGreenOffset{8};
inline constexpr std::size_t kColorBlueOffset{10};

inline constexpr std::size_t kWidgetHashSeedOffset{0x24};
inline constexpr std::size_t kWidgetHashWindowIdOffset{0x10};
inline constexpr std::size_t kWidgetHashWidgetOffset{0x18};
inline constexpr std::size_t kWidgetHostPointerSize{8};
inline constexpr unsigned int kWidgetHashShift{31};

static_assert(std::is_standard_layout_v<PaintEngine>);
static_assert(std::is_standard_layout_v<TextItem>);
static_assert(std::is_standard_layout_v<PointF>);
static_assert(std::is_standard_layout_v<RectF>);
static_assert(std::is_standard_layout_v<Color>);
static_assert(std::is_standard_layout_v<WidgetHashNode>);
static_assert(std::is_standard_layout_v<WidgetHashData>);
static_assert(std::is_standard_layout_v<WidgetMapper>);
static_assert(offsetof(PaintEngine, vtable) == 0);
static_assert(sizeof(TextItem) == kTextItemMetricsSize);
static_assert(offsetof(TextItem, descent) == 0);
static_assert(offsetof(TextItem, ascent) == kTextItemAscentOffset);
static_assert(offsetof(TextItem, width) == kTextItemWidthOffset);
static_assert(sizeof(PointF) == kPointFSize);
static_assert(offsetof(PointF, y) == kPointFYOffset);
static_assert(sizeof(RectF) == kRectFSize);
static_assert(offsetof(RectF, height) == kRectFHeightOffset);
static_assert(sizeof(Color) == kColorSize);
static_assert(offsetof(Color, alpha) == kColorAlphaOffset);
static_assert(offsetof(Color, red) == kColorRedOffset);
static_assert(offsetof(Color, green) == kColorGreenOffset);
static_assert(offsetof(Color, blue) == kColorBlueOffset);
static_assert(offsetof(WidgetMapper, data) == 0);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashData, seed) == kWidgetHashSeedOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashNode, windowId) == kWidgetHashWindowIdOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashNode, widget) == kWidgetHashWidgetOffset);

}  // namespace rgf::qt::internal
