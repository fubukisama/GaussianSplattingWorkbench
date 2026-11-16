# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(GswNativeFirstInstall LANGUAGES CXX)

# This carrier is deliberately separate from the fixed-directory Setup,
# installed launcher, and application build. No public pin is configured.
include(FirstInstallTarget.cmake)
add_executable(gsw_native_first_install ../src/NativeFirstInstallMain.cpp)
gsw_configure_first_install(gsw_native_first_install)
set_target_properties(gsw_native_first_install PROPERTIES OUTPUT_NAME "GSW Native First Install")
