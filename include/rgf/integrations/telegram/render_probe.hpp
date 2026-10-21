#pragma once

#include <string_view>

namespace rgf::integrations::telegram {

using ReportCallback = void (*)(std::string_view details) noexcept;

void RunRenderProbe(ReportCallback report);

}  // namespace rgf::integrations::telegram
