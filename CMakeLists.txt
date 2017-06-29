cmake_minimum_required(VERSION 3.4 FATAL_ERROR)
project(MoonDog C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

include(CMake/PrecompiledHeader.cmake)

if(${CMAKE_GENERATOR} MATCHES "^Visual Studio.*$")
  set(GENERATOR_MSVC TRUE)
endif()

add_subdirectory(Source)
