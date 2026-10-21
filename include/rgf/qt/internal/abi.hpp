#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rgf::qt::internal {

// Foreign ABI verified against Qt 6.11.2 built with MSVC for Windows x64.
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
  std::uintptr_t windowId;
  Widget* widget;
};

inline constexpr std::size_t kWidgetHashSpanBucketCount{128};
inline constexpr std::uint8_t kWidgetHashUnusedEntry{0xff};

// QHashPrivate::Span<Node> stores live bucket-to-entry indices separately.
// Unused allocated entries contain free-list storage, not live widgets.
struct WidgetHashSpan {
  std::array<std::uint8_t, kWidgetHashSpanBucketCount> offsets;
  WidgetHashNode* entries;
  std::uint8_t allocated;
  std::uint8_t nextFree;
};

struct WidgetHashData {
  std::int32_t referenceCount;
  std::size_t size;
  std::size_t numBuckets;
  std::size_t seed;
  WidgetHashSpan* spans;
};

struct WidgetMapper {
  WidgetHashData* data;
};

using GetPaintEngineFn = PaintEngine* (*)(const Painter* painter);
using DrawTextItemFn = void (*)(PaintEngine* engine, const PointF& position,
                                const TextItem& item);
using FillColorFn = void (*)(PaintEngine* engine, const RectF& rectangle,
                             const Color& color);
using UpdateWidgetFn = void (*)(Widget* widget);

inline constexpr std::size_t kRasterVirtualCount{56};
inline constexpr std::size_t kDrawTextItemSlot{16};
inline constexpr std::size_t kFillColorSlot{39};

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

inline constexpr std::size_t kWidgetHashDataSize{40};
inline constexpr std::size_t kWidgetHashSizeOffset{8};
inline constexpr std::size_t kWidgetHashBucketCountOffset{16};
inline constexpr std::size_t kWidgetHashSeedOffset{24};
inline constexpr std::size_t kWidgetHashSpansOffset{32};
inline constexpr std::size_t kWidgetHashSpanSize{144};
inline constexpr std::size_t kWidgetHashEntriesOffset{128};
inline constexpr std::size_t kWidgetHashAllocatedOffset{136};
inline constexpr std::size_t kWidgetHashNextFreeOffset{137};
inline constexpr std::size_t kWidgetHashNodeSize{16};
inline constexpr std::size_t kWidgetHashWidgetOffset{8};
inline constexpr std::size_t kWidgetHostPointerSize{8};

static_assert(std::is_standard_layout_v<PaintEngine>);
static_assert(std::is_standard_layout_v<TextItem>);
static_assert(std::is_standard_layout_v<PointF>);
static_assert(std::is_standard_layout_v<RectF>);
static_assert(std::is_standard_layout_v<Color>);
static_assert(std::is_standard_layout_v<WidgetHashNode>);
static_assert(std::is_standard_layout_v<WidgetHashSpan>);
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
static_assert(offsetof(WidgetHashNode, windowId) == 0);
static_assert(offsetof(WidgetHashSpan, offsets) == 0);
static_assert(offsetof(WidgetHashData, referenceCount) == 0);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              sizeof(WidgetHashData) == kWidgetHashDataSize);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashData, size) == kWidgetHashSizeOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashData, numBuckets) ==
                  kWidgetHashBucketCountOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashData, seed) == kWidgetHashSeedOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashData, spans) == kWidgetHashSpansOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              sizeof(WidgetHashSpan) == kWidgetHashSpanSize);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashSpan, entries) == kWidgetHashEntriesOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashSpan, allocated) ==
                  kWidgetHashAllocatedOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashSpan, nextFree) == kWidgetHashNextFreeOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              sizeof(WidgetHashNode) == kWidgetHashNodeSize);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashNode, widget) == kWidgetHashWidgetOffset);

}  // namespace rgf::qt::internal
