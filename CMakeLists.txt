cmake_minimum_required(VERSION 3.4 FATAL_ERROR)
project(MoonDog C CXX)

set(CMAKE_CXX_STANDARD 14)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

include(CMake/cotire.cmake)

add_subdirectory(Source)
