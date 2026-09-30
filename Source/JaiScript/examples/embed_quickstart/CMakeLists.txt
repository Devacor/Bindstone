cmake_minimum_required(VERSION 3.20)
project(embed_quickstart LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JAISCRIPT_BUILD_TESTS    OFF CACHE BOOL "")
set(JAISCRIPT_BUILD_EXAMPLES OFF CACHE BOOL "")
set(JAISCRIPT_BUILD_RUNNER   OFF CACHE BOOL "")
set(JAISCRIPT_BUILD_LSP      OFF CACHE BOOL "")
add_subdirectory(../.. jaiscript)

# The library's own include path is spelled against CMAKE_SOURCE_DIR, so as a subproject even
# the library cannot find its headers; give the target the real path.
target_include_directories(jaiscript PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_executable(embed_quickstart main.cpp)
target_link_libraries(embed_quickstart PRIVATE jaiscript)

if(MSVC)
	# The library selects the static runtime and its headers need /bigobj in every consumer TU.
	target_compile_options(embed_quickstart PRIVATE /bigobj /permissive-)
	set_property(TARGET embed_quickstart PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_compile_definitions(embed_quickstart PRIVATE
	EMBED_QUICKSTART_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/scripts")
