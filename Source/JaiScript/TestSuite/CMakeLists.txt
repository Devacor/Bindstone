cmake_minimum_required(VERSION 3.20)
project(JaiScriptTestSuite)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directory for test executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# JaiScript implementation sources
set(JAISCRIPT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/implementation)
set(JAISCRIPT_SOURCES
    ${JAISCRIPT_SRC_DIR}/lexer.cpp
    ${JAISCRIPT_SRC_DIR}/parser.cpp
    ${JAISCRIPT_SRC_DIR}/type_info.cpp
    ${JAISCRIPT_SRC_DIR}/value.cpp
    ${JAISCRIPT_SRC_DIR}/interpreter.cpp
    ${JAISCRIPT_SRC_DIR}/engine.cpp
)

# Create a static library for JaiScript implementation
add_library(jaiscript_impl STATIC ${JAISCRIPT_SOURCES})
target_compile_features(jaiscript_impl PUBLIC cxx_std_20)

# Function to add a test executable
function(add_jai_test TEST_NAME)
    add_executable(${TEST_NAME} Tests/${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE jaiscript_impl)
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_20)
    
    # Add to CTest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 30)
endfunction()

# Enable testing
enable_testing()

# Add test subdirectory
add_subdirectory(Tests)

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all tests..."
)

# Custom target to run tests with verbose output
add_custom_target(run_tests_verbose
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose --output-on-failure -C $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all tests with verbose output..."
)

# Custom target to run benchmarks
add_custom_target(run_benchmarks
    COMMAND ${CMAKE_CTEST_COMMAND} --tests-regex ".*benchmark.*" --verbose -C $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running benchmarks..."
)