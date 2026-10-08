# JaiScript Foundry Test Suite

# Collect all test suite files
set(TEST_SOURCES_GLOB
    containers/*.cpp
    containers/*.hpp
    core/*.cpp
    core/*.hpp
    debug/*.cpp
    debug/*.hpp
    foundry/*.cpp
    foundry/*.hpp
    integration/*.cpp
    integration/*.hpp
    language/*.cpp
    language/*.hpp
    properties/*.cpp
    properties/*.hpp
    semantics/*.cpp
    semantics/*.hpp
    serialization/*.cpp
    serialization/*.hpp
    signals/*.cpp
    signals/*.hpp
    stdlib/*.cpp
    stdlib/*.hpp
    vm/*.cpp
    vm/*.hpp
)

# Add performance benchmarks if enabled
if(JAISCRIPT_ENABLE_BENCHMARKS)
    list(APPEND TEST_SOURCES_GLOB
        performance/*.cpp
        performance/*.hpp
    )
    message(STATUS "JaiScript: Performance benchmarks ENABLED (may increase test runtime)")

    # Check if ChaiScript is available for comparison benchmarks
    set(CHAISCRIPT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../External/ChaiScript-6.1.0")
    if(EXISTS "${CHAISCRIPT_ROOT}/include/chaiscript/chaiscript.hpp")
        message(STATUS "JaiScript: ChaiScript found - enabling comparison benchmarks")
        set(HAVE_CHAISCRIPT ON)
    else()
        message(STATUS "JaiScript: ChaiScript not found at ${CHAISCRIPT_ROOT}")
        set(HAVE_CHAISCRIPT OFF)
    endif()

    # Check if Squirrel is available for comparison benchmarks
    set(SQUIRREL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../squirrel")
    if(EXISTS "${SQUIRREL_ROOT}/include/squirrel.h")
        message(STATUS "JaiScript: Squirrel found - enabling comparison benchmarks")
        set(HAVE_SQUIRREL ON)
    else()
        message(STATUS "JaiScript: Squirrel not found at ${SQUIRREL_ROOT}")
        set(HAVE_SQUIRREL OFF)
    endif()

    # Check if Lua + sol2 are available for comparison benchmarks
    set(LUA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../lua")
    set(SOL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../sol2")
    if(EXISTS "${LUA_ROOT}/src/lua.h" AND EXISTS "${SOL2_ROOT}/include/sol/sol.hpp")
        message(STATUS "JaiScript: Lua + sol2 found - enabling comparison benchmarks")
        set(HAVE_SOL2 ON)
    else()
        message(STATUS "JaiScript: Lua/sol2 not found at ${LUA_ROOT} / ${SOL2_ROOT}")
        set(HAVE_SOL2 OFF)
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

# JAISCRIPT_TESTS_SOURCE_ROOT: the JaiScript folder, so corpus tests find examples/ from any working directory
get_filename_component(JAISCRIPT_TESTS_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
target_compile_definitions(jaiscript_tests PRIVATE
    JAISCRIPT_TESTS_SOURCE_ROOT="${JAISCRIPT_TESTS_SOURCE_ROOT}"
)

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

# Add Squirrel support if available
if(HAVE_SQUIRREL)
    # Build Squirrel library
    set(SQUIRREL_SRC_DIR "${SQUIRREL_ROOT}/squirrel")
    set(SQSTDLIB_SRC_DIR "${SQUIRREL_ROOT}/sqstdlib")

    # Collect Squirrel source files
    file(GLOB SQUIRREL_SOURCES "${SQUIRREL_SRC_DIR}/*.cpp")
    file(GLOB SQSTDLIB_SOURCES "${SQSTDLIB_SRC_DIR}/*.cpp")

    # Create Squirrel static library
    add_library(squirrel_lib STATIC ${SQUIRREL_SOURCES} ${SQSTDLIB_SOURCES})
    target_include_directories(squirrel_lib PUBLIC
        "${SQUIRREL_ROOT}/include"
    )
    if(MSVC)
        target_compile_options(squirrel_lib PRIVATE /W2)
        set_property(TARGET squirrel_lib PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()

    # Link Squirrel to tests
    target_include_directories(jaiscript_tests PRIVATE
        "${SQUIRREL_ROOT}/include"
    )
    target_link_libraries(jaiscript_tests PRIVATE squirrel_lib)
    target_compile_definitions(jaiscript_tests PRIVATE
        HAVE_SQUIRREL=1
    )
    message(STATUS "JaiScript: Squirrel include path: ${SQUIRREL_ROOT}/include")
endif()

# Add Lua + sol2 support if available
if(HAVE_SOL2)
    enable_language(C)  # top-level project() is CXX-only; Lua is C

    # Lua core: everything except the standalone interpreter/compiler mains.
    # .c files compile as C by default with MSVC - do NOT add /TP.
    file(GLOB LUA_SOURCES "${LUA_ROOT}/src/*.c")
    list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")

    add_library(lua_lib STATIC ${LUA_SOURCES})
    target_include_directories(lua_lib PUBLIC
        "${LUA_ROOT}/src"
    )
    if(MSVC)
        target_compile_options(lua_lib PRIVATE /W3)
        set_property(TARGET lua_lib PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()

    # Link Lua + sol2 to tests
    target_include_directories(jaiscript_tests PRIVATE
        "${SOL2_ROOT}/include"
    )
    target_link_libraries(jaiscript_tests PRIVATE lua_lib)
    target_compile_definitions(jaiscript_tests PRIVATE
        HAVE_SOL2=1
    )
    message(STATUS "JaiScript: sol2 include path: ${SOL2_ROOT}/include")
endif()

# Platform-specific settings
if(MSVC)
    target_compile_options(jaiscript_tests PRIVATE /W3)
    # Deep recursion exceeds the 1 MB default stack in Debug; a larger stack lets the
    # catchable depth-limit error actually fire.
    target_link_options(jaiscript_tests PRIVATE /STACK:134217728)
    # The Debug PDB passes MSVC's 4 GB limit at the default 4 KB page size (LNK1140); 8 KB pages raise it to 8 GB
    target_link_options(jaiscript_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/PDBPAGESIZE:8192>)
else()
    target_compile_options(jaiscript_tests PRIVATE
        -Wall
        -Wextra
    )
    if(WIN32)
        # Same 128 MB stack the MSVC branch sets: deep recursion
        # and the parser depth budget exceed the 1 MB default under Clang/GCC Debug
        # frame sizes too (LINKER: translates per driver - lld-link takes /STACK,
        # MinGW ld takes --stack)
        if(MINGW)
            target_link_options(jaiscript_tests PRIVATE "LINKER:--stack,134217728")
        else()
            target_link_options(jaiscript_tests PRIVATE "LINKER:/STACK:134217728")
        endif()
    endif()
endif()

# Precompiled headers - most test files include these three
target_precompile_headers(jaiscript_tests PRIVATE
    <jaiscript/testing/foundry.hpp>
    <jaiscript/core/engine.hpp>
    <jaiscript/core/dynamic_binder.hpp>
)

# Add test to CTest
enable_testing()
add_test(NAME FoundryTests COMMAND jaiscript_tests)

# Optional: Add filtered test targets
add_test(NAME LanguageTests COMMAND jaiscript_tests "Language")
add_test(NAME SemanticsTests COMMAND jaiscript_tests "semantics")
add_test(NAME ContainerTests COMMAND jaiscript_tests "Container")
add_test(NAME StaticTests COMMAND jaiscript_tests "Static")
add_test(NAME NamespaceTests COMMAND jaiscript_tests "Namespace")
add_test(NAME SerializationTests COMMAND jaiscript_tests "Serialization")
add_test(NAME StrongTypesTests COMMAND jaiscript_tests "Strong Types")
add_test(NAME TypeSafetyTests COMMAND jaiscript_tests "Type Safety")
add_test(NAME StdlibTests COMMAND jaiscript_tests "Stdlib")
add_test(NAME SignalTests COMMAND jaiscript_tests "Signal")
add_test(NAME ObservableTests COMMAND jaiscript_tests "Observable")
add_test(NAME TemplateBinder COMMAND jaiscript_tests "Template Binder")

# RelWithDebInfo test executables are profiler-friendly
if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_options(jaiscript_tests PRIVATE /Zi /Oi)
    target_link_options(jaiscript_tests PRIVATE /PROFILE /DEBUG:FULL)
endif()

# When benchmarks are enabled, also enable verbose output
if(JAISCRIPT_ENABLE_BENCHMARKS)
    target_compile_definitions(jaiscript_tests PRIVATE FOUNDRY_VERBOSE=1)
    # Add verbose benchmark test target
    add_test(NAME BenchmarkTests COMMAND jaiscript_tests "-verbose")
endif()

# ============================================
# BEGIN differential fuzzer (fuzz/) block
# ============================================
# jaiscript_fuzz: standalone differential fuzzer exe (interpreter vs VM oracle).
# The seeded smoke suite joins the normal test exe via target_sources (fuzz/ is
# deliberately NOT in the test glob so fuzz_main.cpp stays out of jaiscript_tests).
target_sources(jaiscript_tests PRIVATE
    fuzz/fuzz_smoke_tests.cpp
)

add_executable(jaiscript_fuzz
    fuzz/fuzz_main.cpp
)
target_link_libraries(jaiscript_fuzz PRIVATE jaiscript)
target_include_directories(jaiscript_fuzz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(jaiscript_fuzz PRIVATE /W3)
    # Same deep-recursion headroom as jaiscript_tests (catchable depth-limit errors).
    target_link_options(jaiscript_fuzz PRIVATE /STACK:134217728)
else()
    target_compile_options(jaiscript_fuzz PRIVATE -Wall -Wextra)
endif()
# ============================================
# END differential fuzzer (fuzz/) block
# ============================================
