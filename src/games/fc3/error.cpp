#include <rgf/core/error.hpp>
#include <rgf/games/fc3/error.hpp>
#include <string>
#include <system_error>

namespace rgf::games::fc3 {

const char* CategoryName(Error /*tag*/) { return "rgf::games::fc3"; }

std::string Describe(Error error) {
  switch (error) {
    case Error::CameraUnavailable:
      return "no camera is active: the manager holds none, or the world is "
             "not loaded yet";
    case Error::CameraNotFound:
      return "the active camera was located but one of its view fields did "
             "not read: position, forward, up or either field of view";
    case Error::CameraNotSane:
      return "the camera was found but its basis fails the sanity check: "
             "the forward vector is not unit length, or the field of view "
             "is out of range";
    default:
      return "unknown rgf::games::fc3 error";
  }
}

const std::error_category& ErrorCategory() { return core::CategoryOf<Error>(); }

// NOLINTNEXTLINE(readability-identifier-naming)
std::error_code make_error_code(Error error) {
  return core::MakeErrorCode(error);
}

}  // namespace rgf::games::fc3
