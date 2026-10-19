# JaiScript Library

# Collect all implementation source files
set(JAISCRIPT_SOURCES
    implementation/aggregate_lowering.cpp
    implementation/class_registry.cpp
    implementation/construction.cpp
    implementation/conversion_registry.cpp
    implementation/coroutine.cpp
    implementation/debug/connector.cpp
    implementation/debug/controller.cpp
    implementation/engine.cpp
    implementation/environment.cpp
    implementation/interpreter.cpp
    implementation/interpreter_dispatch.cpp
    implementation/lexer.cpp
    implementation/parallel_transform.cpp
    implementation/parse_errors.cpp
    implementation/parser.cpp
    implementation/runtime_errors.cpp
    implementation/schema_binding.cpp
    implementation/serialization/binary_archive.cpp
    implementation/serialization/json_archive.cpp
    implementation/serialization/polymorphic.cpp
    implementation/stdlib/containers.cpp
    implementation/stdlib/io.cpp
    implementation/stdlib/json.cpp
    implementation/stdlib/reflection.cpp
    implementation/stdlib/stdlib.cpp
    implementation/stdlib/strings.cpp
    implementation/stdlib/time.cpp
    implementation/thread_pool.cpp
    implementation/type_checker.cpp
    implementation/type_info.cpp
    implementation/value.cpp
    implementation/vm/chunk_manifest.cpp
    implementation/vm/disassembler.cpp
    implementation/vm/vm_backend.cpp
    implementation/vm/vm_compiler.cpp
    implementation/vm/vm_coverage.cpp
    implementation/vm/vm_inline.cpp
    implementation/vm/region_ir.cpp
)

# A host build may compile every implementation/**/*.cpp or the single jaiscript.cpp; configure fails when
# either disagrees with this list.
file(GLOB_RECURSE jaiscript_on_disk RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS implementation/*.cpp)
set(jaiscript_unity ${CMAKE_CURRENT_SOURCE_DIR}/jaiscript.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${jaiscript_unity})
file(STRINGS ${jaiscript_unity} jaiscript_in_unity REGEX "^#include \"implementation/.+\\.cpp\"")
list(TRANSFORM jaiscript_in_unity REPLACE "^#include \"(implementation/.+\\.cpp)\".*" "\\1")
foreach(jaiscript_list jaiscript_on_disk jaiscript_in_unity)
    set(jaiscript_extra ${${jaiscript_list}})
    set(jaiscript_missing ${JAISCRIPT_SOURCES})
    list(REMOVE_ITEM jaiscript_extra ${JAISCRIPT_SOURCES})
    list(REMOVE_ITEM jaiscript_missing ${${jaiscript_list}})
    if(jaiscript_extra OR jaiscript_missing)
        message(FATAL_ERROR "JAISCRIPT_SOURCES disagrees with ${jaiscript_list} (implementation/ on disk, jaiscript.cpp includes). Not in JAISCRIPT_SOURCES: ${jaiscript_extra}. Missing: ${jaiscript_missing}.")
    endif()
endforeach()

# Create the library
add_library(jaiscript STATIC ${JAISCRIPT_SOURCES})
add_library(JaiScript::jaiscript ALIAS jaiscript)
# Debug and Release installs share one prefix
set_target_properties(jaiscript PROPERTIES DEBUG_POSTFIX d)

# Measurement-only control. Never disable stack cookies for the whole library.
option(JAISCRIPT_EXPERIMENT_VM_NO_GS "Measure /GS- on vm_backend.cpp only (MSVC; security experiment)" OFF)
if(JAISCRIPT_EXPERIMENT_VM_NO_GS)
	if(NOT MSVC)
		message(FATAL_ERROR "JAISCRIPT_EXPERIMENT_VM_NO_GS requires MSVC")
	endif()
	set_property(SOURCE implementation/vm/vm_backend.cpp APPEND PROPERTY COMPILE_OPTIONS /GS-)
endif()

# Include directories
target_include_directories(jaiscript
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# C++20 requirement
target_compile_features(jaiscript PUBLIC cxx_std_20)

# Optional DAP-over-TCP debugger transport (jai::debug::debug_connector). On for desktop
# dev/editor builds; turn OFF for shipping/mobile so no socket code is compiled in. The
# connector's TU is empty when this is off (constructing one is then a link error by design).
option(JAISCRIPT_ENABLE_DEBUGGER "Compile the DAP debug_connector (raw-socket VS Code attach)" ON)
if(JAISCRIPT_ENABLE_DEBUGGER)
    target_compile_definitions(jaiscript PUBLIC JAISCRIPT_ENABLE_DEBUGGER)
endif()

# Platform-specific settings
if(MSVC)
    target_compile_options(jaiscript PRIVATE
        /W3                 # Warning level 3
        /EHsc              # Exception handling model
        /permissive-       # Standards conformance
        /bigobj            # Allow more sections in object files (needed for heavy template usage)
    )

    # RelWithDebInfo is best for profiling - optimized but with debug symbols
    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        target_compile_options(jaiscript PRIVATE
            /O2             # Optimize for speed
            /Ob2            # Inline function expansion
            /Oi             # Enable intrinsic functions
            /Ot             # Favor fast code
            /Zi             # Debug info
        )
    endif()

    # The binder headers overflow the default section limit in a consumer's Debug TU too
    target_compile_options(jaiscript PUBLIC /bigobj)
    # A consumer TU's inline copies of the headers (their lock guards) win COMDAT
    # selection, so every consumer needs unwinding even when CMAKE_CXX_FLAGS is overridden
    target_compile_options(jaiscript PUBLIC /EHsc)
else()
    target_compile_options(jaiscript PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(jaiscript PUBLIC -Wa,-mbig-obj)
    endif()
endif()

# Precompiled headers - parse heavy templates once instead of per-TU
target_precompile_headers(jaiscript PRIVATE
    <jaiscript/core/engine.hpp>
    <jaiscript/core/value.hpp>
    <jaiscript/core/class_definition.hpp>
)

# Set target properties
set_target_properties(jaiscript PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# The DAP connector uses Winsock. MSVC picks it up via connector.cpp's
# #pragma comment(lib) - GCC/Clang GNU drivers ignore that mechanism, so the
# dependency must live here (PUBLIC: the static lib's consumers do the final link).
if(WIN32)
    target_link_libraries(jaiscript PUBLIC ws2_32)
endif()
