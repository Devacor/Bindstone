#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <iostream>

using namespace jai;
using namespace jai::test;

// Helper to test both backends and report differences
void test_both_backends(const std::string& test_name, const std::string& script) {
    std::cout << "\nTesting: " << test_name << "\n";
    std::cout << "Script: " << script << "\n";
    
    // Test interpreter
    try {
        engine interp_engine;
        interp_engine.set_backend(backend_type::interpreter);
        auto result = interp_engine.execute(script);
        std::cout << "Interpreter: SUCCESS - " << result.to_string() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Interpreter: FAILED - " << e.what() << "\n";
    }
    
    // Test JVM
    try {
        engine jvm_engine;
        jvm_engine.set_backend(backend_type::jvm);
        auto result = jvm_engine.execute(script);
        std::cout << "JVM: SUCCESS - " << result.to_string() << "\n";
    } catch (const std::exception& e) {
        std::cout << "JVM: FAILED - " << e.what() << "\n";
    }
}

JAI_TEST_SUITE(VMFixes)

JAI_TEST(debug_vm_issues) {
    std::cout << "\n=== Debugging VM Issues ===\n";
    
    // Test 1: Simple variable
    test_both_backends("Simple Variable", "var x = 42; x;");
    
    // Test 2: Variable in expression
    test_both_backends("Variable Expression", "var x = 10; x + 5;");
    
    // Test 3: Multiple variables
    test_both_backends("Multiple Variables", R"(
        var x = 10;
        var y = 20;
        x + y;
    )");
    
    // Test 4: Variable reassignment
    test_both_backends("Variable Reassignment", R"(
        var x = 10;
        x = x + 1;
        x;
    )");
    
    // Test 5: Simple function
    test_both_backends("Simple Function", R"(
        fun add(a, b) {
            return a + b;
        }
        add(5, 3);
    )");
    
    // Test 5b: Function without explicit return (should return null)
    test_both_backends("Function No Return", R"(
        fun noReturn() {
            42;  // This should NOT implicitly return
        }
        noReturn();
    )");
    
    // Test 6: Function with local variables
    test_both_backends("Function with Locals", R"(
        fun compute(x) {
            var y = x * 2;
            return y + 1;
        }
        compute(5);
    )");
    
    // Test 7: Nested scopes
    test_both_backends("Nested Scopes", R"(
        var x = 10;
        {
            var y = 20;
            x + y;
        }
    )");
    
    // Test 8: Function accessing global
    test_both_backends("Function with Global", R"(
        var global = 100;
        fun useGlobal() {
            return global + 1;
        }
        useGlobal();
    )");
    
    expect_true(true);
}

JAI_TEST(pinpoint_function_issue) {
    std::cout << "\n=== Pinpointing Function Issues ===\n";
    
    engine jvm_engine;
    jvm_engine.set_backend(backend_type::jvm);
    
    // Test progressive complexity
    try {
        std::cout << "1. Empty function: ";
        jvm_engine.execute("fun test() {} test();");
        std::cout << "OK\n";
    } catch (const std::exception& e) {
        std::cout << "FAILED - " << e.what() << "\n";
    }
    
    try {
        std::cout << "2. Function with return: ";
        jvm_engine.execute("fun test() { return 42; } test();");
        std::cout << "OK\n";
    } catch (const std::exception& e) {
        std::cout << "FAILED - " << e.what() << "\n";
    }
    
    try {
        std::cout << "3. Function with parameter: ";
        jvm_engine.execute("fun test(x) { return x; } test(42);");
        std::cout << "OK\n";
    } catch (const std::exception& e) {
        std::cout << "FAILED - " << e.what() << "\n";
    }
    
    try {
        std::cout << "4. Function with expression: ";
        jvm_engine.execute("fun test(x) { return x + 1; } test(41);");
        std::cout << "OK\n";
    } catch (const std::exception& e) {
        std::cout << "FAILED - " << e.what() << "\n";
    }
    
    try {
        std::cout << "5. Function with local var: ";
        jvm_engine.execute("fun test(x) { var y = 1; return x + y; } test(41);");
        std::cout << "OK\n";
    } catch (const std::exception& e) {
        std::cout << "FAILED - " << e.what() << "\n";
    }
    
    expect_true(true);
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()