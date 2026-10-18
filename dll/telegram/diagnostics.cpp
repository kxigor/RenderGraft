#include "diagnostics.hpp"

// clang-format off
#include <windows.h>

#include <psapi.h>
// clang-format on

#include <array>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <rgf/win/error.hpp>
#include <rgf/win/module_view.hpp>
#include <rgf/win/scoped_handle.hpp>
#include <string>
#include <string_view>
#include <system_error>

namespace rgf::telegram {

namespace {

constexpr WCHAR kModuleAnchor{L'\0'};
constexpr std::size_t kReportChars{512};

[[nodiscard]] std::expected<std::filesystem::path, std::error_code>
ReportPath() {
  const auto kModule{win::ModuleView::FromAddress(&kModuleAnchor)};
  if (!kModule) {
    return std::unexpected{kModule.error()};
  }
  auto path{kModule->Path()};
  if (!path) {
    return std::unexpected{path.error()};
  }
  *path += L".log";
  return path;
}

[[nodiscard]] std::expected<std::string, std::error_code> ReadReport() {
  const auto kImage{win::ModuleView::Executable()};
  if (!kImage) {
    return std::unexpected{kImage.error()};
  }

  MODULEINFO image{};
  if (GetModuleInformation(
          /*hProcess   = */ GetCurrentProcess(),
          /*hModule    = */ kImage->Get(),
          /*lpmodinfo  = */ &image,
          /*cb         = */ static_cast<DWORD>(sizeof(image))) == FALSE) {
    return std::unexpected{win::LastError()};
  }

  std::array<char, kReportChars> text{};
  const int kLength{std::snprintf(
      text.data(), text.size(),
      "[rgf/telegram]\nprocess_id=%lu\nworker_thread_id=%lu\n"
      "pointer_bits=%zu\nimage_base=%p\nimage_size=%lu\nentry_point=%p\n"
      "completed\n",
      GetCurrentProcessId(), GetCurrentThreadId(), sizeof(void*) * 8U,
      image.lpBaseOfDll, image.SizeOfImage, image.EntryPoint)};
  if (kLength < 0 || static_cast<std::size_t>(kLength) >= text.size()) {
    return std::unexpected{std::make_error_code(std::errc::value_too_large)};
  }

  return std::string{text.data(), static_cast<std::size_t>(kLength)};
}

[[nodiscard]] std::expected<void, std::error_code> WriteReport(
    const std::filesystem::path& path, std::string_view text) {
  const win::ScopedHandle kFile{CreateFileW(
      /*lpFileName            = */ path.c_str(),
      /*dwDesiredAccess       = */ GENERIC_WRITE,
      /*dwShareMode           = */ FILE_SHARE_READ,
      /*lpSecurityAttributes  = */ nullptr,
      /*dwCreationDisposition = */ CREATE_ALWAYS,
      /*dwFlagsAndAttributes  = */ FILE_ATTRIBUTE_NORMAL,
      /*hTemplateFile         = */ nullptr)};
  if (!kFile) {
    return std::unexpected{win::LastError()};
  }

  DWORD written{};
  if (WriteFile(
          /*hFile                  = */ kFile.Get(),
          /*lpBuffer               = */ text.data(),
          /*nNumberOfBytesToWrite  = */ static_cast<DWORD>(text.size()),
          /*lpNumberOfBytesWritten = */ &written,
          /*lpOverlapped           = */ nullptr) == FALSE) {
    return std::unexpected{win::LastError()};
  }
  if (written != text.size()) {
    return std::unexpected{std::make_error_code(std::errc::io_error)};
  }

  return {};
}

void ReportFailure(const char* step, const std::error_code& error) {
  const std::string kText{std::string{"[rgf/telegram] "} + step + ": " +
                          error.message() + "\n"};
  OutputDebugStringA(kText.c_str());
}

}  // namespace

void RunDiagnostics() noexcept {
  try {
    const auto kPath{ReportPath()};
    if (!kPath) {
      ReportFailure("resolving the report path", kPath.error());
      return;
    }
    const auto kReport{ReadReport()};
    if (!kReport) {
      ReportFailure("reading application diagnostics", kReport.error());
      return;
    }
    OutputDebugStringA(kReport->c_str());
    const auto kWritten{WriteReport(*kPath, *kReport)};
    if (!kWritten) {
      ReportFailure("writing the report", kWritten.error());
    }
  } catch (const std::exception& error) {
    OutputDebugStringA("[rgf/telegram] diagnostics failed: ");
    OutputDebugStringA(error.what());
    OutputDebugStringA("\n");
  } catch (...) {
    OutputDebugStringA("[rgf/telegram] diagnostics failed\n");
  }
}

}  // namespace rgf::telegram
