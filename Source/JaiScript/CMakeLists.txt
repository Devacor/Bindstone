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
option(JAISCRIPT_BUILD_EXAMPLES "Build JaiScript examples" ON)

# JaiScript Library
add_subdirectory(source)

# Tests (Foundry test suite)
if(JAISCRIPT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Examples
if(JAISCRIPT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
