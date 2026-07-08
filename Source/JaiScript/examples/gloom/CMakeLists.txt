# jai_gloom - GLOOM, a terminal 2.5D raycast shooter written in JaiScript.
# The C++ host is a shared, script-runtime-agnostic core (gloom_host) driven
# through a small adapter interface (gloom_host.hpp) so the same game can be
# ported to other embedded languages for comparison (see PORTING.md).
#
# jai_gloom ALWAYS builds with zero extra dependencies. The comparison ports
# are opt-in (OFF by default) and each requires its runtime to be checked out:
#   -DGLOOM_PORT_CHAI=ON      -> gloom_chai      (External/ChaiScript-6.1.0)
#   -DGLOOM_PORT_SQUIRREL=ON  -> gloom_squirrel  (Source/JaiScript/squirrel)
#   -DGLOOM_PORT_LUA=ON       -> gloom_lua       (Source/JaiScript/lua + sol2)
option(GLOOM_PORT_CHAI "Build gloom_chai, the ChaiScript port of GLOOM" OFF)
option(GLOOM_PORT_SQUIRREL "Build gloom_squirrel, the Squirrel port of GLOOM" OFF)
option(GLOOM_PORT_LUA "Build gloom_lua, the Lua/sol2 port of GLOOM" OFF)

function(gloom_msvc_settings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /EHsc /permissive- /bigobj)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
endfunction()

# --- shared host core: console/VT, input, timing, rng, flags, smoke/bench ---
add_library(gloom_host STATIC gloom_host.cpp)
target_include_directories(gloom_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
gloom_msvc_settings(gloom_host)
if(MSVC)
    target_link_libraries(gloom_host PUBLIC winmm)
endif()

# --- jai_gloom: the JaiScript original (always built) ---
add_executable(jai_gloom gloom_adapter_jai.cpp)
target_link_libraries(jai_gloom PRIVATE gloom_host jaiscript)
gloom_msvc_settings(jai_gloom)

# The exe prefers the source scripts dir (live-editable), falling back to
# gloom_scripts/ next to the binary.
target_compile_definitions(jai_gloom PRIVATE
    GLOOM_SOURCE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
)

add_custom_command(TARGET jai_gloom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:jai_gloom>/gloom_scripts
)

# --- gloom_chai: ChaiScript port (opt-in; header-only runtime) ---
if(GLOOM_PORT_CHAI)
    set(GLOOM_CHAISCRIPT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../External/ChaiScript-6.1.0")
    if(NOT EXISTS "${GLOOM_CHAISCRIPT_ROOT}/include/chaiscript/chaiscript.hpp")
        message(FATAL_ERROR "GLOOM_PORT_CHAI=ON but ChaiScript not found at ${GLOOM_CHAISCRIPT_ROOT}")
    endif()
    add_executable(gloom_chai gloom_adapter_chai.cpp)
    target_link_libraries(gloom_chai PRIVATE gloom_host)
    target_include_directories(gloom_chai PRIVATE "${GLOOM_CHAISCRIPT_ROOT}/include")
    target_compile_definitions(gloom_chai PRIVATE
        CHAISCRIPT_NO_THREADS=1
        GLOOM_SOURCE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ports/chai/scripts"
    )
    gloom_msvc_settings(gloom_chai)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ports/chai/scripts")  # guard until the port's scripts are committed
        add_custom_command(TARGET gloom_chai POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/ports/chai/scripts
                $<TARGET_FILE_DIR:gloom_chai>/gloom_scripts_chai
        )
    endif()
    message(STATUS "GLOOM: gloom_chai enabled (${GLOOM_CHAISCRIPT_ROOT})")
endif()

# --- gloom_squirrel: Squirrel port (opt-in; builds the runtime from source) ---
if(GLOOM_PORT_SQUIRREL)
    set(GLOOM_SQUIRREL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../squirrel")
    if(NOT EXISTS "${GLOOM_SQUIRREL_ROOT}/include/squirrel.h")
        message(FATAL_ERROR "GLOOM_PORT_SQUIRREL=ON but Squirrel not found at ${GLOOM_SQUIRREL_ROOT}")
    endif()
    file(GLOB GLOOM_SQUIRREL_SOURCES "${GLOOM_SQUIRREL_ROOT}/squirrel/*.cpp")
    file(GLOB GLOOM_SQSTDLIB_SOURCES "${GLOOM_SQUIRREL_ROOT}/sqstdlib/*.cpp")
    add_library(gloom_squirrel_runtime STATIC ${GLOOM_SQUIRREL_SOURCES} ${GLOOM_SQSTDLIB_SOURCES})
    target_include_directories(gloom_squirrel_runtime PUBLIC "${GLOOM_SQUIRREL_ROOT}/include")
    # double SQFloat (the 32-bit default sinks IEEE-754 double sim conformance);
    # PUBLIC so the adapter TU sees the same layout (it static_asserts this)
    target_compile_definitions(gloom_squirrel_runtime PUBLIC SQUSEDOUBLE)
    if(MSVC)
        target_compile_options(gloom_squirrel_runtime PRIVATE /W2)
        set_property(TARGET gloom_squirrel_runtime PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
    add_executable(gloom_squirrel gloom_adapter_squirrel.cpp)
    target_link_libraries(gloom_squirrel PRIVATE gloom_host gloom_squirrel_runtime)
    target_compile_definitions(gloom_squirrel PRIVATE
        GLOOM_SOURCE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ports/squirrel/scripts"
    )
    gloom_msvc_settings(gloom_squirrel)
    add_custom_command(TARGET gloom_squirrel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/ports/squirrel/scripts
            $<TARGET_FILE_DIR:gloom_squirrel>/gloom_scripts_squirrel
    )
    message(STATUS "GLOOM: gloom_squirrel enabled (${GLOOM_SQUIRREL_ROOT})")
endif()

# --- gloom_lua: Lua + sol2 port (opt-in; builds Lua from source) ---
if(GLOOM_PORT_LUA)
    set(GLOOM_LUA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../lua")
    set(GLOOM_SOL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sol2")
    if(NOT EXISTS "${GLOOM_LUA_ROOT}/src/lua.h" OR NOT EXISTS "${GLOOM_SOL2_ROOT}/include/sol/sol.hpp")
        message(FATAL_ERROR "GLOOM_PORT_LUA=ON but Lua/sol2 not found at ${GLOOM_LUA_ROOT} / ${GLOOM_SOL2_ROOT}")
    endif()
    enable_language(C)  # top-level project() is CXX-only; Lua is C
    # Lua core: everything except the standalone interpreter/compiler mains.
    file(GLOB GLOOM_LUA_SOURCES "${GLOOM_LUA_ROOT}/src/*.c")
    list(FILTER GLOOM_LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
    add_library(gloom_lua_runtime STATIC ${GLOOM_LUA_SOURCES})
    target_include_directories(gloom_lua_runtime PUBLIC "${GLOOM_LUA_ROOT}/src")
    if(MSVC)
        target_compile_options(gloom_lua_runtime PRIVATE /W3)
        set_property(TARGET gloom_lua_runtime PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
    add_executable(gloom_lua gloom_adapter_lua.cpp)
    target_link_libraries(gloom_lua PRIVATE gloom_host gloom_lua_runtime)
    target_include_directories(gloom_lua PRIVATE "${GLOOM_SOL2_ROOT}/include")
    target_compile_definitions(gloom_lua PRIVATE
        GLOOM_SOURCE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ports/lua/scripts"
    )
    gloom_msvc_settings(gloom_lua)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ports/lua/scripts")  # guard until the port's scripts are committed
        add_custom_command(TARGET gloom_lua POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/ports/lua/scripts
                $<TARGET_FILE_DIR:gloom_lua>/gloom_scripts_lua
        )
    endif()
    message(STATUS "GLOOM: gloom_lua enabled (${GLOOM_LUA_ROOT} + ${GLOOM_SOL2_ROOT})")
endif()
