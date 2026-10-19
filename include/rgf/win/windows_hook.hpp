#pragma once

#include <windows.h>

#include <expected>
#include <system_error>

namespace rgf::win {

// TODO(user): Revisit registration ownership versus hook lifecycle semantics.
class WindowsHook {
 public:
  explicit WindowsHook(HHOOK hook) noexcept;

  WindowsHook(const WindowsHook&) = delete;
  WindowsHook& operator=(const WindowsHook&) = delete;

  WindowsHook(WindowsHook&& other) noexcept;
  WindowsHook& operator=(WindowsHook&&) = delete;

  ~WindowsHook();

  [[nodiscard]] HHOOK Get() const noexcept;
  [[nodiscard]] HHOOK Release() noexcept;
  [[nodiscard]] std::expected<void, std::error_code> Reset() noexcept;

 private:
  HHOOK hook_;
};

}  // namespace rgf::win
