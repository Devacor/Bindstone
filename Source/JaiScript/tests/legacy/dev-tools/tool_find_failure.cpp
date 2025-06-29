#include <iostream>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include "../Tests/test_parser.cpp"

// Override the test runner to find which test fails
int main() {
    // Run all tests manually to find the failure
    test_basic_expressions();
    test_binary_expressions();
    test_ternary_expressions();
    test_assignment_expressions();
    test_member_access();
    test_function_calls();
    test_array_access();
    test_lambda_expressions();
    test_new_expressions();
    test_basic_statements();
    test_control_flow();
    test_range_based_for_loops();
    test_function_declarations();
    test_variable_declarations();
    test_class_declarations();
    test_return_statements();
    test_break_continue_statements();
    test_complex_programs();
    test_parser_error_handling();
    test_parser_edge_cases();
    test_type_inference();
    test_trailing_return_types();
    test_const_reference_parameters();
    test_brace_initialization();
    
    std::cout << "\nAll tests completed!\n";
    return 0;
}