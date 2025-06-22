#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <cstdint>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(TypeConversions)

JAI_TEST(integer_type_conversions) {
    Engine engine;
    
    // Test basic integer conversion
    Value result = engine.execute("return 42;");
    expect_eq(result.as<Int>(), 42);
    
    // Test conversion to different integer types
    expect_eq(result.as<int>(), 42);
    expect_eq(result.as<int32_t>(), 42);
    expect_eq(result.as<int64_t>(), 42);
}

JAI_TEST(integer_size_conversions) {
    Engine engine;
    
    // Test int8_t conversion
    Value result1 = engine.execute("return 127;");
    expect_eq(static_cast<int8_t>(result1.as<Int>()), static_cast<int8_t>(127));
    
    // Test int16_t conversion  
    Value result2 = engine.execute("return 32000;");
    expect_eq(static_cast<int16_t>(result2.as<Int>()), static_cast<int16_t>(32000));
    
    // Test int32_t conversion
    Value result3 = engine.execute("return 123456;");
    expect_eq(static_cast<int32_t>(result3.as<Int>()), static_cast<int32_t>(123456));
    
    // Test int64_t conversion
    Value result4 = engine.execute("return 9876543210;");
    expect_eq(result4.as<Int>(), static_cast<int64_t>(9876543210));
}

JAI_TEST(unsigned_integer_conversions) {
    Engine engine;
    
    // Test uint8_t conversion
    Value result1 = engine.execute("return 255;");
    expect_eq(static_cast<uint8_t>(result1.as<Int>()), static_cast<uint8_t>(255));
    
    // Test uint16_t conversion
    Value result2 = engine.execute("return 65000;");
    expect_eq(static_cast<uint16_t>(result2.as<Int>()), static_cast<uint16_t>(65000));
    
    // Test uint32_t conversion  
    Value result3 = engine.execute("return 4000000000;");
    expect_eq(static_cast<uint32_t>(result3.as<Int>()), static_cast<uint32_t>(4000000000));
    
    // Test uint64_t conversion
    Value result4 = engine.execute("return 18446744073709551615;");
    expect_eq(static_cast<uint64_t>(result4.as<Int>()), static_cast<uint64_t>(18446744073709551615ULL));
}

JAI_TEST(float_type_conversions) {
    Engine engine;
    
    // Test basic float conversion
    Value result1 = engine.execute("return 3.14;");
    expect_near(result1.as<Float>(), 3.14, 0.001);
    
    // Test float conversion  
    expect_near(static_cast<float>(result1.as<Float>()), 3.14f, 0.001f);
    
    // Test double conversion
    expect_near(result1.as<Float>(), 3.14, 0.001);
}

JAI_TEST(string_type_conversions) {
    Engine engine;
    
    // Test string conversion
    Value result = engine.execute("return \"Hello, World!\";");
    expect_eq(result.as<String>(), "Hello, World!");
    
    // Test std::string conversion
    expect_eq(result.as<std::string>(), std::string("Hello, World!"));
}

JAI_TEST(boolean_type_conversions) {
    Engine engine;
    
    // Test true conversion
    Value result1 = engine.execute("return true;");
    expect_eq(result1.as<Bool>(), true);
    expect_eq(result1.as<bool>(), true);
    
    // Test false conversion
    Value result2 = engine.execute("return false;");
    expect_eq(result2.as<Bool>(), false);
    expect_eq(result2.as<bool>(), false);
}

JAI_TEST(character_type_conversions) {
    Engine engine;
    
    // Test character conversion
    Value result = engine.execute("return 'A';");
    expect_eq(result.as<Char>(), 'A');
    expect_eq(result.as<char>(), 'A');
}

JAI_TEST(arithmetic_expression_conversions) {
    Engine engine;
    
    // Test arithmetic result conversions
    Value result1 = engine.execute("return 10 + 32;");
    expect_eq(result1.as<Int>(), 42);
    expect_eq(static_cast<int32_t>(result1.as<Int>()), 42);
    
    Value result2 = engine.execute("return 3.14 * 2.0;");
    expect_near(result2.as<Float>(), 6.28, 0.001);
    expect_near(static_cast<float>(result2.as<Float>()), 6.28f, 0.001f);
}

JAI_TEST(variable_assignment_conversions) {
    Engine engine;
    
    // Test variable assignment and conversion
    Value result = engine.execute(R"(
        var x = 100;
        var y = 200;
        return x + y;
    )");
    
    expect_eq(result.as<Int>(), 300);
    expect_eq(static_cast<int16_t>(result.as<Int>()), static_cast<int16_t>(300));
    expect_eq(static_cast<uint32_t>(result.as<Int>()), static_cast<uint32_t>(300));
}

JAI_TEST(function_return_conversions) {
    Engine engine;
    
    // Test function return value conversions
    Value result = engine.execute(R"(
        function calculate() -> auto {
            return 42 * 2;
        }
        return calculate();
    )");
    
    expect_eq(result.as<Int>(), 84);
    expect_eq(static_cast<int8_t>(result.as<Int>()), static_cast<int8_t>(84));
    expect_eq(static_cast<uint16_t>(result.as<Int>()), static_cast<uint16_t>(84));
}

JAI_TEST(mixed_type_expression_conversions) {
    Engine engine;
    
    // Test mixed type expressions
    Value result1 = engine.execute(R"(
        var intVal = 10;
        var floatVal = 3.5;
        return intVal + floatVal;
    )");
    
    // Result should be promoted to float
    expect_near(result1.as<Float>(), 13.5, 0.001);
}

JAI_TEST(conditional_expression_conversions) {
    Engine engine;
    
    // Test conditional expression conversions
    Value result1 = engine.execute(R"(
        var condition = true;
        return condition ? 100 : 200;
    )");
    
    expect_eq(result1.as<Int>(), 100);
    expect_eq(static_cast<uint8_t>(result1.as<Int>()), static_cast<uint8_t>(100));
    
    Value result2 = engine.execute(R"(
        var condition = false;
        return condition ? 100 : 200;
    )");
    
    expect_eq(result2.as<Int>(), 200);
    expect_eq(static_cast<int16_t>(result2.as<Int>()), static_cast<int16_t>(200));
}

JAI_TEST(loop_variable_conversions) {
    Engine engine;
    
    // Test loop variable conversions
    Value result = engine.execute(R"(
        var sum = 0;
        for (var i = 1; i <= 10; i = i + 1) {
            sum = sum + i;
        }
        return sum;
    )");
    
    expect_eq(result.as<Int>(), 55); // 1+2+...+10 = 55
    expect_eq(static_cast<uint32_t>(result.as<Int>()), static_cast<uint32_t>(55));
}

JAI_TEST(bounds_testing) {
    Engine engine;
    
    // Test boundary values
    Value maxInt = engine.execute("return 2147483647;"); // Max int32
    expect_eq(static_cast<int32_t>(maxInt.as<Int>()), 2147483647);
    
    Value minInt = engine.execute("return -2147483648;"); // Min int32  
    expect_eq(static_cast<int32_t>(minInt.as<Int>()), -2147483648);
    
    // Test large values
    Value largeInt = engine.execute("return 9223372036854775807;"); // Max int64
    expect_eq(largeInt.as<Int>(), 9223372036854775807LL);
}

JAI_TEST(precision_testing) {
    Engine engine;
    
    // Test floating point precision
    Value preciseFloat = engine.execute("return 3.141592653589793;");
    expect_near(preciseFloat.as<Float>(), 3.141592653589793, 0.000000000000001);
    
    Value smallFloat = engine.execute("return 0.000001;");
    expect_near(smallFloat.as<Float>(), 0.000001, 0.0000001);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()