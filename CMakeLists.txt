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
set(BUILD_TESTING OFF)  # PluginSDK 0.3.0 builds its own tests by default
add_subdirectory(third_party/PluginSDK)

# --- Code shared between plugins -------------------------------------------
add_subdirectory(common)

# --- Plugins. One line each. -----------------------------------------------
d2rl_add_plugin(engine-stability)
d2rl_add_plugin(miasma-chains-cap)
d2rl_add_plugin(cooldown-fix)
d2rl_add_plugin(playermode-exposing)
d2rl_add_plugin(extended-drop-cap)
d2rl_add_plugin(critical-strike-damage)
d2rl_add_plugin(magic-find-tail)
d2rl_add_plugin(lightning-fury-caster-return)
d2rl_add_plugin(echoing-strike-qol)
d2rl_add_plugin(dodge-cap)
d2rl_add_plugin(cast-on-cast)
d2rl_add_plugin(hex-debuff-installer)
d2rl_add_plugin(clone-item)
d2rl_add_plugin(attack-rating-overhaul)
d2rl_add_plugin(whirlwind-follow-cursor)
d2rl_add_plugin(summon-resist-cap)
d2rl_add_plugin(faster-cast-rate-cap)
d2rl_add_plugin(smite-hit-cap)
d2rl_add_plugin(charge-aoe-hits)
d2rl_add_plugin(armageddon-bootstrap-fix)
d2rl_add_plugin(prgstack-calc-field)
d2rl_add_plugin(srcdam-calc-field)
d2rl_add_plugin(protect-experience-loss)
d2rl_add_plugin(state-protection-finishers)
d2rl_add_plugin(crushing-blow-modifier)
d2rl_add_plugin(damage-overflow-fix)
d2rl_add_plugin(poison-immunity-state)
d2rl_add_plugin(max-life-one)
d2rl_add_plugin(bleed-damage)
d2rl_add_plugin(soft-hits)
d2rl_add_plugin(aura-reactivation)
d2rl_add_plugin(left-click-auras)
d2rl_add_plugin(charm-dedup)
d2rl_add_plugin(item-quantity)
d2rl_add_plugin(holy-freeze-boss-damage)
d2rl_add_plugin(evil-urn-block-list)
d2rl_add_plugin(crossbow-charges)
d2rl_add_plugin(spelldesc-decimals)
d2rl_add_plugin(petmax-fix)
d2rl_add_plugin(blood-boil-enhancements)
d2rl_add_plugin(phasing)
d2rl_add_plugin(unusable-items)
d2rl_add_plugin(stance-dance)
d2rl_add_plugin(restrict-state)
d2rl_add_plugin(ally-ignore-list)
d2rl_add_plugin(monster-display)
d2rl_add_plugin(performance-improvements)
d2rl_add_plugin(profiler)
d2rl_add_plugin(affix-color-fix)
d2rl_add_plugin(transfer-to-stash)
