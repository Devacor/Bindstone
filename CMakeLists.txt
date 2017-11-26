cmake_minimum_required(VERSION 3.4 FATAL_ERROR)
project(BindStone C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

if(${CMAKE_GENERATOR} MATCHES "Xcode")
  set(XCODE TRUE)
endif()

include(CMake/PrecompiledHeader.cmake)
include(CMake/Resources.cmake)

add_resources(TARGET BindStone SOURCES ${CMAKE_SOURCE_DIR}/Assets DEST Assets)
if(APPLE)
  add_resources(TARGET BindStone SOURCES ${CMAKE_SOURCE_DIR}/LaunchScreen DEST .)
endif()

if(${CMAKE_GENERATOR} MATCHES "^Visual Studio.*$")
  set(GENERATOR_MSVC TRUE)
endif()

set(APP_VERSION 1.0)

add_subdirectory(Source)
