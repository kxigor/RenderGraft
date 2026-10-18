#include <windows.h>

#include <rgf/app/module.hpp>
#include <rgf/core/cast.hpp>

namespace rgf::app {

HMODULE SelfModule() noexcept {
  HMODULE module{nullptr};
  if (GetModuleHandleExW(
          /*dwFlags      = */ GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          /*lpModuleName = */ core::PointerCast<PCWSTR>(&SelfModule),
          /*phModule     = */ &module) == FALSE) {
    return nullptr;
  }

  return module;
}

}  // namespace rgf::app
