#include <rgf/win/error.hpp>
#include <rgf/win/module_view.hpp>
#include <string>

namespace rgf::win {

ModuleView::ModuleView(HMODULE module) noexcept : module_{module} {}

std::expected<ModuleView, std::error_code> ModuleView::Executable() {
  HMODULE module{GetModuleHandleW(/*lpModuleName = */ nullptr)};
  if (module == nullptr) {
    return std::unexpected{LastError()};
  }
  return ModuleView{module};
}

std::expected<ModuleView, std::error_code> ModuleView::FromAddress(
    const void* address) {
  if (address == nullptr) {
    return std::unexpected{std::make_error_code(std::errc::invalid_argument)};
  }
  HMODULE module{nullptr};
  if (GetModuleHandleExW(
          /*dwFlags      = */ GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          /*lpModuleName = */ static_cast<PCWSTR>(address),
          /*phModule     = */ &module) == FALSE) {
    return std::unexpected{LastError()};
  }
  return ModuleView{module};
}

std::expected<std::filesystem::path, std::error_code> ModuleView::Path() const {
  std::wstring buffer{};
  buffer.resize(MAX_PATH);
  while (true) {
    const DWORD kCapacity{static_cast<DWORD>(buffer.size())};
    const DWORD kLength{GetModuleFileNameW(
        /*hModule    = */ module_,
        /*lpFilename = */ buffer.data(),
        /*nSize      = */ kCapacity)};
    if (kLength == 0) {
      return std::unexpected{LastError()};
    }
    if (kLength < kCapacity) {
      buffer.resize(kLength);
      return std::filesystem::path{buffer};
    }
    if (buffer.size() > MAXDWORD / 2) {
      return std::unexpected{
          std::make_error_code(std::errc::filename_too_long)};
    }
    buffer.resize(buffer.size() * 2);
  }
}

HMODULE ModuleView::Get() const noexcept { return module_; }

}  // namespace rgf::win
