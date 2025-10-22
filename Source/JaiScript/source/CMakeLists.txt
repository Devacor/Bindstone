# JaiScript Library

# Collect all implementation source files
set(JAISCRIPT_SOURCES
    implementation/class_registry.cpp
    implementation/conversion_registry.cpp
    implementation/engine.cpp
    implementation/interpreter.cpp
    implementation/interpreter_dispatch.cpp
    implementation/lexer.cpp
    implementation/parser.cpp
    implementation/type_info.cpp
    implementation/types.cpp
    implementation/value.cpp
    implementation/vm_backend_stub.cpp
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

# Platform-specific settings
if(MSVC)
    target_compile_options(jaiscript PRIVATE
        /W3                 # Warning level 3
        /EHsc              # Exception handling model
        /permissive-       # Standards conformance
    )
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

# Set target properties
set_target_properties(jaiscript PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
