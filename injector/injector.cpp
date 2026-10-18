#include "injector.hpp"

#include <array>
#include <cstdio>
#include <cwchar>
#include <expected>
#include <filesystem>
#include <rgf/remote/remote.hpp>
#include <rgf/win/error.hpp>
#include <rgf/win/scoped_handle.hpp>
#include <system_error>

namespace rgf::injector {
namespace {

[[nodiscard]] std::expected<std::filesystem::path, std::error_code>
ResolveDllPath(PCWSTR dllName) {
  std::array<WCHAR, MAX_PATH> exePath{};
  const DWORD kLength{GetModuleFileNameW(
      /*hModule    = */ nullptr,
      /*lpFilename = */ exePath.data(),
      /*nSize      = */ static_cast<DWORD>(MAX_PATH))};
  if (kLength == 0) {
    return std::unexpected{win::LastError()};
  }
  if (kLength >= static_cast<DWORD>(MAX_PATH)) {
    return std::unexpected{
        std::error_code{ERROR_INSUFFICIENT_BUFFER, std::system_category()}};
  }
  return std::filesystem::path{exePath.data()}.parent_path() / dllName;
}

}  // namespace

int Run(const Target& target) {
  const std::expected<DWORD, std::error_code> kPid{
      remote::FindProcessByImageName(target.processImage)};
  if (!kPid) {
    std::fwprintf(stderr, L"[injector] target process not found: %ls (%hs)\n",
                  target.processImage, kPid.error().message().c_str());
    return 1;
  }

  constexpr DWORD kProcessAccess{
      PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
      PROCESS_VM_WRITE | PROCESS_VM_READ};
  const win::ScopedHandle kProc{OpenProcess(
      /*dwDesiredAccess = */ kProcessAccess,
      /*bInheritHandle  = */ FALSE,
      /*dwProcessId     = */ *kPid)};
  if (!kProc) {
    const std::error_code kError{win::LastError()};
    std::fwprintf(stderr, L"[injector] OpenProcess failed for pid %lu: %hs\n",
                  *kPid, kError.message().c_str());
    return 1;
  }

  const auto kDllPath{ResolveDllPath(target.dllName)};
  if (!kDllPath) {
    std::fwprintf(stderr, L"[injector] could not resolve the DLL path: %hs\n",
                  kDllPath.error().message().c_str());
    return 1;
  }

  const std::expected<void, std::error_code> kInjected{
      remote::InjectLibrary(kProc.Get(), kDllPath->c_str())};
  if (!kInjected) {
    std::fwprintf(stderr, L"[injector] injection failed: %hs\n",
                  kInjected.error().message().c_str());
    return 1;
  }

  std::wprintf(L"[injector] injected \"%ls\" into %ls (pid %lu)\n",
               kDllPath->c_str(), target.processImage, *kPid);
  return 0;
}

}  // namespace rgf::injector
