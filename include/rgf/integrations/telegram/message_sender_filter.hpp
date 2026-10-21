#pragma once

#include <cstdint>
#include <expected>
#include <system_error>

namespace rgf::integrations::telegram {

class MessageInfo;

class MessageSenderFilter {
 public:
  [[nodiscard]] static std::expected<MessageSenderFilter, std::error_code>
  ForUser(std::uint64_t userId) noexcept;

  [[nodiscard]] bool Matches(const MessageInfo& message) const noexcept;

 private:
  explicit MessageSenderFilter(std::uint64_t userId) noexcept;

  const std::uint64_t kUserId_;
};

}  // namespace rgf::integrations::telegram
