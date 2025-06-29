# All test files in the TestSuite
set(TEST_FILES
    # Core component tests
    test_lexer
    test_parser
    test_value
    test_engine
    
    # C++ Integration tests
    test_class_builder
    test_class_builder_comprehensive
    test_class_builder_minimal
    test_class_instantiation
    test_class_methods
    test_cpp_bindings
    
    # Operator tests
    test_operators
    test_extended_operators
    test_operator_overloading
    test_operator_overloading_debug
    test_operator_debug
    test_simple_op
    test_simple_operator
    test_spaceship_custom_types
    
    # Language feature tests
    test_functions
    test_lambdas
    test_lambda_captures
    test_lambda_debug
    test_control_flow
    test_range_for
    
    # Type system tests
    test_type_conversions
    test_type_safety
    test_float_conversion
    test_reference_types
    
    # Utility tests
    test_extraction_convenience
    test_parameter_convenience
    test_variable_query
    test_return_values
    
    # Memory and performance tests
    test_memory_leak_detection
    test_memory_leak_specific
    test_memory_stress
    test_memory_stress_simple
    test_performance
    test_jaiscript_vs_chaiscript
    
    # Debug and development tests
    test_custom_type_debug
    test_custom_type_return
    test_inheritance_debug
    test_issue_diagnosis
    test_overload_debug
    test_segfault_debug
    test_vector2_debug
)

# Add each test
foreach(TEST_FILE ${TEST_FILES})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_FILE}.cpp)
        add_jai_test(${TEST_FILE})
    endif()
endforeach()