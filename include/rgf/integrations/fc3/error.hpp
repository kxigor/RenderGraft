#pragma once

#include <cstdint>
#include <system_error>
#include <type_traits>

namespace rgf::integrations::fc3 {

enum class Error : std::uint8_t {
  CameraUnavailable = 1,
  CameraNotFound = 2,
  CameraNotSane = 3,
};

[[nodiscard]] const std::error_category& ErrorCategory();

// NOLINTNEXTLINE(readability-identifier-naming)
[[nodiscard]] std::error_code make_error_code(Error error);

}  // namespace rgf::integrations::fc3

template <>
struct std::is_error_code_enum<rgf::integrations::fc3::Error> : std::true_type {
};
