# =========================================================
# rgf::hook  (STATIC)
# =========================================================
# Function-interception primitives. Vtable names the layout of a virtual method
# table and hands out the address of a slot; VmtHook swaps one such slot,
# which is how COM interfaces such as IDXGISwapChain are intercepted; WndProcHook
# replaces a window procedure through SetWindowLongPtrW. Both put back what they
# replaced on destruction. Windows-only, so it is added from src/ only under
# WIN32.

add_library(rgf_hook STATIC
  vtable.cpp
  patch.cpp
  held_patch.cpp
  vmt_hook.cpp
  wnd_proc_hook.cpp
  error.cpp
)
add_library(rgf::hook ALIAS rgf_hook)

target_compile_features(rgf_hook PUBLIC cxx_std_23)

# Public headers live in the top-level include/ (sibling of src/).
target_include_directories(rgf_hook
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

# ProtectGuard is an implementation detail of the patching itself, so rgf::win
# is linked privately.
target_link_libraries(rgf_hook PRIVATE rgf::win)

if(TARGET project_options)
  target_link_libraries(rgf_hook PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_hook PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_hook PRIVATE project_coverage)
endif()
