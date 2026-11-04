#pragma once

#include <cstdint>
#include <expected>
#include <rgf/integrations/telegram/internal/message.hpp>
#include <system_error>

namespace rgf::integrations::telegram {

class MessageInfo {
 public:
  [[nodiscard]] static std::expected<MessageInfo, std::error_code> Read(
      const internal::Message& message) noexcept;

  [[nodiscard]] std::int64_t MessageId() const noexcept;
  [[nodiscard]] std::uint64_t DialogPeerId() const noexcept;
  [[nodiscard]] std::uint64_t FromPeerId() const noexcept;
  [[nodiscard]] bool IsOutgoing() const noexcept;

 private:
  MessageInfo(std::int64_t messageId, std::uint64_t dialogPeerId,
              std::uint64_t fromPeerId, bool outgoing) noexcept;

  const std::int64_t kMessageId_;
  const std::uint64_t kDialogPeerId_;
  const std::uint64_t kFromPeerId_;
  const bool kOutgoing_;
};

}  // namespace rgf::integrations::telegram
