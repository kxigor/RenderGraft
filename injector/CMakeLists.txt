if(NOT WIN32)
  message(FATAL_ERROR "The injector targets Windows. Configure under MSYS2 UCRT64 (or MSVC).")
endif()

add_library(rgf_injector_core STATIC
  injector.cpp
)
add_library(rgf::injector ALIAS rgf_injector_core)
target_compile_features(rgf_injector_core PUBLIC cxx_std_23)
target_include_directories(rgf_injector_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rgf_injector_core PRIVATE rgf::remote rgf::win)

add_executable(rgf_injector
  fc3/main.cpp
)
set_target_properties(rgf_injector PROPERTIES
  OUTPUT_NAME "injector_fc3"
)

target_link_libraries(rgf_injector PRIVATE rgf::injector)

add_executable(rgf_injector_telegram
  telegram/main.cpp
)
set_target_properties(rgf_injector_telegram PROPERTIES
  OUTPUT_NAME "injector_telegram"
)

target_link_libraries(rgf_injector_telegram PRIVATE rgf::injector)

foreach(rgf_injector_target IN ITEMS
    rgf_injector_core rgf_injector rgf_injector_telegram)
  if(TARGET project_options)
    target_link_libraries(${rgf_injector_target} PRIVATE project_options)
  endif()
  if(TARGET project_sanitizers)
    target_link_libraries(${rgf_injector_target} PRIVATE project_sanitizers)
  endif()
  if(TARGET project_coverage)
    target_link_libraries(${rgf_injector_target} PRIVATE project_coverage)
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
      NOT rgf_injector_target STREQUAL "rgf_injector_core")
    target_link_options(${rgf_injector_target} PRIVATE
      -static
      -static-libgcc
      -static-libstdc++
    )
  endif()
endforeach()

if(TARGET aimbot_fc3)
  set(rgf_staged_dll
      $<TARGET_FILE_DIR:rgf_injector>/$<TARGET_FILE_NAME:aimbot_fc3>)

  add_custom_target(rgf_stage_dll ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:aimbot_fc3> $<TARGET_FILE_DIR:rgf_injector>
    COMMENT "Staging aimbot_fc3.dll next to injector_fc3.exe"
    VERBATIM
  )

  if(CMAKE_STRIP)
    add_custom_command(TARGET rgf_stage_dll POST_BUILD
      COMMAND ${CMAKE_STRIP} --strip-debug ${rgf_staged_dll}
      COMMENT "Stripping debug information from the staged aimbot_fc3.dll"
      VERBATIM
    )
  endif()

  add_dependencies(rgf_stage_dll aimbot_fc3 rgf_injector)
endif()

if(TARGET rgf_telegram)
  set(rgf_staged_telegram_dll
      $<TARGET_FILE_DIR:rgf_injector_telegram>/$<TARGET_FILE_NAME:rgf_telegram>)

  add_custom_target(rgf_stage_telegram_dll ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:rgf_telegram> $<TARGET_FILE_DIR:rgf_injector_telegram>
    COMMENT "Staging rgf_telegram.dll next to injector_telegram.exe"
    VERBATIM
  )

  if(CMAKE_STRIP)
    add_custom_command(TARGET rgf_stage_telegram_dll POST_BUILD
      COMMAND ${CMAKE_STRIP} --strip-debug ${rgf_staged_telegram_dll}
      COMMENT "Stripping debug information from the staged rgf_telegram.dll"
      VERBATIM
    )
  endif()

  add_dependencies(rgf_stage_telegram_dll rgf_telegram rgf_injector_telegram)
endif()
