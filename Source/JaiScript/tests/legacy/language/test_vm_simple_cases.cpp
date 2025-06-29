#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(VMSimpleCases)

JAI_TEST(vm_after_empty_for_loop) {
    engine vm_engine;
    vm_engine.set_backend(backend_type::jvm);
    
    // Test 1: Value after empty for loop
    {
        script_value result = vm_engine.execute(R"(
            for (var i = 0; i < 0; i = i + 1) {
                // empty loop
            }
            42;
        )");
        
        std::cout << "After empty for loop: ";
        if (result.is_int()) {
            std::cout << "Got int: " << result.as_int() << "\n";
            expect_eq(result.as_int(), 42);
        } else {
            std::cout << "Got type: " << static_cast<int>(result.type()) << "\n";
            expect_true(false);
        }
    }
    
    // Test 2: Variable after for loop
    {
        script_value result = vm_engine.execute(R"(
            var x = 100;
            for (var i = 0; i < 1; i = i + 1) {
                // just one iteration
            }
            x;
        )");
        
        std::cout << "Variable after for loop: ";
        if (result.is_int()) {
            std::cout << "Got int: " << result.as_int() << "\n";
            expect_eq(result.as_int(), 100);
        } else {
            std::cout << "Got type: " << static_cast<int>(result.type()) << "\n";
            expect_true(false);
        }
    }
    
    // Test 3: Expression after for loop
    {
        script_value result = vm_engine.execute(R"(
            var x = 5;
            for (var i = 0; i < 1; i = i + 1) {
                x = x + 1;
            }
            x + 10;
        )");
        
        std::cout << "Expression after for loop: ";
        if (result.is_int()) {
            std::cout << "Got int: " << result.as_int() << "\n";
            expect_eq(result.as_int(), 16); // x becomes 6, then 6 + 10 = 16
        } else {
            std::cout << "Got type: " << static_cast<int>(result.type()) << "\n";
            expect_true(false);
        }
    }
}

JAI_TEST(vm_for_loop_variable_modified) {
    engine vm_engine;
    vm_engine.set_backend(backend_type::jvm);
    
    // The original failing case
    script_value result = vm_engine.execute(R"(
        var sum = 0;
        for (var i = 0; i < 3; i = i + 1) {
            sum = sum + i;
        }
        sum;
    )");
    
    std::cout << "Original for loop case: ";
    if (result.is_int()) {
        std::cout << "Got int: " << result.as_int() << "\n";
        expect_eq(result.as_int(), 3);
    } else {
        std::cout << "Got type: " << static_cast<int>(result.type()) << " (NULL!)\n";
        expect_true(false);
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()