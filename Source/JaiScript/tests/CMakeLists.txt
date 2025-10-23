# JaiScript Foundry Test Suite

# Collect all test suite files
file(GLOB_RECURSE TEST_SOURCES
    containers/*.cpp
    containers/*.hpp
    integration/*.cpp
    integration/*.hpp
    language/*.cpp
    language/*.hpp
    # performance/*.cpp  # Temporarily disabled
    # performance/*.hpp
    semantics/*.cpp
    semantics/*.hpp
    # vm/*.cpp  # VM backend not yet implemented - excluded for now
    # vm/*.hpp
)

# Create test executable
add_executable(jaiscript_tests
    main_test_runner.cpp
    ${TEST_SOURCES}
)

# Link against JaiScript library
target_link_libraries(jaiscript_tests PRIVATE jaiscript)

# Include test directory for relative includes
target_include_directories(jaiscript_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Platform-specific settings
if(MSVC)
    target_compile_options(jaiscript_tests PRIVATE
        /W3                 # Warning level 3
        /EHsc              # Exception handling model
        /permissive-       # Standards conformance
        /bigobj            # Allow large object files
    )
    # Use static runtime to match library
    set_property(TARGET jaiscript_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
else()
    target_compile_options(jaiscript_tests PRIVATE
        -Wall
        -Wextra
    )
endif()

# Add test to CTest
enable_testing()
add_test(NAME FoundryTests COMMAND jaiscript_tests)

# Optional: Add filtered test targets
add_test(NAME LanguageTests COMMAND jaiscript_tests "Language")
add_test(NAME SemanticsTests COMMAND jaiscript_tests "semantics")
add_test(NAME ContainerTests COMMAND jaiscript_tests "Container")
add_test(NAME StaticTests COMMAND jaiscript_tests "Static")

# Create a separate executable target for static method tests only
# This appears in VS dropdown for easy debugging
add_executable(static_method_tests
    main_test_runner.cpp
    language/static_method_tests.cpp
    language/static_field_tests.cpp
)

target_link_libraries(static_method_tests PRIVATE jaiscript)
target_include_directories(static_method_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(static_method_tests PRIVATE /W3 /EHsc /permissive- /bigobj)
    set_property(TARGET static_method_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# Enable profiling support for test executables when needed
if(MSVC AND (JAISCRIPT_ENABLE_PROFILING OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
    target_compile_options(jaiscript_tests PRIVATE /Zi /Oi)
    target_link_options(jaiscript_tests PRIVATE /PROFILE /DEBUG:FULL)
endif()
