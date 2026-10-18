#pragma once

#include <windows.h>

#include <expected>
#include <filesystem>
#include <system_error>

namespace rgf::win {

class ModuleView {
 public:
  [[nodiscard]] static std::expected<ModuleView, std::error_code> Executable();
  [[nodiscard]] static std::expected<ModuleView, std::error_code> FromAddress(
      const void* address);

  [[nodiscard]] std::expected<std::filesystem::path, std::error_code> Path()
      const;
  [[nodiscard]] HMODULE Get() const noexcept;

 private:
  explicit ModuleView(HMODULE module) noexcept;

  HMODULE module_;
};

}  // namespace rgf::win
