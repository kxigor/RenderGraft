#include <rgf/integrations/telegram/message_info.hpp>
#include <rgf/integrations/telegram/message_sender_filter.hpp>

namespace rgf::integrations::telegram {

namespace {

constexpr std::uint64_t kUserPeerLimit{1ULL << 48U};

}  // namespace

std::expected<MessageSenderFilter, std::error_code>
MessageSenderFilter::ForUser(std::uint64_t userId) noexcept {
  if (userId == 0 || userId >= kUserPeerLimit) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  return MessageSenderFilter{userId};
}

MessageSenderFilter::MessageSenderFilter(std::uint64_t userId) noexcept
    : kUserId_{userId} {}

bool MessageSenderFilter::Matches(const MessageInfo& message) const noexcept {
  return message.FromPeerId() == kUserId_;
}

}  // namespace rgf::integrations::telegram
