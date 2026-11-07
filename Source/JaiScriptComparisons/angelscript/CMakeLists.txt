# angelscript_run: the stand-alone AngelScript host for the benchmark apps and the kernel suite (opt-in; builds the
# runtime from source at COMPARISONS_ANGELSCRIPT_ROOT).
option(ANGELSCRIPT_PEERS "Build angelscript_run (AngelScript 2.38) for the benchmark apps and the kernel suite" OFF)
if(ANGELSCRIPT_PEERS)
	include(angelscript.cmake)
	add_executable(angelscript_run angelscript_run.cpp)
	target_link_libraries(angelscript_run PRIVATE angelscript_runtime)
	if(MSVC)
		target_compile_options(angelscript_run PRIVATE /W3 /EHsc)
	endif()
	message(STATUS "AngelScript: angelscript_run enabled")
endif()
