# Shared helpers every ESR plugin links. Static, so each DLL is standalone.
add_library(esr-common STATIC
    esr_common.cpp
    esr_common.h
)
target_include_directories(esr-common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(esr-common PUBLIC D2RLPlugin::D2RLPlugin)
