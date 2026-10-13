# JaiScriptComparisons - JaiScript against Lua, LuaJIT, Squirrel and ChaiScript: the GLOOM, demoreel and JaiDoom
# ports, the Gator Quest Lua twin, the interpreter benchmark suites and the kernel suite's Lua side. JaiScript builds
# as a subproject; nothing in it refers back here.
cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_GENERATOR MATCHES "^(Visual Studio|Xcode)|Multi-Config$")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build.")
endif()

project(JaiScriptComparisons LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JAISCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JaiScript" CACHE PATH "The JaiScript source root")
set(COMPARISONS_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "Holds the lua/, sol2/ and squirrel/ source checkouts")
set(COMPARISONS_CHAISCRIPT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../External/ChaiScript-6.1.0" CACHE PATH "ChaiScript 6.1.0 (the folder holding include/)")

# The layout the measurements were taken with: the static runtime, whole-program Release, every program in bin/
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${PROJECT_BINARY_DIR}/bin)
endforeach()

set(JAISCRIPT_BUILD_EXAMPLES ON)   # the ports link the games' shared hosts
set(JAISCRIPT_BUILD_RUNNER ON)
set(JAISCRIPT_BUILD_TESTS OFF)
set(JAISCRIPT_BUILD_LSP OFF)
set(JAISCRIPT_INSTALL OFF)
set(JAISCRIPT_LTCG ON)
add_subdirectory("${JAISCRIPT_DIR}" jaiscript)

add_subdirectory(benchmarks)
add_subdirectory(gloom)
add_subdirectory(demoreel)

option(JAIDOOM_PORT_LUA "Build the headless Lua JaiDoom comparison" OFF)
if(JAIDOOM_PORT_LUA)
    add_subdirectory(jaidoom/ports jaidoom_ports)
endif()
