# JaiScript Library

# Collect all implementation source files
set(JAISCRIPT_SOURCES
    implementation/class_registry.cpp
    implementation/conversion_registry.cpp
    implementation/coroutine.cpp
    implementation/engine.cpp
    implementation/environment.cpp
    implementation/interpreter.cpp
    implementation/interpreter_dispatch.cpp
    implementation/lexer.cpp
    implementation/parse_errors.cpp
    implementation/parser.cpp
    implementation/runtime_errors.cpp
    implementation/type_checker.cpp
    implementation/type_info.cpp
    implementation/types.cpp
    implementation/value.cpp
    implementation/vm/disassembler.cpp
    implementation/vm/vm_backend.cpp
    implementation/vm/vm_compiler.cpp
)

# Create the library
add_library(jaiscript STATIC ${JAISCRIPT_SOURCES})

# Include directories
target_include_directories(jaiscript
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# C++20 requirement
target_compile_features(jaiscript PUBLIC cxx_std_20)

# Enable environment cycle detection (always on for debugging circular parent chains)
target_compile_definitions(jaiscript PRIVATE
    JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES
)

# Platform-specific settings
if(MSVC)
    target_compile_options(jaiscript PRIVATE
        /W3                 # Warning level 3
        /EHsc              # Exception handling model
        /permissive-       # Standards conformance
        /bigobj            # Allow more sections in object files (needed for heavy template usage)
    )

    # Profiling support for MSVC
    if(JAISCRIPT_ENABLE_PROFILING)
        target_compile_options(jaiscript PRIVATE
            /Zi             # Generate debug info for profiler
            /Oi             # Enable intrinsic functions (better profiling)
        )
        target_link_options(jaiscript PRIVATE
            /PROFILE        # Enable profiling
            /DEBUG:FULL     # Full debug info
        )
    endif()

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

    # Use static runtime for consistent linkage
    set_property(TARGET jaiscript PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
else()
    target_compile_options(jaiscript PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
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
