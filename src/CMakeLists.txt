# =========================================================
# LIBRARIES
# =========================================================
# Reusable logic split into focused static libraries, layered by dependency.
# Public headers live one level up, in the top-level include/ (sibling of src/),
# so every library shares the same include root: include/rgf/...
#
#   rgf::process      generic access to the host process (memory, modules, scan)
#   rgf::games::<t>   per-title, engine-specific bindings; depend on rgf::process
#   rgf::aimbot       target choice and the geometry of pointing at one
#   rgf::config       settings shared by the overlay's consumers
#   rgf::win          RAII wrappers over raw Win32 resources (handles, memory)
#   rgf::remote       Windows DLL-injection helpers (Toolhelp + CreateRemoteThread)
#   rgf::hook         function interception (virtual method table, window proc)
#   rgf::render       owns the Present hook; the overlay's foundation
#   rgf::menu         the overlay's user interface
#   rgf::esp          entity boxes projected into screen space
#   rgf::app          the injected DLL's session, with no title-specific parts
#
# Dependency direction: games::* -> process ; render -> hook -> win ; menu and
# esp are siblings on top of render, sharing state only through config, so
# neither one requires the other to run. rgf::app sits on top of render and
# menu and knows no game; the DLL shim supplies one through rgf::app::Game.
#
# WHAT IS NOT YET TITLE-NEUTRAL, stated plainly so a second title does not
# discover it the hard way:
#
#   rgf::esp     links rgf::games::fc3 and names EntityInfo, Kind, CameraBasis
#                and Diagnostics in its own internal/ headers. It reads more of
#                an entity than the aimbot does -- the transform and the box for
#                the outlines, the head and spine for the aim box -- so it is
#                the harder of the two to lift off a title, not the easier.
#   rgf::render  is Direct3D 11 and DXGI. Only include/rgf/render/overlay.hpp is
#                API-neutral; everything naming IDXGISwapChain lives under
#                render/dxgi/. A title on OpenGL or Vulkan needs a sibling
#                backend, not a change here.
#   rgf::app     knows no game, but it does drive the Present hook directly
#                through rgf::render::dxgi, so it inherits that backend.
#   rgf::config  carries Far Cry 3's taxonomy (allies, civilians, animals,
#                birds, vehicles) in a Fc3Settings section. Enemies and props
#                stay at the top level because every title has both.

add_subdirectory(config)
add_subdirectory(math)

# Windows-only libraries; skip on non-Windows so the rest still configures for
# a Linux/macOS consumer. Ordered by dependency: win before remote and hook,
# hook before render, render before its two consumers. process reads this
# process's own memory through the Win32 API, so games and aimbot, which build
# on it, are Windows-only too.
if(WIN32)
  add_subdirectory(win)
  add_subdirectory(module)
  add_subdirectory(remote)
  add_subdirectory(hook)
  add_subdirectory(qt)
  add_subdirectory(telegram)
  add_subdirectory(process)
  add_subdirectory(games)
  add_subdirectory(aimbot)
  add_subdirectory(render)
  add_subdirectory(menu)
  add_subdirectory(esp)
  add_subdirectory(app)
endif()
