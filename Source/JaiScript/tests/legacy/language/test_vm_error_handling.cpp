#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/bytecode.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

// Helper function to test if VM throws expected exception
template<typename ExceptionType>
bool throws_exception(const std::string& source) {
    try {
        lexer lex(source);
        auto tokens = lex.tokenize();
        
        parser parse(tokens);
        auto declarations = parse.parse();
        
        auto backend = create_vm_backend();
        backend->execute(declarations);
        return false;
    } catch (const ExceptionType&) {
        return true;
    } catch (...) {
        return false;
    }
}

// Helper to get exception message
std::string get_exception_message(const std::string& source) {
    try {
        lexer lex(source);
        auto tokens = lex.tokenize();
        
        parser parse(tokens);
        auto declarations = parse.parse();
        
        auto backend = create_vm_backend();
        backend->execute(declarations);
        return "";
    } catch (const std::exception& e) {
        return e.what();
    }
}

JAI_TEST_SUITE(VMErrorHandling)

// Compilation Errors
JAI_TEST(vm_compilation_syntax_errors) {
    // Missing semicolon
    expect_true(throws_exception<parse_error>("var x = 5"));
    
    // Invalid syntax
    expect_true(throws_exception<parse_error>("var = 5;"));
    expect_true(throws_exception<parse_error>("5 5;"));
    expect_true(throws_exception<parse_error>("if (true) else {}"));
    
    // Unmatched brackets
    expect_true(throws_exception<parse_error>("var arr = [1, 2, 3"));
    expect_true(throws_exception<parse_error>("var map = {\"key\": value"));
    expect_true(throws_exception<parse_error>("fun test() { return 5;"));
    
    // Invalid operators
    expect_true(throws_exception<parse_error>("5 ** 2;")); // Power operator not implemented
    expect_true(throws_exception<parse_error>("5 %% 2;")); // Invalid operator
}

// Runtime Type Errors
JAI_TEST(vm_runtime_type_errors) {
    // Invalid operations on incompatible types
    expect_true(throws_exception<runtime_error>(R"(
        "string" - 5;
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        true * false;
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        var arr = [1, 2, 3];
        arr / 2;
    )"));
    
    // Invalid method calls
    expect_true(throws_exception<runtime_error>(R"(
        var num = 42;
        num.push(5);
    )"));
    
    // Type conversion errors
    expect_true(throws_exception<runtime_error>(R"(
        var str = "not a number";
        var num = str + 0; // Assuming this should fail for non-numeric strings
    )"));
}

// Variable Access Errors
JAI_TEST(vm_undefined_variable_errors) {
    // Undefined variable access
    expect_true(throws_exception<runtime_error>(R"(
        undefined_var;
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        var x = y; // y is undefined
    )"));
    
    // Variable used before declaration in same scope
    expect_true(throws_exception<runtime_error>(R"(
        x = 5;
        var x;
    )"));
    
    // Out of scope variable
    expect_true(throws_exception<runtime_error>(R"(
        {
            var local = 10;
        }
        local; // Out of scope
    )"));
}

// Array Access Errors
JAI_TEST(vm_array_bounds_errors) {
    // Array index out of bounds
    expect_true(throws_exception<runtime_error>(R"(
        var arr = [1, 2, 3];
        arr[5];
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        var arr = [1, 2, 3];
        arr[-1];
    )"));
    
    // Invalid index type
    expect_true(throws_exception<runtime_error>(R"(
        var arr = [1, 2, 3];
        arr["string"];
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        var arr = [1, 2, 3];
        arr[1.5];
    )"));
}

// Function Call Errors
JAI_TEST(vm_function_call_errors) {
    // Wrong number of arguments
    expect_true(throws_exception<runtime_error>(R"(
        fun add(a, b) {
            return a + b;
        }
        add(1); // Missing argument
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        fun add(a, b) {
            return a + b;
        }
        add(1, 2, 3); // Too many arguments
    )"));
    
    // Calling non-function
    expect_true(throws_exception<runtime_error>(R"(
        var x = 5;
        x();
    )"));
    
    // Undefined function
    expect_true(throws_exception<runtime_error>(R"(
        undefined_function();
    )"));
}

// Stack Overflow
JAI_TEST(vm_stack_overflow) {
    // Infinite recursion
    expect_true(throws_exception<runtime_error>(R"(
        fun infinite() {
            return infinite();
        }
        infinite();
    )"));
    
    // Deep recursion
    expect_true(throws_exception<runtime_error>(R"(
        fun deep(n) {
            if (n <= 0) return 0;
            return 1 + deep(n - 1);
        }
        deep(10000); // Should exceed stack limit
    )"));
    
    // Mutual recursion
    expect_true(throws_exception<runtime_error>(R"(
        fun foo() { return bar(); }
        fun bar() { return foo(); }
        foo();
    )"));
}

// Division by Zero
JAI_TEST(vm_division_by_zero) {
    // Integer division by zero
    expect_true(throws_exception<runtime_error>(R"(
        5 / 0;
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        var x = 10;
        var y = 0;
        x / y;
    )"));
    
    // Modulo by zero
    expect_true(throws_exception<runtime_error>(R"(
        10 % 0;
    )"));
    
    // Float division by zero might return infinity instead of throwing
    // This behavior should be documented and tested accordingly
}

// Null/Undefined Operations
JAI_TEST(vm_null_operations) {
    // Operations on null
    expect_true(throws_exception<runtime_error>(R"(
        var x = null;
        x.method();
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        var x = null;
        x[0];
    )"));
    
    expect_true(throws_exception<runtime_error>(R"(
        var x = null;
        x + 5;
    )"));
}

// Error Messages Quality
JAI_TEST(vm_error_message_quality) {
    // Check that error messages are helpful
    auto msg = get_exception_message("undefined_var;");
    expect_true(msg.find("undefined") != std::string::npos || 
                msg.find("not defined") != std::string::npos);
    
    msg = get_exception_message(R"(
        var arr = [1, 2, 3];
        arr[10];
    )");
    expect_true(msg.find("index") != std::string::npos || 
                msg.find("bounds") != std::string::npos);
    
    msg = get_exception_message("5 / 0;");
    expect_true(msg.find("zero") != std::string::npos || 
                msg.find("division") != std::string::npos);
}

// Recovery and State After Errors
JAI_TEST(vm_error_recovery) {
    // VM should be usable after catching an error
    auto backend = create_vm_backend();
    
    // First execution with error
    try {
        lexer lex("undefined_var;");
        auto tokens = lex.tokenize();
        parser parse(tokens);
        auto declarations = parse.parse();
        backend->execute(declarations);
        expect_true(false); // Should not reach here
    } catch (const runtime_error&) {
        // Expected
    }
    
    // Second execution should work fine
    try {
        lexer lex("5 + 5;");
        auto tokens = lex.tokenize();
        parser parse(tokens);
        auto declarations = parse.parse();
        auto result = backend->execute(declarations);
        expect_eq(result.as<script_int>(), 10);
    } catch (...) {
        expect_true(false); // Should not throw
    }
}

// Complex Error Scenarios
JAI_TEST(vm_complex_error_scenarios) {
    // Error in nested function calls
    expect_true(throws_exception<runtime_error>(R"(
        fun outer() {
            return inner();
        }
        fun inner() {
            return undefined_var;
        }
        outer();
    )"));
    
    // Error in loop body
    expect_true(throws_exception<runtime_error>(R"(
        for (var i = 0; i < 5; ++i) {
            if (i == 3) {
                undefined_operation();
            }
        }
    )"));
    
    // Error in lambda
    expect_true(throws_exception<runtime_error>(R"(
        var f = fun() { return 5 / 0; };
        f();
    )"));
}

// Custom Error Types (if supported)
JAI_TEST(vm_custom_errors) {
    // Test throw statement (if implemented)
    // expect_true(throws_exception<runtime_error>(R"(
    //     throw "Custom error message";
    // )"));
    
    // Test try-catch (if implemented)
    // auto result = compile_and_execute_vm(R"(
    //     try {
    //         throw "Error";
    //         "not reached";
    //     } catch (e) {
    //         "caught: " + e;
    //     }
    // )");
    // expect_eq(result.as<script_string>(), "caught: Error");
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()