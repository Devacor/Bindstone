# JaiScript - High-performance scripting language
cmake_minimum_required(VERSION 3.20)

project(JaiScript
    VERSION 0.1.0
    DESCRIPTION "High-performance scripting language with C++ syntax for game engines"
    LANGUAGES CXX
)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(JAISCRIPT_BUILD_TESTS "Build JaiScript test suite" ON)
option(JAISCRIPT_ENABLE_PROFILING "Enable profiling instrumentation" OFF)
option(JAISCRIPT_ENABLE_BENCHMARKS "Enable performance benchmark tests" OFF)
option(JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES "Enable debugging environment cycles" OFF)

# Optimization settings for different build configurations
if(MSVC)
    # Visual Studio multi-configuration: Use generator expressions for per-config flags
    # These work with VS's Debug/Release/RelWithDebInfo dropdown
    add_compile_options(
        $<$<CONFIG:Release>:/O2>       # Maximum optimization for speed
        $<$<CONFIG:Release>:/Ob2>      # Inline function expansion
        $<$<CONFIG:Release>:/Oi>       # Enable intrinsic functions
        $<$<CONFIG:Release>:/Ot>       # Favor fast code
        $<$<CONFIG:Release>:/GL>       # Whole program optimization
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>     # Link-time code generation
        $<$<CONFIG:Release>:/OPT:REF>  # Remove unreferenced functions
        $<$<CONFIG:Release>:/OPT:ICF>  # Identical COMDAT folding
    )
    # RelWithDebInfo: Optimizations + debugging
    add_compile_options(
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/Ob1>
        $<$<CONFIG:RelWithDebInfo>:/Zi>
    )
    message(STATUS "JaiScript: MSVC optimizations configured for Release and RelWithDebInfo")
    message(STATUS "  Select 'Release' in Visual Studio dropdown for maximum performance")
else()
    # Single-configuration generators (Ninja, Make, etc.)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native -flto)
        add_link_options(-flto)
        message(STATUS "JaiScript: Release build with GCC/Clang optimizations (-O3 -march=native -flto)")
    endif()
endif()

# JaiScript Library
add_subdirectory(source)

# Simple test executables
add_executable(test_simple_static ${CMAKE_SOURCE_DIR}/../../test_simple_static.cpp)
target_link_libraries(test_simple_static PRIVATE jaiscript)
target_include_directories(test_simple_static PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(test_simple_static PRIVATE /MT$<$<CONFIG:Debug>:d>)
endif()

add_executable(test_runner ${CMAKE_SOURCE_DIR}/../../test_runner.cpp)
target_link_libraries(test_runner PRIVATE jaiscript)
target_include_directories(test_runner PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(test_runner PRIVATE /MT$<$<CONFIG:Debug>:d>)
endif()

# Tests (Foundry test suite)
if(JAISCRIPT_BUILD_TESTS)
    add_subdirectory(source/tests)
endif()
