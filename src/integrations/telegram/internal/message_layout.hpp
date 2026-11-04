#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rgf::integrations::telegram::internal {

// Telegram Desktop 7.3.2, Windows x64, MSVC ABI.
// Message is the owning view for this draw scope. Its HistoryItem is not
// necessarily the source of every Qt text run (quotes, names, album captions).
inline constexpr std::size_t kMessageItemOffset{0x40};
inline constexpr std::size_t kItemIdOffset{0x8};
inline constexpr std::size_t kHistoryPeerOffset{0x320};
inline constexpr std::size_t kPeerIdOffset{0x8};
inline constexpr std::uint64_t kOutgoingFlag{0x800};

// A contiguous HistoryItem prefix beginning at id. Foreign pointers remain
// 64-bit values even when the reader itself is built for x86.
// from is the model peer, including its channel/dialog fallback; it is not
// displayFrom, a forwarding source, or proof of a particular human author.
struct MessageFields {
  std::int64_t messageId;
  std::uint64_t history;
  std::uint64_t from;
  std::uint64_t cachedDisplayFrom;
  std::uint64_t flags;
};

// IDs retain their signed message value and typed PeerId bits. The dialog
// plus message pair is account-scoped; pending client IDs change after sending.
inline constexpr std::size_t kMessageFieldsSize{40};
inline constexpr std::size_t kMessageHistoryFieldOffset{8};
inline constexpr std::size_t kMessageFromFieldOffset{16};
inline constexpr std::size_t kMessageDisplayFromFieldOffset{24};
inline constexpr std::size_t kMessageFlagsFieldOffset{32};

static_assert(std::is_trivially_copyable_v<MessageFields>);
static_assert(std::is_standard_layout_v<MessageFields>);
static_assert(sizeof(MessageFields) == kMessageFieldsSize);
static_assert(offsetof(MessageFields, messageId) == 0);
static_assert(offsetof(MessageFields, history) == kMessageHistoryFieldOffset);
static_assert(offsetof(MessageFields, from) == kMessageFromFieldOffset);
static_assert(offsetof(MessageFields, cachedDisplayFrom) ==
              kMessageDisplayFromFieldOffset);
static_assert(offsetof(MessageFields, flags) == kMessageFlagsFieldOffset);

}  // namespace rgf::integrations::telegram::internal
