# =========================================================
# rgf::win  (STATIC)
# =========================================================
# Thin RAII wrappers over raw Win32 resources: ScopedHandle (CloseHandle),
# RemoteMemory (VirtualFreeEx in another process), ProtectGuard (VirtualProtect),
# plus two header-only helpers -- LastError (error.hpp), which turns GetLastError
# into a std::error_code, and ComPtr (com_ptr.hpp), which releases a COM
# interface. General Win32 plumbing, reused by rgf::remote, rgf::hook and the
# injector. Windows-only, so it is added from src/ only under WIN32.

add_library(rgf_win STATIC
  scoped_handle.cpp
  remote_memory.cpp
  protect_guard.cpp
)
add_library(rgf::win ALIAS rgf_win)

target_compile_features(rgf_win PUBLIC cxx_std_23)

# Public headers live in the top-level include/ (sibling of src/).
target_include_directories(rgf_win
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

if(TARGET project_options)
  target_link_libraries(rgf_win PRIVATE project_options)
endif()
if(TARGET project_sanitizers)
  target_link_libraries(rgf_win PRIVATE project_sanitizers)
endif()
if(TARGET project_coverage)
  target_link_libraries(rgf_win PRIVATE project_coverage)
endif()
