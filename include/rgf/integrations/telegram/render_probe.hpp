#pragma once

#include <rgf/integrations/telegram/message_sender_filter.hpp>
#include <string_view>

namespace rgf::integrations::telegram {

using ReportCallback = void (*)(std::string_view details) noexcept;

void RunRenderProbe(MessageSenderFilter filter, ReportCallback report);

}  // namespace rgf::integrations::telegram
