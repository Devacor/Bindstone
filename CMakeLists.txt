cmake_minimum_required(VERSION 3.4 FATAL_ERROR)
project(BindStone C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

include(CMake/PrecompiledHeader.cmake)
include(CMake/Resources.cmake)

add_resources(TARGET MoonDog SOURCES ${CMAKE_SOURCE_DIR}/Assets DEST .)

if(${CMAKE_GENERATOR} MATCHES "^Visual Studio.*$")
  set(GENERATOR_MSVC TRUE)
endif()

set(APP_VERSION 1.0)

add_subdirectory(Source)
