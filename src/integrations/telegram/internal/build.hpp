#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rgf::integrations::telegram::internal {

inline constexpr std::uint32_t kTimestamp{0x6ad7d64f};
inline constexpr std::uint32_t kImageSize{0xf398000};
inline constexpr std::uint32_t kEntryRva{0x6a1937c};
inline constexpr std::uint32_t kChecksum{0xee55c40};

inline constexpr std::uintptr_t kMessageTableRva{0x9094968};
inline constexpr std::uintptr_t kMessageDrawRva{0x1dbed50};
inline constexpr std::size_t kMessageDrawSlot{10};

inline constexpr std::uintptr_t kHistoryTableRva{0x90ac088};
inline constexpr std::uintptr_t kHistoryPaintRva{0x1f832b0};
inline constexpr std::size_t kHistoryPaintSlot{27};
inline constexpr std::uintptr_t kHistoryPaintDeviceTableRva{0x90abfb0};
inline constexpr std::uintptr_t kWidgetRedirectedRva{0x5e0e7e0};

inline constexpr std::uintptr_t kRasterVtableRva{0x73b5638};
inline constexpr std::uintptr_t kDrawTextItemRva{0x6354c30};
inline constexpr std::uintptr_t kFillColorRva{0x6356ef0};
inline constexpr std::uintptr_t kDrawPixmapRectRva{0x6352e60};
inline constexpr std::uintptr_t kDrawTiledPixmapRva{0x6355150};
inline constexpr std::uintptr_t kDrawImageRectRva{0x6351150};
inline constexpr std::uintptr_t kDrawPixmapPointRva{0x6352b50};
inline constexpr std::uintptr_t kDrawImagePointRva{0x6350c90};
inline constexpr std::uintptr_t kGetPaintEngineRva{0x614e2b0};
inline constexpr std::array<std::uint8_t, 11> kGetPaintEngineCode{
    0x48, 0x8b, 0x01, 0x48, 0x8b, 0x80, 0x18, 0x01, 0x00, 0x00, 0xc3};

inline constexpr std::uintptr_t kWidgetMapperRva{0xec69928};
inline constexpr std::uintptr_t kSetWidgetIdRva{0x5e19640};
inline constexpr std::uintptr_t kUpdateWidgetRva{0x5e1d4b0};
inline constexpr std::array<std::uint8_t, 32> kSetWidgetIdCode{
    0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec,
    0x30, 0x48, 0x8b, 0x71, 0x08, 0x48, 0x8b, 0xf9, 0x48, 0x8b, 0x0d,
    0xce, 0x02, 0xe5, 0x08, 0x48, 0x8b, 0xda, 0x48, 0x89, 0x74};
inline constexpr std::array<std::uint8_t, 32> kUpdateWidgetCode{
    0x48, 0x83, 0xec, 0x38, 0x48, 0x8b, 0x51, 0x20, 0x33, 0xc0, 0x48,
    0x8b, 0x49, 0x08, 0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x89, 0x44,
    0x24, 0x20, 0x8b, 0x42, 0x1c, 0x2b, 0x42, 0x14, 0x89, 0x44};

}  // namespace rgf::integrations::telegram::internal
