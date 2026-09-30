# jai_demoreel - demoscene tech reel: every effect is script, C++ is a thin console host.
# The host is a shared, script-runtime-agnostic core (demoreel_host) driven through a
# small adapter interface (demoreel_host.hpp) so the same reel can be ported to other
# embedded languages for comparison (see PORTING.md). jai_demoreel always builds; the
# comparison ports are opt-in (OFF by default) and need their runtime checked out.

function(demoreel_msvc_settings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /EHsc /permissive- /bigobj)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
endfunction()

# --- shared host core: console/VT filter, keys, timing, rng, flags, harnesses ---
add_library(demoreel_host STATIC demoreel_host.cpp)
target_include_directories(demoreel_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
demoreel_msvc_settings(demoreel_host)
if(WIN32)
    target_link_libraries(demoreel_host PUBLIC winmm)
endif()

# --- jai_demoreel: the JaiScript original (always built) ---
add_executable(jai_demoreel demoreel_adapter_jai.cpp)
target_link_libraries(jai_demoreel PRIVATE demoreel_host jaiscript)
demoreel_msvc_settings(jai_demoreel)

# The exe prefers the source scenes dir (live-editable for the 'r' hot reload),
# falling back to demoreel_scenes/ next to the binary.
target_compile_definitions(jai_demoreel PRIVATE
    JAI_DEMOREEL_SOURCE_SCENES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenes"
)

add_custom_command(TARGET jai_demoreel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scenes
        $<TARGET_FILE_DIR:jai_demoreel>/demoreel_scenes
)

# --- demoreel_lua: Lua 5.4 + sol2 port (opt-in; builds Lua from source) ---
option(DEMOREEL_PORT_LUA "Build demoreel_lua, the Lua/sol2 port of the demoreel" OFF)
if(DEMOREEL_PORT_LUA)
    set(DEMOREEL_LUA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../lua" CACHE PATH "Lua 5.4 source root")
    set(DEMOREEL_SOL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sol2" CACHE PATH "sol2 source root")
    if(NOT EXISTS "${DEMOREEL_LUA_ROOT}/src/lua.h" OR NOT EXISTS "${DEMOREEL_SOL2_ROOT}/include/sol/sol.hpp")
        message(FATAL_ERROR "DEMOREEL_PORT_LUA=ON but Lua/sol2 not found at ${DEMOREEL_LUA_ROOT} / ${DEMOREEL_SOL2_ROOT}")
    endif()
    enable_language(C)
    file(GLOB DEMOREEL_LUA_SOURCES "${DEMOREEL_LUA_ROOT}/src/*.c")
    list(FILTER DEMOREEL_LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
    add_library(demoreel_lua_runtime STATIC ${DEMOREEL_LUA_SOURCES})
    target_include_directories(demoreel_lua_runtime PUBLIC "${DEMOREEL_LUA_ROOT}/src")
    if(MSVC)
        target_compile_options(demoreel_lua_runtime PRIVATE /W3)
        set_property(TARGET demoreel_lua_runtime PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    elseif(UNIX)
        target_link_libraries(demoreel_lua_runtime PUBLIC m ${CMAKE_DL_LIBS})
    endif()
    add_executable(demoreel_lua demoreel_adapter_lua.cpp)
    target_link_libraries(demoreel_lua PRIVATE demoreel_host demoreel_lua_runtime)
    target_include_directories(demoreel_lua PRIVATE "${DEMOREEL_SOL2_ROOT}/include")
    target_compile_definitions(demoreel_lua PRIVATE
        DEMOREEL_LUA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ports/lua"
    )
    demoreel_msvc_settings(demoreel_lua)
    add_custom_command(TARGET demoreel_lua POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/ports/lua
            $<TARGET_FILE_DIR:demoreel_lua>/demoreel_lua_scenes
    )
    message(STATUS "demoreel: demoreel_lua enabled (${DEMOREEL_LUA_ROOT} + ${DEMOREEL_SOL2_ROOT})")
endif()

# --- demoreel_squirrel: Squirrel 3.2 port (opt-in; builds the runtime from source) ---
option(DEMOREEL_PORT_SQUIRREL "Build demoreel_squirrel, the Squirrel port of the demoreel" OFF)
set(DEMOREEL_SQUIRREL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../squirrel" CACHE PATH "Squirrel 3.2 source root")
if(DEMOREEL_PORT_SQUIRREL)
    if(NOT EXISTS "${DEMOREEL_SQUIRREL_ROOT}/include/squirrel.h")
        message(FATAL_ERROR "DEMOREEL_PORT_SQUIRREL=ON but Squirrel not found at ${DEMOREEL_SQUIRREL_ROOT}")
    endif()
    file(GLOB demoreel_sq_sources "${DEMOREEL_SQUIRREL_ROOT}/squirrel/*.cpp" "${DEMOREEL_SQUIRREL_ROOT}/sqstdlib/*.cpp")
    add_library(demoreel_squirrel_runtime STATIC ${demoreel_sq_sources})
    target_include_directories(demoreel_squirrel_runtime PUBLIC "${DEMOREEL_SQUIRREL_ROOT}/include")
    # double SQFloat for IEEE parity; _SQ64 self-selects on x64 (the adapter static_asserts both)
    target_compile_definitions(demoreel_squirrel_runtime PUBLIC SQUSEDOUBLE)
    if(MSVC)
        target_compile_options(demoreel_squirrel_runtime PRIVATE /W2)
        set_property(TARGET demoreel_squirrel_runtime PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
    add_executable(demoreel_squirrel demoreel_adapter_squirrel.cpp)
    target_link_libraries(demoreel_squirrel PRIVATE demoreel_host demoreel_squirrel_runtime)
    target_compile_definitions(demoreel_squirrel PRIVATE
        DEMOREEL_SQUIRREL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ports/squirrel"
    )
    demoreel_msvc_settings(demoreel_squirrel)
    add_custom_command(TARGET demoreel_squirrel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/ports/squirrel
            $<TARGET_FILE_DIR:demoreel_squirrel>/demoreel_squirrel_scenes
    )
endif()

# Matched cold-help layout controls for each enabled runtime.
option(DEMOREEL_PADDING_CONTROLS "Build demoreel placement controls with 40 cold-help padding statements" OFF)
if(DEMOREEL_PADDING_CONTROLS)
    add_library(demoreel_host_padding STATIC demoreel_host.cpp)
    target_include_directories(demoreel_host_padding PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(demoreel_host_padding PRIVATE DEMOREEL_LAYOUT_PADDING=1)
    demoreel_msvc_settings(demoreel_host_padding)
    if(WIN32)
        target_link_libraries(demoreel_host_padding PUBLIC winmm)
    endif()
    foreach(original jai_demoreel demoreel_lua demoreel_squirrel)
        if(TARGET ${original})
            get_target_property(sources ${original} SOURCES)
            get_target_property(definitions ${original} COMPILE_DEFINITIONS)
            get_target_property(includes ${original} INCLUDE_DIRECTORIES)
            get_target_property(libraries ${original} LINK_LIBRARIES)
            list(REMOVE_ITEM libraries demoreel_host)
            add_executable(${original}_padding ${sources})
            target_link_libraries(${original}_padding PRIVATE demoreel_host_padding ${libraries})
            if(definitions)
                target_compile_definitions(${original}_padding PRIVATE ${definitions})
            endif()
            if(includes)
                target_include_directories(${original}_padding PRIVATE ${includes})
            endif()
            demoreel_msvc_settings(${original}_padding)
        endif()
    endforeach()
endif()
