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

TODO: Design a coherent logging architecture with singleton access and logger factories for stream or file output, including ownership, lifetime, and error handling.

## Telegram Desktop

A planned adapter will visually hide sensitive content during screen sharing in the installed Telegram Desktop client. The specific elements and interception method will be chosen after research.

The initial target is Telegram Desktop 7.3.1 x64 with statically linked Qt 5.15.19. A reversible rendering probe draws test markers through the client's Qt paint engine. The next step is to select the sensitive interface elements and relate them to application state.

The starting points are Qt Widgets, `QPainter`, paint events, the [Telegram Desktop source code](https://github.com/telegramdesktop/tdesktop/tree/v7.3.1), and [Qt 5.15 QWidget documentation](https://doc.qt.io/archives/qt-5.15/qwidget.html).

### Milestones

| Milestone | Result | Status |
| --- | --- | --- |
| T1 | Basic DLL injection into Telegram, runtime diagnostics, and automatic unload while the client keeps running | done |
| T2 | Identify the selected build's Qt rendering path and validate candidate hook points against the running application | done |
| T3 | Intercept the selected rendering path, draw a test marker, and restore normal rendering when the hook is removed | done |
| T4 | Choose the sensitive interface elements to hide and relate their painted regions to the relevant internal application state | next |
| T5 | Implement masking through the rendering hook, with an on/off control; verify the result during screen capture and restore normal rendering on disable or unload | planned |
| T6 | Separate the shared instrumentation and Qt graphics mechanisms from Telegram-specific state, signatures, and masking rules | planned |
| T7 | Document a reproducible AI-assisted adapter workflow: inputs, tools, proposed hook points, and checks that accept or reject the findings | planned |
| T8 | Evaluate masking correctness, runtime overhead, and repeated attach/unload; document the tested Telegram build and limitations | planned |

Work through these milestones in order and update their status after verification. The selected interface elements, hook mechanism, and evaluation criteria will be refined during implementation. T7 will describe the resulting adapter workflow.

### Rendering probe

After building `dev-debug`, open a chat with visible messages in the supported 64-bit Telegram Desktop build and run:

```sh
./build/dev-debug/injector/injector_telegram.exe
```

The launcher loads `rgf_telegram.dll` into `Telegram.exe`. For 15 seconds, opaque purple rectangles cover text rendered through `QRasterPaintEngine::drawTextItem` while `HistoryView::Message::draw` is active. Each rectangle follows the text run's width, ascent, and descent, with a small margin. The Qt hook uses a temporary private vtable on the current paint engine. Hook installation and removal run on the GUI thread.

The probe requests a repaint when starting and stopping, restores the hooks, and unloads automatically while Telegram keeps running. Runtime diagnostics and call counts are written to `rgf_telegram.dll.log` next to the DLL and to the debugger output. Other builds are rejected before patching; an uncertain cleanup keeps the DLL loaded and reports the failure.

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

For the Far Cry 3 example, `INSERT` toggles the menu; `Unload` ends the session and unloads the DLL. The Telegram graphics adapter is not implemented yet.

License: [Educational Source-Available License](LICENSE), for educational and non-commercial use.
