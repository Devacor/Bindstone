#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ScriptReferences)

JAI_TEST(reference_parameter_modifies_original) {
    engine engine;
    
    // Test that reference parameters can modify the original variable
    script_value result = engine.execute(R"(
        function increment(int& x) {
            x = x + 1;
        }
        
        var a = 10;
        increment(a);
        a;  // Should be 11
    )");
    
    expect_eq(result.as<int>(), 11);
}

JAI_TEST(value_parameter_does_not_modify_original) {
    engine engine;
    
    // Test that value parameters don't modify the original
    script_value result = engine.execute(R"(
        function increment(int x) {
            x = x + 1;
        }
        
        var a = 10;
        increment(a);
        a;  // Should still be 10
    )");
    
    expect_eq(result.as<int>(), 10);
}

JAI_TEST(reference_swap_function) {
    engine engine;
    
    // Classic swap function using references
    engine.execute(R"(
        function swap(int& a, int& b) {
            var temp = a;
            a = b;
            b = temp;
        }
        
        var x = 5;
        var y = 10;
        swap(x, y);
    )");
    
    script_value x = engine.get_variable("x");
    script_value y = engine.get_variable("y");
    
    expect_eq(x.as<int>(), 10);
    expect_eq(y.as<int>(), 5);
}

// const is not supported in JaiScript by design
// JAI_TEST(const_reference_parameter) - removed

// TODO: Array element references not yet supported
// JAI_TEST(reference_to_array_element) - disabled for now

// TODO: Map value references not yet supported  
// JAI_TEST(reference_to_map_value) - disabled for now

JAI_TEST(reference_chaining) {
    engine engine;
    
    // Test that passing a reference to another reference chains correctly
    script_value result = engine.execute(R"(
        function modifyRef(int& x) {
            x = 999;
        }
        
        function passThrough(int& y) {
            modifyRef(y);  // Pass reference to another reference
        }
        
        var original = 42;
        passThrough(original);
        original;  // Should be 999
    )");
    
    expect_eq(result.as<int>(), 999);
}

JAI_TEST(multiple_reference_parameters) {
    engine engine;
    
    // Test function with multiple reference parameters
    engine.execute(R"(
        function multiRef(int& a, int& b, int c) {
            a = a + 10;
            b = b + 20;
            c = c + 30;  // Should not affect original
        }
        
        var x = 1;
        var y = 2;
        var z = 3;
        multiRef(x, y, z);
    )");
    
    script_value x = engine.get_variable("x");
    script_value y = engine.get_variable("y");
    script_value z = engine.get_variable("z");
    
    expect_eq(x.as<int>(), 11);
    expect_eq(y.as<int>(), 22);
    expect_eq(z.as<int>(), 3);  // Unchanged
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()