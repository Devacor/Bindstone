cmake_minimum_required(VERSION 3.10)
project(JaiScriptTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(../include)

# Source files
set(SOURCES
    ../src/jaiscript.cpp
    test_lexer.cpp
    test_parser.cpp
    test_main.cpp
)

# Create executable
add_executable(jaiscript_test ${SOURCES})

# Add debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(jaiscript_test PRIVATE -g -O0)
endif()