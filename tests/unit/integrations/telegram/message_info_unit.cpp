#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <rgf/core/cast.hpp>
#include <rgf/integrations/telegram/message_info.hpp>
#include <rgf/integrations/telegram/message_sender_filter.hpp>
#include <rgf/process/error.hpp>
#include <rgf/process/memory.hpp>
#include <span>
#include <system_error>

#include "internal/message_layout.hpp"

namespace {

namespace telegram = rgf::integrations::telegram::internal;
using rgf::integrations::telegram::MessageInfo;
using rgf::integrations::telegram::MessageSenderFilter;

constexpr std::int64_t kMessageId{0x12345678abc};
constexpr std::uint64_t kDialogPeerId{(2ULL << 48U) | 0x3456ULL};
constexpr std::uint64_t kFromPeerId{0x5678};
constexpr std::uint64_t kUnreadablePointer{1};
constexpr unsigned int kPeerTypeShift{48};

[[nodiscard]] std::uint64_t Address(const void* pointer) noexcept {
  return rgf::core::PointerCast<std::uintptr_t>(pointer);
}

void Store64(std::span<std::byte> bytes, std::size_t offset,
             std::uint64_t value) noexcept {
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

class MessageInfoUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    rgf::process::ForgetReadableRegions();
    Store64(message_, telegram::kMessageItemOffset, Address(item_.data()));
    Store64(history_, telegram::kHistoryPeerOffset,
            Address(dialogPeer_.data()));
    Store64(dialogPeer_, telegram::kPeerIdOffset, kDialogPeerId);
    Store64(fromPeer_, telegram::kPeerIdOffset, kFromPeerId);
    fields_.history = Address(history_.data());
    fields_.from = Address(fromPeer_.data());
    StoreFields();
  }

  void TearDown() override { rgf::process::ForgetReadableRegions(); }

  [[nodiscard]] const telegram::Message& Message() const noexcept {
    return *rgf::core::PointerCast<const telegram::Message*>(message_.data());
  }

  void StoreFields() noexcept {
    std::memcpy(item_.data() + telegram::kItemIdOffset, &fields_,
                sizeof(fields_));
  }

  void ExpectBadAddress() const {
    const auto kInfo{MessageInfo::Read(Message())};
    ASSERT_FALSE(kInfo.has_value());
    EXPECT_EQ(kInfo.error(), std::make_error_code(std::errc::bad_address));
  }

  void ExpectUnreadable() const {
    const auto kInfo{MessageInfo::Read(Message())};
    ASSERT_FALSE(kInfo.has_value());
    EXPECT_EQ(kInfo.error(),
              rgf::process::make_error_code(rgf::process::Error::NotReadable));
  }

  [[nodiscard]] telegram::MessageFields& Fields() noexcept { return fields_; }
  [[nodiscard]] std::span<std::byte> MessageBytes() noexcept {
    return message_;
  }
  [[nodiscard]] std::span<std::byte> HistoryBytes() noexcept {
    return history_;
  }
  [[nodiscard]] std::span<std::byte> DialogPeerBytes() noexcept {
    return dialogPeer_;
  }
  [[nodiscard]] std::span<std::byte> FromPeerBytes() noexcept {
    return fromPeer_;
  }

 private:
  std::array<std::byte, telegram::kMessageItemOffset + sizeof(std::uint64_t)>
      message_{};
  std::array<std::byte,
             telegram::kItemIdOffset + sizeof(telegram::MessageFields)>
      item_{};
  std::array<std::byte, telegram::kHistoryPeerOffset + sizeof(std::uint64_t)>
      history_{};
  std::array<std::byte, telegram::kPeerIdOffset + sizeof(std::uint64_t)>
      dialogPeer_{};
  std::array<std::byte, telegram::kPeerIdOffset + sizeof(std::uint64_t)>
      fromPeer_{};
  telegram::MessageFields fields_{.messageId = kMessageId,
                                  .history = 0,
                                  .from = 0,
                                  .cachedDisplayFrom = kUnreadablePointer,
                                  .flags = telegram::kOutgoingFlag};
};

using MessageSenderFilterUnit = MessageInfoUnit;

void ExpectInvalidUserId(std::uint64_t userId) {
  const auto kFilter{MessageSenderFilter::ForUser(userId)};
  ASSERT_FALSE(kFilter.has_value());
  EXPECT_EQ(kFilter.error(), std::make_error_code(std::errc::invalid_argument));
}

}  // namespace

TEST(MessageSenderFilter, RejectsZeroAndNonUserPeerIds) {
  ExpectInvalidUserId(0);
  ExpectInvalidUserId(1ULL << kPeerTypeShift);
  ExpectInvalidUserId((2ULL << kPeerTypeShift) | kFromPeerId);
  ExpectInvalidUserId(std::numeric_limits<std::uint64_t>::max());
}

TEST(MessageSenderFilter, AcceptsTheLargestUntaggedUserId) {
  constexpr std::uint64_t kUserId{(1ULL << kPeerTypeShift) - 1};

  const auto kFilter{MessageSenderFilter::ForUser(kUserId)};

  EXPECT_TRUE(kFilter.has_value());
}

TEST_F(MessageSenderFilterUnit, MatchesTheRequestedModelSender) {
  const auto kFilter{MessageSenderFilter::ForUser(kFromPeerId)};
  const auto kInfo{MessageInfo::Read(Message())};
  ASSERT_TRUE(kFilter.has_value());
  ASSERT_TRUE(kInfo.has_value());

  EXPECT_TRUE(kFilter->Matches(*kInfo));
}

TEST_F(MessageSenderFilterUnit, RejectsMessagesFromAnotherPerson) {
  Store64(FromPeerBytes(), telegram::kPeerIdOffset, kFromPeerId + 1);
  const auto kFilter{MessageSenderFilter::ForUser(kFromPeerId)};
  const auto kInfo{MessageInfo::Read(Message())};
  ASSERT_TRUE(kFilter.has_value());
  ASSERT_TRUE(kInfo.has_value());

  EXPECT_FALSE(kFilter->Matches(*kInfo));
}

TEST_F(MessageSenderFilterUnit, KeepsTheChannelTypeWhenMatchingSenderIds) {
  constexpr std::uint64_t kChannelPeerId{(2ULL << kPeerTypeShift) |
                                         kFromPeerId};
  Store64(FromPeerBytes(), telegram::kPeerIdOffset, kChannelPeerId);
  const auto kFilter{MessageSenderFilter::ForUser(kFromPeerId)};
  const auto kInfo{MessageInfo::Read(Message())};
  ASSERT_TRUE(kFilter.has_value());
  ASSERT_TRUE(kInfo.has_value());

  EXPECT_FALSE(kFilter->Matches(*kInfo));
}

TEST_F(MessageSenderFilterUnit, DialogIdentityCannotReplaceTheSender) {
  Store64(DialogPeerBytes(), telegram::kPeerIdOffset, kFromPeerId);
  Store64(FromPeerBytes(), telegram::kPeerIdOffset, kFromPeerId + 1);
  const auto kFilter{MessageSenderFilter::ForUser(kFromPeerId)};
  const auto kInfo{MessageInfo::Read(Message())};
  ASSERT_TRUE(kFilter.has_value());
  ASSERT_TRUE(kInfo.has_value());

  EXPECT_FALSE(kFilter->Matches(*kInfo));
}

TEST_F(MessageSenderFilterUnit, OutgoingStateDoesNotChangeTheSenderMatch) {
  const auto kFilter{MessageSenderFilter::ForUser(kFromPeerId)};
  const auto kOutgoing{MessageInfo::Read(Message())};
  Fields().flags = 0;
  StoreFields();
  const auto kIncoming{MessageInfo::Read(Message())};
  ASSERT_TRUE(kFilter.has_value());
  ASSERT_TRUE(kOutgoing.has_value());
  ASSERT_TRUE(kIncoming.has_value());

  EXPECT_TRUE(kFilter->Matches(*kOutgoing));
  EXPECT_TRUE(kFilter->Matches(*kIncoming));
}

TEST_F(MessageInfoUnit, ReadsTheModelIdsAndOutgoingFlag) {
  const auto kInfo{MessageInfo::Read(Message())};

  ASSERT_TRUE(kInfo.has_value());
  EXPECT_EQ(kInfo->MessageId(), kMessageId);
  EXPECT_EQ(kInfo->DialogPeerId(), kDialogPeerId);
  EXPECT_EQ(kInfo->FromPeerId(), kFromPeerId);
  EXPECT_TRUE(kInfo->IsOutgoing());
}

TEST_F(MessageInfoUnit, PreservesNegativeClientMessageIds) {
  constexpr std::int64_t kClientId{-0x10000000000LL};
  Fields().messageId = kClientId;
  StoreFields();

  const auto kInfo{MessageInfo::Read(Message())};

  ASSERT_TRUE(kInfo.has_value());
  EXPECT_EQ(kInfo->MessageId(), kClientId);
}

TEST_F(MessageInfoUnit, PreservesPeerTypeBitsWithoutConvertingToBotApiIds) {
  const std::array kPeerIds{0x1234ULL, (1ULL << 48U) | 0x1234ULL,
                            (2ULL << 48U) | 0x1234ULL};
  for (const std::uint64_t kPeerId : kPeerIds) {
    Store64(DialogPeerBytes(), telegram::kPeerIdOffset, kPeerId);
    Store64(FromPeerBytes(), telegram::kPeerIdOffset, kPeerId);

    const auto kInfo{MessageInfo::Read(Message())};

    ASSERT_TRUE(kInfo.has_value());
    EXPECT_EQ(kInfo->DialogPeerId(), kPeerId);
    EXPECT_EQ(kInfo->FromPeerId(), kPeerId);
  }
}

TEST_F(MessageInfoUnit, ReportsTheStoredFromPeerIncludingTheDialogFallback) {
  Fields().from = Address(DialogPeerBytes().data());
  StoreFields();

  const auto kInfo{MessageInfo::Read(Message())};

  ASSERT_TRUE(kInfo.has_value());
  EXPECT_EQ(kInfo->FromPeerId(), kDialogPeerId);
  EXPECT_EQ(kInfo->DialogPeerId(), kDialogPeerId);
}

TEST_F(MessageInfoUnit, UsesOnlyTheInternalOutgoingBit) {
  // Raw MTProto's out bit and unrelated internal flags must not mean outgoing.
  constexpr std::uint64_t kOtherFlags{(1ULL << 1U) | (1ULL << 63U)};
  Fields().flags = kOtherFlags;
  StoreFields();

  const auto kInfo{MessageInfo::Read(Message())};

  ASSERT_TRUE(kInfo.has_value());
  EXPECT_FALSE(kInfo->IsOutgoing());
}

TEST_F(MessageInfoUnit, OwnsAnImmutableCopyAfterTheHostStateChanges) {
  const auto kInfo{MessageInfo::Read(Message())};
  ASSERT_TRUE(kInfo.has_value());
  const MessageInfo kCopy{*kInfo};
  constexpr std::int64_t kNewId{17};
  constexpr std::uint64_t kOtherPeer{0x9876};
  Fields().messageId = kNewId;
  Fields().flags = 0;
  StoreFields();
  Store64(DialogPeerBytes(), telegram::kPeerIdOffset, kOtherPeer);
  Store64(FromPeerBytes(), telegram::kPeerIdOffset, kOtherPeer);

  const auto kUpdated{MessageInfo::Read(Message())};

  ASSERT_TRUE(kUpdated.has_value());
  EXPECT_EQ(kUpdated->MessageId(), kNewId);
  EXPECT_FALSE(kUpdated->IsOutgoing());
  EXPECT_EQ(kCopy.MessageId(), kMessageId);
  EXPECT_EQ(kCopy.DialogPeerId(), kDialogPeerId);
  EXPECT_EQ(kCopy.FromPeerId(), kFromPeerId);
  EXPECT_TRUE(kCopy.IsOutgoing());
}

TEST_F(MessageInfoUnit, RejectsANullHistoryItem) {
  Store64(MessageBytes(), telegram::kMessageItemOffset, 0);

  ExpectBadAddress();
}

TEST_F(MessageInfoUnit, RejectsANullHistory) {
  Fields().history = 0;
  StoreFields();

  ExpectBadAddress();
}

TEST_F(MessageInfoUnit, RejectsANullDialogPeer) {
  Store64(HistoryBytes(), telegram::kHistoryPeerOffset, 0);

  ExpectBadAddress();
}

TEST_F(MessageInfoUnit, RejectsANullFromPeer) {
  Fields().from = 0;
  StoreFields();

  ExpectBadAddress();
}

TEST_F(MessageInfoUnit, PreservesAnUnreadableHistoryItemError) {
  Store64(MessageBytes(), telegram::kMessageItemOffset, kUnreadablePointer);

  ExpectUnreadable();
}

TEST_F(MessageInfoUnit, PreservesAnUnreadableHistoryError) {
  Fields().history = kUnreadablePointer;
  StoreFields();

  ExpectUnreadable();
}

TEST_F(MessageInfoUnit, PreservesAnUnreadableDialogPeerError) {
  Store64(HistoryBytes(), telegram::kHistoryPeerOffset, kUnreadablePointer);

  ExpectUnreadable();
}

TEST_F(MessageInfoUnit, PreservesAnUnreadableFromPeerError) {
  Fields().from = kUnreadablePointer;
  StoreFields();

  ExpectUnreadable();
}

TEST_F(MessageInfoUnit, RejectsForeignPointersWhoseFieldAddressWouldOverflow) {
  Store64(MessageBytes(), telegram::kMessageItemOffset,
          std::numeric_limits<std::uint64_t>::max());

  ExpectBadAddress();
}

TEST_F(MessageInfoUnit, RejectsForeignHistoryPointersThatCannotFitTheReader) {
  Fields().history = std::numeric_limits<std::uint64_t>::max();
  StoreFields();

  ExpectBadAddress();
}
