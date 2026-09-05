# ESR D2RLoader plugins - one project, many DLLs.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build                       # every plugin
#   cmake --build build --target d2rl-esr-states   # just one
#
# Auto-deploy on build by pointing at your mod:
#   cmake -S . -B build -G Ninja -DD2R_MOD_DIR="F:/Diablo II Resurrected/mods/Eastern_Sun_Resurrected"
#
cmake_minimum_required(VERSION 3.28)
project(esr-plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(D2R_MOD_DIR "" CACHE PATH
    "Mod folder to copy built DLLs into. Leave empty to skip deployment.")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AddPlugin)

# --- SDK, cloned once, shared by every plugin ------------------------------
add_subdirectory(third_party/PluginSDK)

# --- Code shared between plugins -------------------------------------------
add_subdirectory(common)

# --- Plugins. One line each. -----------------------------------------------
esr_add_plugin(extended-states)
# esr_add_plugin(loot)
# esr_add_plugin(whatever)
