#pragma once

#include <cstdint>
#include <system_error>
#include <type_traits>

namespace rgf::hook {

enum class Error : std::uint8_t {
  HookAlreadyApplied = 1,
  HookNotApplied = 2,
  DetourNotSet = 3,
  SlotNotReadable = 4,
  WindowNotFound = 5,
};

[[nodiscard]] const std::error_category& ErrorCategory();

// NOLINTNEXTLINE(readability-identifier-naming)
[[nodiscard]] std::error_code make_error_code(Error error);

}  // namespace rgf::hook

template <>
struct std::is_error_code_enum<rgf::hook::Error> : std::true_type {};
