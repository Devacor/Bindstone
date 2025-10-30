# JaiScript Foundry Test Suite

# Collect all test suite files
set(TEST_SOURCES_GLOB
    containers/*.cpp
    containers/*.hpp
    foundry/*.cpp
    foundry/*.hpp
    integration/*.cpp
    integration/*.hpp
    language/*.cpp
    language/*.hpp
    semantics/*.cpp
    semantics/*.hpp
    # vm/*.cpp  # VM backend not yet implemented - excluded for now
    # vm/*.hpp
)

# Add performance benchmarks if enabled
if(JAISCRIPT_ENABLE_BENCHMARKS)
    list(APPEND TEST_SOURCES_GLOB
        performance/*.cpp
        performance/*.hpp
    )
    message(STATUS "JaiScript: Performance benchmarks ENABLED (may increase test runtime)")

    # Check if ChaiScript is available for comparison benchmarks
    set(CHAISCRIPT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../External/ChaiScript-6.1.0")
    if(EXISTS "${CHAISCRIPT_ROOT}/include/chaiscript/chaiscript.hpp")
        message(STATUS "JaiScript: ChaiScript found - enabling comparison benchmarks")
        set(HAVE_CHAISCRIPT ON)
    else()
        message(STATUS "JaiScript: ChaiScript not found at ${CHAISCRIPT_ROOT}")
        set(HAVE_CHAISCRIPT OFF)
    endif()
else()
    message(STATUS "JaiScript: Performance benchmarks DISABLED (use -DJAISCRIPT_ENABLE_BENCHMARKS=ON to enable)")
endif()

file(GLOB_RECURSE TEST_SOURCES ${TEST_SOURCES_GLOB})

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

# Add ChaiScript support if available
if(HAVE_CHAISCRIPT)
    target_include_directories(jaiscript_tests PRIVATE
        "${CHAISCRIPT_ROOT}/include"
    )
    target_compile_definitions(jaiscript_tests PRIVATE
        HAVE_CHAISCRIPT=1
        CHAISCRIPT_NO_THREADS=1  # ChaiScript thread support not needed for benchmarks
    )
    message(STATUS "JaiScript: ChaiScript include path: ${CHAISCRIPT_ROOT}/include")
endif()

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
add_test(NAME NamespaceTests COMMAND jaiscript_tests "Namespace")

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

# Create a test executable for string static methods proof-of-concept
add_executable(test_string_static_method
    test_string_static_method.cpp
)

target_link_libraries(test_string_static_method PRIVATE jaiscript)
target_include_directories(test_string_static_method PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(test_string_static_method PRIVATE /W3 /EHsc /permissive- /bigobj)
    set_property(TARGET test_string_static_method PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

# Enable profiling support for test executables when needed
if(MSVC AND (JAISCRIPT_ENABLE_PROFILING OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
    target_compile_options(jaiscript_tests PRIVATE /Zi /Oi)
    target_link_options(jaiscript_tests PRIVATE /PROFILE /DEBUG:FULL)
endif()
