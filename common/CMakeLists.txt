# Shared helpers every plugin links. Static, so each DLL is standalone.
add_library(d2rl-common STATIC
    common.cpp
    common.h
)
target_include_directories(d2rl-common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(d2rl-common PUBLIC D2RLPlugin::D2RLPlugin)
