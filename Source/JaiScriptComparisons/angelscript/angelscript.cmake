# AngelScript 2.38 as a static library with the add-ons the comparison scripts use. MSVC x64 gets the native calling
# convention, which needs the MASM thunk (as_callfunc_x64_msvc_asm.asm); every other as_callfunc_*.cpp compiles to
# nothing off its own platform, so the source glob stays whole.
if(NOT TARGET angelscript_runtime)
	set(AS_ROOT "${COMPARISONS_ANGELSCRIPT_ROOT}")
	if(NOT EXISTS "${AS_ROOT}/angelscript/include/angelscript.h")
		message(FATAL_ERROR "AngelScript requested but not found at ${AS_ROOT} (the SDK folder holding angelscript/include/angelscript.h)")
	endif()
	file(GLOB angelscript_sources "${AS_ROOT}/angelscript/source/*.cpp")
	set(angelscript_add_ons
		"${AS_ROOT}/add_on/scriptstdstring/scriptstdstring.cpp"
		"${AS_ROOT}/add_on/scriptstdstring/scriptstdstring_utils.cpp"
		"${AS_ROOT}/add_on/scriptarray/scriptarray.cpp"
		"${AS_ROOT}/add_on/scriptmath/scriptmath.cpp"
		"${AS_ROOT}/add_on/scriptdictionary/scriptdictionary.cpp"
		"${AS_ROOT}/add_on/scriptbuilder/scriptbuilder.cpp"
		"${AS_ROOT}/add_on/scripthelper/scripthelper.cpp"
	)
	if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
		enable_language(ASM_MASM)
		list(APPEND angelscript_sources "${AS_ROOT}/angelscript/source/as_callfunc_x64_msvc_asm.asm")
	endif()
	add_library(angelscript_runtime STATIC ${angelscript_sources} ${angelscript_add_ons})
	target_include_directories(angelscript_runtime PUBLIC
		"${AS_ROOT}/angelscript/include"
		"${AS_ROOT}/add_on"
	)
	# scriptmath registers float-only math unless AS_USE_FLOAT is 0; the ports compute in double like their peers
	target_compile_definitions(angelscript_runtime PRIVATE ANGELSCRIPT_EXPORT _LIB AS_USE_FLOAT=0)
	if(MSVC)
		target_compile_definitions(angelscript_runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
		target_compile_options(angelscript_runtime PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W1 /EHsc>)
	endif()
	find_package(Threads REQUIRED)
	target_link_libraries(angelscript_runtime PUBLIC Threads::Threads)
	file(STRINGS "${AS_ROOT}/angelscript/include/angelscript.h" angelscript_version REGEX "#define ANGELSCRIPT_VERSION_STRING")
	message(STATUS "AngelScript: ${angelscript_version} at ${AS_ROOT}")
endif()
