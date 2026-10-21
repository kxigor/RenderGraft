#pragma once

#include <string_view>

namespace rgf::integrations::telegram {

void RunDiagnostics(std::string_view details = {}) noexcept;

}  // namespace rgf::integrations::telegram
