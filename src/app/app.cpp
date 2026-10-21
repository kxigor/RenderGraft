#include <windows.h>

#include <atomic>
#include <exception>
#include <expected>
#include <rgf/app/app.hpp>
#include <rgf/app/detour_guard.hpp>
#include <rgf/app/fault.hpp>
#include <rgf/app/game.hpp>
#include <rgf/app/log.hpp>
#include <rgf/app/scoped_hooks.hpp>
#include <rgf/config/config.hpp>
#include <rgf/hook/hook.hpp>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/hook/wnd_proc_hook.hpp>
#include <rgf/menu/menu.hpp>
#include <rgf/render/dxgi/overlay.hpp>
#include <rgf/render/dxgi/present_hook.hpp>
#include <rgf/render/dxgi/probe.hpp>
#include <rgf/render/overlay.hpp>
#include <rgf/win/scoped_handle.hpp>
#include <system_error>

namespace rgf::app {

namespace {

constexpr DWORD kWindowPollMilliseconds{50};
constexpr int kWindowPollTicks{200};

[[nodiscard]] std::atomic<Game*>& CurrentGame() noexcept {
  static std::atomic<Game*> game{nullptr};
  return game;
}

[[nodiscard]] HANDLE UnloadEvent() noexcept {
  static const win::ScopedHandle kEvent{CreateEventW(
      /*lpEventAttributes = */ nullptr,
      /*bManualReset      = */ TRUE,
      /*bInitialState     = */ FALSE,
      /*lpName            = */ nullptr)};

  return kEvent.Get();
}

[[nodiscard]] WNDPROC& OriginalWndProc() noexcept {
  static WNDPROC original{nullptr};
  return original;
}

class InputBinding {
 public:
  explicit InputBinding(const hook::Hook& hook) noexcept {
    OriginalWndProc() = hook.Original<WNDPROC>();
  }

  InputBinding(const InputBinding&) = delete;
  InputBinding& operator=(const InputBinding&) = delete;

  InputBinding(InputBinding&&) = delete;
  InputBinding& operator=(InputBinding&&) = delete;

  ~InputBinding() { OriginalWndProc() = nullptr; }
};

LRESULT __stdcall WndProcDetour(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  const DetourGuard kInside{};

  if (menu::HandleMessage(window, message, wParam, lParam)) {
    return TRUE;
  }

  WNDPROC original{OriginalWndProc()};
  if (original == nullptr) {
    return DefWindowProcW(window, message, wParam, lParam);
  }

  return CallWindowProcW(
      /*lpPrevWndFunc = */ original,
      /*hWnd          = */ window,
      /*Msg           = */ message,
      /*wParam        = */ wParam,
      /*lParam        = */ lParam);
}

void DrawOverlay() noexcept {
  menu::Draw();

  Game* game{CurrentGame().load()};
  if (game != nullptr) {
    game->OnFrame();
  }
}

HRESULT __stdcall PresentDetour(IDXGISwapChain* swapChain, UINT syncInterval,
                                UINT flags) {
  const DetourGuard kInside{};

  const std::expected<void, std::error_code> kDrawn{
      render::dxgi::DrawOverlayFrame(swapChain, &DrawOverlay)};
  if (!kDrawn) {
    static bool reported{false};
    if (!reported) {
      reported = true;
      LogFailure("drawing the overlay", kDrawn.error());
    }
  }

  if (config::Current().unloadRequested) {
    SetEvent(
        /*hEvent = */ UnloadEvent());
  }

  render::dxgi::PresentFn original{render::dxgi::OriginalPresent()};
  if (original == nullptr) {
    return E_FAIL;
  }

  return original(swapChain, syncInterval, flags);
}

void LogAttached(const Game& game) {
  Logf("[rgf] attached to %s\n", game.Name());
}

void LogTable(const hook::Vtable& table) {
  Logf("[rgf] vtable at %p, Present at %p\n",
       static_cast<const void*>(table.Get()),
       table.At(render::dxgi::kPresentSlot));
}

void RunUntilUnload() {
  Log("[rgf] running\n");
  WaitForSingleObject(
      /*hHandle        = */ UnloadEvent(),
      /*dwMilliseconds = */ INFINITE);
  Log("[rgf] unload requested\n");
}

[[nodiscard]] HWND WaitForOverlayWindow() noexcept {
  for (int tick{0}; tick < kWindowPollTicks; ++tick) {
    HWND window{render::OverlayWindow()};
    if (window != nullptr) {
      return window;
    }
    Sleep(
        /*dwMilliseconds = */ kWindowPollMilliseconds);
  }

  return nullptr;
}

void AddInputHook(ScopedHooks& hooks) {
  HWND window{WaitForOverlayWindow()};
  if (window == nullptr) {
    Log("[rgf] no overlay window, input not hooked\n");
    return;
  }

  hooks.Add<InputBinding>(hook::WndProcHook::CreateDynamic(window),
                          &WndProcDetour, "input");
}

void RunSession(Game& game) {
  const std::expected<hook::Vtable, std::error_code> kTable{
      render::dxgi::ProbeSwapChainVtable()};
  if (!kTable) {
    LogFailure("probing the swap chain vtable", kTable.error());
    return;
  }
  LogTable(*kTable);

  ScopedHooks hooks;
  if (!hooks.Add<render::dxgi::PresentBinding>(
          hook::VmtHook::Create(*kTable, render::dxgi::kPresentSlot),
          &PresentDetour, "Present")) {
    return;
  }

  game.InstallHooks(hooks);
  AddInputHook(hooks);

  RunUntilUnload();
}

}  // namespace

void Run(Game& game) noexcept {
  LogAttached(game);
  CurrentGame().store(&game);

  {
    const FaultLogger kFaults{};
    try {
      RunSession(game);
    } catch (const std::exception& error) {
      Logf("[rgf] session failed: %s\n", error.what());
    } catch (...) {
      Log("[rgf] session failed with an unknown exception\n");
    }
  }

  WaitForDetours();
  CurrentGame().store(nullptr);
  render::ShutdownOverlay();
  Log("[rgf] session stopped\n");
}

}  // namespace rgf::app
