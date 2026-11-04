#include <rgf/core/error.hpp>
#include <rgf/hook/error.hpp>
#include <string>
#include <system_error>

namespace rgf::hook {

const char* CategoryName(Error /*tag*/) { return "rgf::hook"; }

std::string Describe(Error error) {
  switch (error) {
    case Error::HookAlreadyApplied:
      return "the hook is already applied: the table slot already holds the "
             "detour";
    case Error::HookNotApplied:
      return "the hook is not applied: there is no saved original to put "
             "back into the table slot";
    case Error::DetourNotSet:
      return "the hook has no detour: SetDetour must name one before the "
             "hook can be enabled";
    case Error::SlotNotReadable:
      return "the table slot is not readable: the address is not committed, "
             "or the page denies access";
    case Error::WindowNotFound:
      return "the window was not found: the handle is null or no longer "
             "names a live window";
    default:
      return "unknown rgf::hook error";
  }
}

const std::error_category& ErrorCategory() { return core::CategoryOf<Error>(); }

// NOLINTNEXTLINE(readability-identifier-naming)
std::error_code make_error_code(Error error) {
  return core::MakeErrorCode(error);
}

}  // namespace rgf::hook
