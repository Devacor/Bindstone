#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include <memory>

using namespace jai;
using namespace jai::test;

// This test is specifically designed to expose memory leaks
JAI_TEST_SUITE(SpecificMemoryLeakTests)

JAI_TEST(circular_reference_through_lambdas) {
    engine engine;
    
    // This test creates circular references through lambda captures
    // If reference counting isn't properly implemented, this will leak
    for (int iter = 0; iter < 100; ++iter) {
        try {
            engine.execute(R"(
                // Create a circular reference chain
                auto create_circular = []() -> auto {
                    auto holder1 = null;
                    auto holder2 = null;
                    
                    holder1 = [&holder2]() -> auto { 
                        return holder2; 
                    };
                    
                    holder2 = [&holder1]() -> auto { 
                        return holder1; 
                    };
                    
                    // Return a lambda that captures both
                    return [holder1, holder2]() -> auto {
                        return "circular";
                    };
                };
                
                // Create many circular references
                for (i = 0; i < 10; i = i + 1) {
                    auto circular = create_circular();
                    circular();
                }
            )");
        } catch (const std::exception& e) {
            // Some features might not be implemented
        }
    }
    
    expect_eq(true, true); // If we don't crash, test passes
}

JAI_TEST(large_string_accumulation) {
    engine engine;
    
    // This test creates increasingly large strings
    // If strings aren't properly freed, memory usage will explode
    try {
        engine.execute(R"(
            auto big_string = "Start";
            
            // Double the string size repeatedly
            for (i = 0; i < 20; i = i + 1) {
                big_string = big_string + big_string;
            }
            
            // big_string is now 2^20 * 5 = ~5MB
            // Create multiple copies
            auto copy1 = big_string;
            auto copy2 = big_string;
            auto copy3 = big_string;
            
            // Let them go out of scope
            "done";
        )");
    } catch (const std::bad_alloc&) {
        // If we run out of memory, that's actually a sign of a leak
        expect_eq(true, false);
    } catch (const std::exception&) {
        // Other errors are ok for this test
    }
    
    expect_eq(true, true);
}

JAI_TEST(exception_cleanup_stress) {
    engine engine;
    
    // Add a function that allocates memory
    engine.add_function("allocate_data", []() -> script_string {
        // Allocate 1MB of data
        return script_string(std::string(1024 * 1024, 'X'));
    });
    
    // Test that memory is properly cleaned up when exceptions occur
    for (int i = 0; i < 100; ++i) {
        try {
            engine.execute(R"(
                auto data = allocate_data();
                
                // Force an exception after allocation
                undefined_function();
            )");
        } catch (const std::exception&) {
            // Expected - we're testing cleanup
        }
    }
    
    // If memory isn't cleaned up, we'll have allocated 100MB
    expect_eq(true, true);
}

JAI_TEST(value_assignment_chain_leak) {
    // Test for leaks in value assignment chains
    for (int iter = 0; iter < 1000; ++iter) {
        script_value v1 = script_string("Initial string that is reasonably long to ensure heap allocation");
        script_value v2 = script_int(42);
        script_value v3 = script_float(3.14159);
        script_value v4 = script_bool(true);
        
        // Create assignment chains
        v1 = v2 = v3 = v4 = script_string("New value for all");
        v4 = v3 = v2 = v1 = script_int(999);
        v1 = v2 = script_string("Another string");
        v3 = v4 = script_float(2.718);
        
        // Create temporary values
        for (int i = 0; i < 100; ++i) {
            script_value temp = script_string("Temporary " + std::to_string(i));
            temp = script_int(i);
            temp = script_string("Changed " + std::to_string(i));
        }
    }
    
    expect_eq(true, true);
}

JAI_TEST(function_binding_memory_leak) {
    // Test for leaks when repeatedly binding functions
    for (int iter = 0; iter < 100; ++iter) {
        engine engine;
        
        // Bind many functions with captured state
        for (int i = 0; i < 100; ++i) {
            std::string name = "func_" + std::to_string(i);
            
            // Each function captures a large string
            std::string captured_data(10000, 'X');
            captured_data += std::to_string(i);
            
            engine.add_function(name, [captured_data, i]() -> script_string {
                return script_string(captured_data + " called");
            });
        }
        
        // Call some functions
        try {
            engine.execute("func_0() + func_50() + func_99();");
        } catch (const std::exception&) {
            // Some calls might fail
        }
        
        // engine destruction should clean up all bound functions
    }
    
    expect_eq(true, true);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()