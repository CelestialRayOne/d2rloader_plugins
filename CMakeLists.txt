# D2RLoader plugins - one project, many DLLs.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build                                          # every plugin
#   cmake --build build --target d2rl-celestialrayone-extended-states  # just one
#
# Auto-deploy on build by pointing at your mod:
#   cmake -S . -B build -G Ninja -DD2R_MOD_DIR="F:/Diablo II Resurrected/mods/<YourMod>"
#
cmake_minimum_required(VERSION 3.28)
project(d2rloader-plugins LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(D2R_MOD_DIR "" CACHE PATH
    "Mod folder to copy built DLLs into. Leave empty to skip deployment.")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AddPlugin)

# --- SDK, vendored once, shared by every plugin ----------------------------
add_subdirectory(third_party/PluginSDK)

# --- Code shared between plugins -------------------------------------------
add_subdirectory(common)

# --- Plugins. One line each. -----------------------------------------------
d2rl_add_plugin(engine-stability)
d2rl_add_plugin(extended-states)
d2rl_add_plugin(miasma-chains-cap)
d2rl_add_plugin(cooldown-fix)
d2rl_add_plugin(playermode-exposing)
d2rl_add_plugin(extended-drop-cap)
