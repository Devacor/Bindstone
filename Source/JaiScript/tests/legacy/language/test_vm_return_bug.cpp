#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(VMReturnBug)

JAI_TEST(vm_implicit_return_simple) {
    // Test that VM properly returns the last expression value
    engine vm_engine;
    vm_engine.set_backend(backend_type::jvm);
    
    // Simple case - just a literal
    {
        script_value result = vm_engine.execute("42;");
        std::cout << "Test 1 - literal '42;': ";
        if (result.is_int()) {
            std::cout << "Got int: " << result.as_int() << "\n";
            expect_eq(result.as_int(), 42);
        } else {
            std::cout << "Got type: " << static_cast<int>(result.type()) << " (expected int)\n";
            expect_true(false);
        }
    }
    
    // Variable case
    {
        script_value result = vm_engine.execute("var x = 10; x;");
        std::cout << "Test 2 - variable 'var x = 10; x;': ";
        if (result.is_int()) {
            std::cout << "Got int: " << result.as_int() << "\n";
            expect_eq(result.as_int(), 10);
        } else {
            std::cout << "Got type: " << static_cast<int>(result.type()) << " (expected int)\n";
            expect_true(false);
        }
    }
    
    // Expression case
    {
        script_value result = vm_engine.execute("var x = 10; x + 5;");
        std::cout << "Test 3 - expression 'var x = 10; x + 5;': ";
        if (result.is_int()) {
            std::cout << "Got int: " << result.as_int() << "\n";
            expect_eq(result.as_int(), 15);
        } else {
            std::cout << "Got type: " << static_cast<int>(result.type()) << " (expected int)\n";
            expect_true(false);
        }
    }
}

JAI_TEST(vm_for_loop_return) {
    engine vm_engine;
    vm_engine.set_backend(backend_type::jvm);
    
    // The failing test case
    script_value result = vm_engine.execute(R"(
        var sum = 0;
        for (var i = 0; i < 3; i = i + 1) {
            sum = sum + i;
        }
        sum;
    )");
    
    std::cout << "For loop test - result type: " << static_cast<int>(result.type()) << "\n";
    if (result.is_int()) {
        std::cout << "Got int: " << result.as_int() << "\n";
        expect_eq(result.as_int(), 3);
    } else if (result.is_null()) {
        std::cout << "ERROR: Got null instead of int!\n";
        expect_true(false);
    } else {
        std::cout << "ERROR: Got unexpected type!\n";
        expect_true(false);
    }
}

JAI_TEST(compare_backends_for_loop) {
    // Compare interpreter vs VM for the same code
    std::string script = R"(
        var sum = 0;
        for (var i = 0; i < 3; i = i + 1) {
            sum = sum + i;
        }
        sum;
    )";
    
    // Test interpreter
    engine interp_engine;
    interp_engine.set_backend(backend_type::interpreter);
    script_value interp_result = interp_engine.execute(script);
    
    // Test VM
    engine vm_engine;
    vm_engine.set_backend(backend_type::jvm);
    script_value vm_result = vm_engine.execute(script);
    
    std::cout << "Interpreter result: ";
    if (interp_result.is_int()) {
        std::cout << interp_result.as_int() << " (int)\n";
    } else {
        std::cout << "type=" << static_cast<int>(interp_result.type()) << "\n";
    }
    
    std::cout << "VM result: ";
    if (vm_result.is_int()) {
        std::cout << vm_result.as_int() << " (int)\n";
    } else {
        std::cout << "type=" << static_cast<int>(vm_result.type()) << "\n";
    }
    
    // Both should return the same result
    expect_eq(interp_result.type(), vm_result.type());
    if (interp_result.is_int() && vm_result.is_int()) {
        expect_eq(interp_result.as_int(), vm_result.as_int());
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()