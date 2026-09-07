# d2rl_add_plugin(<name>)
#
# Builds plugins/<name>/*.cpp and plugins/<name>/*.rc into
# d2rl-${D2RL_PLUGIN_AUTHOR}-<name>.dll, links the SDK and the shared common
# library, and copies the result into D2R_MOD_DIR if that is set.
#
# Adding a plugin is: create plugins/<name>/, drop sources in, add one
# d2rl_add_plugin(<name>) line to the root CMakeLists.txt.
#
set(D2RL_PLUGIN_AUTHOR "celestialrayone" CACHE STRING
    "Author token used in DLL names: d2rl-<author>-<plugin>.dll")

function(d2rl_add_plugin name)
    set(dir "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${name}")
    if(NOT EXISTS "${dir}")
        message(FATAL_ERROR "d2rl_add_plugin: ${dir} does not exist")
    endif()

    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${dir}/*.cpp" "${dir}/*.h" "${dir}/*.rc")
    if(NOT sources)
        message(FATAL_ERROR "d2rl_add_plugin: no sources in ${dir}")
    endif()

    set(target "d2rl-${D2RL_PLUGIN_AUTHOR}-${name}")
    add_library(${target} SHARED ${sources})

    target_include_directories(${target} PRIVATE "${dir}")
    target_link_libraries(${target} PRIVATE D2RLPlugin::D2RLPlugin d2rl-common)

    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${target}")

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()

    if(D2R_MOD_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${D2R_MOD_DIR}/d2rloader/plugins"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${target}>"
                    "${D2R_MOD_DIR}/d2rloader/plugins/"
            COMMENT "Deploying ${target} to ${D2R_MOD_DIR}")
    endif()

    message(STATUS "plugin: ${target}")
endfunction()
