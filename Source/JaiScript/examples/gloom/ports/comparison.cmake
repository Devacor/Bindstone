# Runtime sources are supplied explicitly; prepare_comparison.py fetches pinned archives.
option(GLOOM_PORT_LUA55 "Build the Lua 5.5.1 GLOOM comparison" OFF)
option(GLOOM_PORT_LUAJIT "Build the LuaJIT JIT GLOOM comparison (runtime-codegen ceiling)" OFF)
option(GLOOM_COMPARISON_PADDING_CONTROLS "Build cold-function placement controls" OFF)
set(GLOOM_SOL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sol2" CACHE PATH "sol2 source root")
set(GLOOM_LUA55_ROOT "" CACHE PATH "Lua 5.5.1 source root")
set(GLOOM_LUAJIT_ROOT "" CACHE PATH "Pinned LuaJIT 2.1 source root")
set(GLOOM_LUAJIT_LIBRARY "" CACHE FILEPATH "Built static LuaJIT JIT library")

function(gloom_lua_comparison target runtime label backend scripts)
	add_executable(${target} gloom_adapter_lua.cpp)
	target_link_libraries(${target} PRIVATE gloom_host ${runtime})
	target_include_directories(${target} PRIVATE "${GLOOM_SOL2_ROOT}/include")
	target_compile_definitions(${target} PRIVATE
		GLOOM_LUA_PROGRAM="${target}"
		GLOOM_LUA_LABEL="${label}"
		GLOOM_LUA_BACKEND="${backend}"
		GLOOM_SOURCE_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ports/${scripts}/scripts"
	)
	gloom_msvc_settings(${target})
endfunction()

if(GLOOM_PORT_LUA55)
	if(NOT EXISTS "${GLOOM_LUA55_ROOT}/src/lua.h")
		message(FATAL_ERROR "Set GLOOM_LUA55_ROOT to the Lua 5.5.1 sources from prepare_comparison.py")
	endif()
	enable_language(C)
	file(GLOB lua55_sources "${GLOOM_LUA55_ROOT}/src/*.c")
	list(FILTER lua55_sources EXCLUDE REGEX "/(lua|luac)\\.c$")
	add_library(gloom_lua55_runtime STATIC ${lua55_sources})
	target_include_directories(gloom_lua55_runtime PUBLIC "${GLOOM_LUA55_ROOT}/src")
	if(MSVC)
		set_property(TARGET gloom_lua55_runtime PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		target_compile_options(gloom_lua55_runtime PRIVATE /W3)
	elseif(UNIX)
		target_link_libraries(gloom_lua55_runtime PUBLIC m ${CMAKE_DL_LIBS})
	endif()
	gloom_lua_comparison(gloom_lua55 gloom_lua55_runtime "Lua 5.5.1 / sol2" lua55 lua)
endif()

if(GLOOM_PORT_LUAJIT)
	if(NOT EXISTS "${GLOOM_LUAJIT_ROOT}/src/luajit.h" OR NOT EXISTS "${GLOOM_LUAJIT_LIBRARY}")
		message(FATAL_ERROR "Set GLOOM_LUAJIT_ROOT and GLOOM_LUAJIT_LIBRARY to the pinned LuaJIT JIT build")
	endif()
	add_library(gloom_luajit_runtime STATIC IMPORTED)
	set_target_properties(gloom_luajit_runtime PROPERTIES
		IMPORTED_LOCATION "${GLOOM_LUAJIT_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${GLOOM_LUAJIT_ROOT}/src"
	)
	gloom_lua_comparison(gloom_luajit gloom_luajit_runtime "LuaJIT 2.1 JIT / sol2" luajit-jit luajit)
	target_compile_definitions(gloom_luajit PRIVATE
		GLOOM_LUAJIT=1
		GLOOM_LUA_COMMON_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ports/lua/scripts"
	)
endif()

if(GLOOM_COMPARISON_PADDING_CONTROLS)
	add_library(gloom_host_padding STATIC gloom_host.cpp)
	target_include_directories(gloom_host_padding PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_definitions(gloom_host_padding PRIVATE GLOOM_LAYOUT_PADDING=1)
	gloom_msvc_settings(gloom_host_padding)
	if(MSVC)
		target_link_libraries(gloom_host_padding PUBLIC winmm)
	endif()
	foreach(original IN ITEMS jai_gloom gloom_squirrel gloom_lua gloom_lua55 gloom_luajit)
		if(TARGET ${original})
			get_target_property(sources ${original} SOURCES)
			get_target_property(libraries ${original} LINK_LIBRARIES)
			list(TRANSFORM libraries REPLACE "^gloom_host$" "gloom_host_padding")
			add_executable(${original}_padding ${sources})
			target_link_libraries(${original}_padding PRIVATE ${libraries})
			foreach(property IN ITEMS INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS MSVC_RUNTIME_LIBRARY)
				get_target_property(value ${original} ${property})
				if(value)
					set_property(TARGET ${original}_padding PROPERTY ${property} "${value}")
				endif()
			endforeach()
		endif()
	endforeach()
endif()
