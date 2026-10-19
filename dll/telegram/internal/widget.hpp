#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rgf::telegram::internal {

struct Widget;

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

using FindWidgetNode = WidgetHashNode** (*)(const WidgetMapper* mapper,
                                            const std::uintptr_t& windowId,
                                            std::uint32_t hash);
using UpdateWidget = void (*)(Widget* widget);

inline constexpr std::uintptr_t kWidgetMapperRva{0xdfa76a0};
inline constexpr std::uintptr_t kFindWidgetNodeRva{0x5a8ccd0};
inline constexpr std::uintptr_t kUpdateWidgetRva{0x5ab8bc0};
inline constexpr std::array<std::uint8_t, 32> kFindWidgetNodeCode{
    0x4c, 0x8b, 0x09, 0x45, 0x8b, 0xd0, 0x4c, 0x8b, 0xda, 0x45, 0x8b,
    0x41, 0x20, 0x45, 0x85, 0xc0, 0x74, 0x3f, 0x33, 0xd2, 0x41, 0x8b,
    0xc2, 0x41, 0xf7, 0xf0, 0x49, 0x8b, 0x41, 0x08, 0x4c, 0x8d};
inline constexpr std::array<std::uint8_t, 32> kUpdateWidgetCode{
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x60, 0x48,
    0x8b, 0x51, 0x28, 0x33, 0xdb, 0x48, 0x89, 0x5c, 0x24, 0x30, 0x8b,
    0x42, 0x1c, 0x2b, 0x42, 0x14, 0x89, 0x44, 0x24, 0x38, 0x8b};

inline constexpr std::size_t kWidgetHashSeedOffset{0x24};
inline constexpr std::size_t kWidgetHashWindowIdOffset{0x10};
inline constexpr std::size_t kWidgetHashWidgetOffset{0x18};
inline constexpr std::size_t kWidgetHostPointerSize{8};
inline constexpr unsigned int kWidgetHashShift{31};

static_assert(std::is_standard_layout_v<WidgetHashNode>);
static_assert(std::is_standard_layout_v<WidgetHashData>);
static_assert(std::is_standard_layout_v<WidgetMapper>);
static_assert(offsetof(WidgetMapper, data) == 0);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashData, seed) == kWidgetHashSeedOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashNode, windowId) == kWidgetHashWindowIdOffset);
static_assert(sizeof(void*) != kWidgetHostPointerSize ||
              offsetof(WidgetHashNode, widget) == kWidgetHashWidgetOffset);

}  // namespace rgf::telegram::internal
