# SPDX-License-Identifier: GPL-3.0-or-later
set(GSW_FIRST_INSTALL_MANIFEST_SHA256 "" CACHE STRING "SHA-256 of the independently authorized protocol-2 bootstrap.json")

function(gsw_configure_first_install target)
  if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Native first install requires Windows x64 and MSVC")
  endif()
  string(LENGTH "${GSW_FIRST_INSTALL_MANIFEST_SHA256}" pin_length)
  if(NOT pin_length EQUAL 64 OR NOT GSW_FIRST_INSTALL_MANIFEST_SHA256 MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "First install requires an explicit lowercase SHA-256 bootstrap manifest pin")
  endif()
  set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  target_sources(${target} PRIVATE "${root}/src/NativeFirstInstall.cpp")
  set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_include_directories(${target} PRIVATE "${root}/src" "${root}/third_party/nlohmann")
  target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00
    GSW_FIRST_INSTALL_MANIFEST_SHA256="${GSW_FIRST_INSTALL_MANIFEST_SHA256}")
  target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  target_link_options(${target} PRIVATE "SHELL:/MANIFESTUAC:\"level='asInvoker' uiAccess='false'\"")
  target_link_libraries(${target} PRIVATE advapi32 bcrypt ole32)
endfunction()
