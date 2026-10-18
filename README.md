# RenderGraft

RenderGraft is evolving from [AimbotFramework](https://github.com/kxigor/AimbotFramework) into a C++23 framework for dynamic instrumentation of graphical applications on Windows. The goal is to reuse DLL injection, function interception, access to application state, and changes to graphics and behavior across different 2D and 3D applications. AI is intended to assist with application analysis and adapter development.

## What already works

The original example is single-player Far Cry 3 using Direct3D 11. `injector_fc3.exe` loads `aimbot_fc3.dll`; the module hooks `IDXGISwapChain::Present` and game entity updates, reads the camera and world state, projects 3D objects onto the screen, and draws a Dear ImGui overlay. Implemented features include ESP, target selection and aim assist, reversible patches to game instructions, a menu, diagnostics, and DLL unloading that restores hooks and patches.

Main components carried over from AimbotFramework:

| Modules | Purpose |
| --- | --- |
| `rgf::win`, `rgf::remote` | RAII for Win32/COM, process lookup, and DLL injection |
| `rgf::process`, `rgf::hook` | Memory reads, module access, signature scanning, VMT/WndProc hooks, and reversible patches |
| `rgf::render`, `rgf::app` | D3D11 overlay, per-frame callback, application session, hook management, and logging |
| `rgf::module` | Common DLL bootstrap: running a callback on a worker thread and unloading after the session ends |
| `rgf::math` | 3D geometry, matrices, and projection to screen coordinates |
| `rgf::games::fc3`, `rgf::esp`, `rgf::aimbot` | Far Cry 3 adapter, entity visualization, and game logic |
| `rgf::menu`, `rgf::config` | Interface and settings for the current example |

The framework uses the `rgf::*` namespace and CMake aliases, with public headers under `include/rgf/`. `rgf::app` accepts an implementation through `rgf::app::Game`, but installs the D3D11 hook itself; ESP and aim assist still depend on Far Cry 3. Doom 3 and Quake II RTX have skeleton implementations: OpenGL/Vulkan backends and inline/trampoline hooks are not implemented yet.

`DllMain` delegates to `rgf::module::DllEntry(instance, reason, &OnAttach)`. The `void OnAttach()` callback runs the entire session; before returning, it must finish its work and remove its hooks. `app::Run()` releases session resources and returns, then the callback's local objects are destroyed and the bootstrap unloads the DLL. If an unexpected exception escapes the callback, the worker thread exits and leaves the DLL loaded.

## Development direction

Extract a common instrumentation core, graphics backends, and adapters for specific applications. Each additional application should connect through an adapter and the required graphics mechanism; a 2D scenario does not require a game world model. Far Cry 3 remains the working example while the common layer is extracted.

## Next scenario: Telegram Desktop

A planned adapter will visually hide sensitive content during screen sharing in the installed Telegram Desktop client. The specific elements and interception method will be chosen after research.

Planning starts with studying Qt Widgets, `QPainter`, paint events, and the actual rendering path in the selected Telegram version, followed by materials on DLL injection and graphics hooks in this stack. The starting points are the [Telegram Desktop source code](https://github.com/telegramdesktop/tdesktop) and [QWidget documentation](https://doc.qt.io/qt-6/qwidget.html).

## Building the current example

Requirements are MSYS2, a compiler with C++23 support, CMake 3.25+, Ninja, and GoogleTest for tests. Dear ImGui is included as a git submodule:

```sh
git submodule update --init --recursive
```

Development and tests in MSYS2 UCRT64:

```sh
cmake --preset dev-debug
cmake --build --preset dev-debug
ctest --preset dev-debug
```

Running in 32-bit Far Cry 3 requires an x86 build in MSYS2 MINGW32 with CMake and Ninja available:

```sh
cmake --preset dev-debug-x86
cmake --build --preset dev-debug-x86
ctest --preset dev-debug-x86
```

After starting `farcry3_d3d11.exe`, run from the repository root:

```sh
./build/dev-debug-x86/injector/injector_fc3.exe
```

The launcher runs without arguments. Its entry point specifies the process name and DLL name, while the common code performs the injection. The current launcher is in `injector/fc3/main.cpp` and uses `farcry3_d3d11.exe` + `aimbot_fc3.dll`. It looks for the DLL next to the launcher. Each additional adapter gets its own short entry point using the same common implementation.

The launcher, DLL, and target process must use matching architectures. A successful DLL load and completed callback initialization are separate states: the callback starts asynchronously.

`INSERT` toggles the menu; `Unload` ends the session and unloads the DLL. The Telegram adapter is not implemented yet.

License: [Educational Source-Available License](LICENSE), for educational and non-commercial use.
