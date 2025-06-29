#include "../jai_test.hpp"
#include "../../include/jaiscript/jaiscript.hpp"
#include <limits>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(TypeSafetyTests)

// Working bounds checking tests
JAI_TEST(positive_to_unsigned) {
    engine engine;
    script_value result = engine.execute("42;");
    expect_eq(static_cast<uint32_t>(result.as_int()), 42u);
}

JAI_TEST(zero_to_unsigned) {
    engine engine;
    script_value result = engine.execute("0;");
    expect_eq(static_cast<uint32_t>(result.as_int()), 0u);
}

JAI_TEST(large_positive_to_unsigned) {
    engine engine;
    script_value result = engine.execute("4000000000;");
    expect_eq(static_cast<uint32_t>(result.as_int()), 4000000000u);
}

// Working return validation tests
JAI_TEST(simple_return_works) {
    engine engine;
    script_value result = engine.execute("var x = 42; x;");
    expect_eq(result.as_int(), script_int(42));
}

JAI_TEST(computed_return_works) {
    engine engine;
    script_value result = engine.execute("var a = 10; var b = 20; a + b;");
    expect_eq(result.as_int(), script_int(30));
}

JAI_TEST(expression_return_works) {
    engine engine;
    script_value result = engine.execute("5 * 8;");
    expect_eq(result.as_int(), script_int(40));
}

JAI_TEST(non_typed_execute_without_return) {
    engine engine;
    script_value result = engine.execute("var x = 42; var y = x * 2;");
    expect_eq(engine.has_variable("x"), true);
    expect_eq(engine.get_variable("x").as<script_int>(), 42);
}

JAI_TEST(non_typed_execute_with_return) {
    engine engine;
    script_value result = engine.execute("var x = 42; return x;");
    expect_eq(result.as<script_int>(), 42);
}

// Working type range tests
JAI_TEST(valid_int16_range) {
    engine engine;
    script_value result1 = engine.execute("32767;");  // max
    expect_eq(result1.as<int16_t>(), int16_t(32767));
    
    script_value result2 = engine.execute("-32768;"); // min
    expect_eq(result2.as<int16_t>(), int16_t(-32768));
}

JAI_TEST(valid_uint16_range) {
    engine engine;
    script_value result1 = engine.execute("65535;"); // max
    expect_eq(result1.as<uint16_t>(), uint16_t(65535));
    
    script_value result2 = engine.execute("0;"); // min
    expect_eq(result2.as<uint16_t>(), uint16_t(0));
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()