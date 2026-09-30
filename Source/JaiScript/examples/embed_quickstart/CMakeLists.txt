cmake_minimum_required(VERSION 3.20)
project(embed_quickstart LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default: build JaiScript from source as a subproject. ON: use an installed package
# (cmake --install from a JaiScript build, then -DCMAKE_PREFIX_PATH=<prefix>).
option(EMBED_QUICKSTART_FIND_PACKAGE "Use an installed JaiScript through find_package" OFF)

if(EMBED_QUICKSTART_FIND_PACKAGE)
	find_package(JaiScript 0.1 CONFIG REQUIRED)
else()
	add_subdirectory(../.. jaiscript)
endif()

add_executable(embed_quickstart main.cpp)
target_link_libraries(embed_quickstart PRIVATE JaiScript::jaiscript)

if(MSVC)
	# The library selects the static runtime and its headers need /bigobj in every consumer TU.
	target_compile_options(embed_quickstart PRIVATE /bigobj)
	set_property(TARGET embed_quickstart PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_compile_definitions(embed_quickstart PRIVATE
	EMBED_QUICKSTART_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/scripts")
