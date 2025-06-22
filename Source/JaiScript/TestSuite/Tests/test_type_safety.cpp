#include "../../include/jaiscript/jaiscript.hpp"
#include "../test_base.hpp"
#include <limits>

using namespace JaiScript;

JAI_TEST_SUITE(TypeSafetyTests) {
    // Working bounds checking tests
    JAI_TEST(positive_to_unsigned) {
        Engine engine;
        Value result = engine.execute<uint32_t>("return 42;");
        expect_eq(result.as<uint32_t>(), 42u);
    }
    
    JAI_TEST(zero_to_unsigned) {
        Engine engine;
        Value result = engine.execute<uint32_t>("return 0;");
        expect_eq(result.as<uint32_t>(), 0u);
    }
    
    JAI_TEST(large_positive_to_unsigned) {
        Engine engine;
        Value result = engine.execute<uint32_t>("return 4000000000;");
        expect_eq(result.as<uint32_t>(), 4000000000u);
    }
    
    // Working return validation tests
    JAI_TEST(simple_return_works) {
        Engine engine;
        Value result = engine.execute<int>("x = 42; return x;");
        expect_eq(result.as<int>(), 42);
    }
    
    JAI_TEST(computed_return_works) {
        Engine engine;
        Value result = engine.execute<int>("a = 10; b = 20; return a + b;");
        expect_eq(result.as<int>(), 30);
    }
    
    JAI_TEST(expression_return_works) {
        Engine engine;
        Value result = engine.execute<int>("return 5 * 8;");
        expect_eq(result.as<int>(), 40);
    }
    
    JAI_TEST(non_typed_execute_without_return) {
        Engine engine;
        Value result = engine.execute("x = 42; y = x * 2;");
        expect_eq(engine.hasVariable("x"), true);
        expect_eq(engine.getVariable("x").as<Int>(), 42);
    }
    
    JAI_TEST(non_typed_execute_with_return) {
        Engine engine;
        Value result = engine.execute("x = 42; return x;");
        expect_eq(result.as<Int>(), 42);
    }
    
    // Working type range tests
    JAI_TEST(valid_int16_range) {
        Engine engine;
        Value result1 = engine.execute<int16_t>("return 32767;");  // max
        expect_eq(result1.as<int16_t>(), 32767);
        
        Value result2 = engine.execute<int16_t>("return -32768;"); // min
        expect_eq(result2.as<int16_t>(), -32768);
    }
    
    JAI_TEST(valid_uint16_range) {
        Engine engine;
        Value result1 = engine.execute<uint16_t>("return 65535;"); // max
        expect_eq(result1.as<uint16_t>(), 65535u);
        
        Value result2 = engine.execute<uint16_t>("return 0;"); // min
        expect_eq(result2.as<uint16_t>(), 0u);
    }
}