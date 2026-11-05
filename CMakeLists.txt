cmake_minimum_required(VERSION 3.21)

project(ModKeybindsMenu VERSION 1.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(deps/RED4ext.SDK)

add_library(ModKeybindsMenu SHARED
  src/Main.cpp
  src/Keybinds.cpp
  src/Keybinds.hpp
  src/UserSettings.cpp
  src/UserSettings.hpp
  src/InputOverrides.cpp
  src/InputOverrides.hpp
  src/NativeSettings.cpp
  src/NativeSettings.hpp
  src/Rcf.cpp
  src/Rcf.hpp
  src/InputKeyNames.hpp
  deps/pugixml/src/pugixml.cpp
)

target_include_directories(ModKeybindsMenu PRIVATE deps/pugixml/src deps/json)
target_compile_definitions(ModKeybindsMenu PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

# Test builds read back live input mappings (natives ModKeybindsMenu_TestMapping, ModKeybindsMenu_TestHash).
option(MKM_TEST "Build test natives" OFF)
if(MKM_TEST)
  target_compile_definitions(ModKeybindsMenu PRIVATE MKM_TEST)
endif()
target_compile_options(ModKeybindsMenu PRIVATE /W3 /utf-8)
target_link_libraries(ModKeybindsMenu PRIVATE RED4ext::SDK shell32 ole32)

# Package layout: red4ext/plugins/ModKeybindsMenu/{ModKeybindsMenu.dll, scripts/} and the CET companion
set(PKG "${CMAKE_SOURCE_DIR}/package/red4ext/plugins/ModKeybindsMenu")
# Companion for Native Settings UI key bindings, run by CET when installed
set(CET "${CMAKE_SOURCE_DIR}/package/bin/x64/plugins/cyber_engine_tweaks/mods/ModKeybindsMenu")
# Optional file: vanilla controller entries and the Control Scheme diagram (scripts-optional)
set(OPT "${CMAKE_SOURCE_DIR}/package/optional/red4ext/plugins/ModKeybindsMenu")
add_custom_command(TARGET ModKeybindsMenu POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${PKG}/scripts"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG}/scripts"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ModKeybindsMenu>" "${PKG}/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/scripts" "${PKG}/scripts"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${OPT}/scripts"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${OPT}/scripts"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/scripts-optional" "${OPT}/scripts"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CET}"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/cet/init.lua" "${CET}/"
)
