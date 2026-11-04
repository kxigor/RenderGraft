#include <cstddef>
#include <cstdint>
#include <limits>
#include <rgf/core/cast.hpp>
#include <rgf/integrations/telegram/message_info.hpp>
#include <rgf/process/memory.hpp>

#include "internal/message_layout.hpp"

namespace rgf::integrations::telegram {

namespace {

[[nodiscard]] std::expected<std::uintptr_t, std::error_code> FieldAddress(
    std::uint64_t base, std::size_t offset) noexcept {
  if (base == 0 || base > std::numeric_limits<std::uintptr_t>::max() - offset) {
    return std::unexpected{std::make_error_code(std::errc::bad_address)};
  }
  return static_cast<std::uintptr_t>(base) + offset;
}

[[nodiscard]] std::expected<internal::MessageFields, std::error_code>
ReadMessageFields(const internal::Message& message) noexcept {
  const auto kItemAddress{
      FieldAddress(core::PointerCast<std::uintptr_t>(&message),
                   internal::kMessageItemOffset)};
  if (!kItemAddress) {
    return std::unexpected{kItemAddress.error()};
  }
  const auto kItem{process::ReadAt<std::uint64_t>(*kItemAddress)};
  if (!kItem) {
    return std::unexpected{kItem.error()};
  }
  const auto kFieldsAddress{FieldAddress(*kItem, internal::kItemIdOffset)};
  if (!kFieldsAddress) {
    return std::unexpected{kFieldsAddress.error()};
  }
  return process::ReadAt<internal::MessageFields>(*kFieldsAddress);
}

[[nodiscard]] std::expected<std::uint64_t, std::error_code> ReadPeerId(
    std::uint64_t peer) noexcept {
  const auto kIdAddress{FieldAddress(peer, internal::kPeerIdOffset)};
  if (!kIdAddress) {
    return std::unexpected{kIdAddress.error()};
  }
  return process::ReadAt<std::uint64_t>(*kIdAddress);
}

[[nodiscard]] std::expected<std::uint64_t, std::error_code> ReadDialogPeerId(
    std::uint64_t history) noexcept {
  const auto kPeerAddress{FieldAddress(history, internal::kHistoryPeerOffset)};
  if (!kPeerAddress) {
    return std::unexpected{kPeerAddress.error()};
  }
  const auto kPeer{process::ReadAt<std::uint64_t>(*kPeerAddress)};
  if (!kPeer) {
    return std::unexpected{kPeer.error()};
  }
  return ReadPeerId(*kPeer);
}

}  // namespace

std::expected<MessageInfo, std::error_code> MessageInfo::Read(
    const internal::Message& message) noexcept {
  const auto kFields{ReadMessageFields(message)};
  if (!kFields) {
    return std::unexpected{kFields.error()};
  }
  const auto kDialogPeerId{ReadDialogPeerId(kFields->history)};
  if (!kDialogPeerId) {
    return std::unexpected{kDialogPeerId.error()};
  }
  const auto kFromPeerId{ReadPeerId(kFields->from)};
  if (!kFromPeerId) {
    return std::unexpected{kFromPeerId.error()};
  }
  return MessageInfo{kFields->messageId, *kDialogPeerId, *kFromPeerId,
                     (kFields->flags & internal::kOutgoingFlag) != 0};
}

MessageInfo::MessageInfo(std::int64_t messageId, std::uint64_t dialogPeerId,
                         std::uint64_t fromPeerId, bool outgoing) noexcept
    : kMessageId_{messageId},
      kDialogPeerId_{dialogPeerId},
      kFromPeerId_{fromPeerId},
      kOutgoing_{outgoing} {}

std::int64_t MessageInfo::MessageId() const noexcept { return kMessageId_; }

std::uint64_t MessageInfo::DialogPeerId() const noexcept {
  return kDialogPeerId_;
}

std::uint64_t MessageInfo::FromPeerId() const noexcept { return kFromPeerId_; }

bool MessageInfo::IsOutgoing() const noexcept { return kOutgoing_; }

}  // namespace rgf::integrations::telegram
