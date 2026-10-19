#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rgf::telegram::internal {

struct Painter;
struct TextItem;

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

using GetPaintEngine = PaintEngine* (*)(const Painter* painter);
using DrawTextItem = void (*)(PaintEngine* engine, const PointF& position,
                              const TextItem& item);
using FillColor = void (*)(PaintEngine* engine, const RectF& rectangle,
                           const Color& color);

inline constexpr std::size_t kRasterVirtualCount{54};
inline constexpr std::size_t kDrawTextItemSlot{16};
inline constexpr std::size_t kFillColorSlot{37};
inline constexpr std::uintptr_t kRasterVtableRva{0x6c75178};
inline constexpr std::uintptr_t kDrawTextItemRva{0x5e8a6c0};
inline constexpr std::uintptr_t kFillColorRva{0x5e8c180};
inline constexpr std::uintptr_t kGetPaintEngineRva{0x5d3ba80};
inline constexpr std::array<std::uint8_t, 11> kGetPaintEngineCode{
    0x48, 0x8b, 0x01, 0x48, 0x8b, 0x80, 0xf0, 0x00, 0x00, 0x00, 0xc3};

inline constexpr std::size_t kPointFSize{16};
inline constexpr std::size_t kPointFYOffset{8};
inline constexpr std::size_t kRectFSize{32};
inline constexpr std::size_t kRectFHeightOffset{24};
inline constexpr std::size_t kColorSize{16};
inline constexpr std::size_t kColorAlphaOffset{4};
inline constexpr std::size_t kColorRedOffset{6};
inline constexpr std::size_t kColorGreenOffset{8};
inline constexpr std::size_t kColorBlueOffset{10};

static_assert(std::is_standard_layout_v<PaintEngine>);
static_assert(std::is_standard_layout_v<PointF>);
static_assert(std::is_standard_layout_v<RectF>);
static_assert(std::is_standard_layout_v<Color>);
static_assert(offsetof(PaintEngine, vtable) == 0);
static_assert(sizeof(PointF) == kPointFSize);
static_assert(offsetof(PointF, y) == kPointFYOffset);
static_assert(sizeof(RectF) == kRectFSize);
static_assert(offsetof(RectF, height) == kRectFHeightOffset);
static_assert(sizeof(Color) == kColorSize);
static_assert(offsetof(Color, alpha) == kColorAlphaOffset);
static_assert(offsetof(Color, red) == kColorRedOffset);
static_assert(offsetof(Color, green) == kColorGreenOffset);
static_assert(offsetof(Color, blue) == kColorBlueOffset);

}  // namespace rgf::telegram::internal
