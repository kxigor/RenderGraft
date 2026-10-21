#include <rgf/module/entry.hpp>
#include <rgf/win/scoped_handle.hpp>

namespace rgf::module {

namespace {

[[nodiscard]] AttachCallback& AttachHandler() noexcept {
  static AttachCallback callback{nullptr};
  return callback;
}

DWORD WINAPI RunModule(LPVOID parameter) {
  try {
    AttachHandler()();
  } catch (...) {
    OutputDebugStringA("[rgf] module callback failed; DLL remains loaded\n");
    return ERROR_UNHANDLED_EXCEPTION;
  }

  FreeLibraryAndExitThread(
      /*hLibModule = */ static_cast<HMODULE>(parameter),
      /*dwExitCode = */ ERROR_SUCCESS);
}

}  // namespace

BOOL DllEntry(HMODULE self, DWORD reason, AttachCallback onAttach) noexcept {
  if (reason != DLL_PROCESS_ATTACH) {
    return TRUE;
  }
  if (self == nullptr || onAttach == nullptr) {
    SetLastError(/*dwErrCode = */ ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  AttachHandler() = onAttach;
  const win::ScopedHandle kThread{CreateThread(
      /*lpThreadAttributes = */ nullptr,
      /*dwStackSize        = */ 0,
      /*lpStartAddress     = */ &RunModule,
      /*lpParameter        = */ self,
      /*dwCreationFlags    = */ 0,
      /*lpThreadId         = */ nullptr)};
  return kThread ? TRUE : FALSE;
}

}  // namespace rgf::module
