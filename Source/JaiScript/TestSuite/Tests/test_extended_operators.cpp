#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(ExtendedOperators)

JAI_TEST(spaceship_operator_integers) {
    Engine engine;
    
    // Test less than
    Value result1 = engine.execute("5 <=> 10;");
    expect_eq(result1.as<Int>(), -1);
    
    // Test equal
    Value result2 = engine.execute("10 <=> 10;");
    expect_eq(result2.as<Int>(), 0);
    
    // Test greater than
    Value result3 = engine.execute("15 <=> 10;");
    expect_eq(result3.as<Int>(), 1);
}

JAI_TEST(spaceship_operator_floats) {
    Engine engine;
    
    // Test less than
    Value result1 = engine.execute("5.5 <=> 10.5;");
    expect_eq(result1.as<Int>(), -1);
    
    // Test equal
    Value result2 = engine.execute("10.0 <=> 10.0;");
    expect_eq(result2.as<Int>(), 0);
    
    // Test greater than
    Value result3 = engine.execute("15.5 <=> 10.5;");
    expect_eq(result3.as<Int>(), 1);
}

JAI_TEST(spaceship_operator_strings) {
    Engine engine;
    
    // Test less than (lexicographical)
    Value result1 = engine.execute("\"apple\" <=> \"banana\";");
    expect_eq(result1.as<Int>(), -1);
    
    // Test equal
    Value result2 = engine.execute("\"hello\" <=> \"hello\";");
    expect_eq(result2.as<Int>(), 0);
    
    // Test greater than
    Value result3 = engine.execute("\"zebra\" <=> \"apple\";");
    expect_eq(result3.as<Int>(), 1);
}

JAI_TEST(bitwise_and_operator) {
    Engine engine;
    
    // Test bitwise AND operations
    Value result1 = engine.execute("12 & 10;");  // 1100 & 1010 = 1000 = 8
    expect_eq(result1.as<Int>(), 8);
    
    Value result2 = engine.execute("15 & 7;");   // 1111 & 0111 = 0111 = 7
    expect_eq(result2.as<Int>(), 7);
    
    Value result3 = engine.execute("5 & 3;");    // 0101 & 0011 = 0001 = 1
    expect_eq(result3.as<Int>(), 1);
}

JAI_TEST(bitwise_or_operator) {
    Engine engine;
    
    // Test bitwise OR operations
    Value result1 = engine.execute("12 | 10;");  // 1100 | 1010 = 1110 = 14
    expect_eq(result1.as<Int>(), 14);
    
    Value result2 = engine.execute("5 | 3;");    // 0101 | 0011 = 0111 = 7
    expect_eq(result2.as<Int>(), 7);
    
    Value result3 = engine.execute("8 | 4;");    // 1000 | 0100 = 1100 = 12
    expect_eq(result3.as<Int>(), 12);
}

JAI_TEST(bitwise_xor_operator) {
    Engine engine;
    
    // Test bitwise XOR operations
    Value result1 = engine.execute("12 ^ 10;");  // 1100 ^ 1010 = 0110 = 6
    expect_eq(result1.as<Int>(), 6);
    
    Value result2 = engine.execute("5 ^ 3;");    // 0101 ^ 0011 = 0110 = 6
    expect_eq(result2.as<Int>(), 6);
    
    Value result3 = engine.execute("15 ^ 15;");  // Same values XOR to 0
    expect_eq(result3.as<Int>(), 0);
}

JAI_TEST(bitwise_not_operator) {
    Engine engine;
    
    // Test bitwise NOT operations
    Value result1 = engine.execute("~0;");
    expect_eq(result1.as<Int>(), ~0LL);  // All bits flipped
    
    Value result2 = engine.execute("~(-1);");
    expect_eq(result2.as<Int>(), 0);     // ~(-1) = 0
    
    // Test with variable
    engine.execute("var x = 5;");
    Value result3 = engine.execute("~x;");
    expect_eq(result3.as<Int>(), ~5LL);
}

JAI_TEST(left_shift_operator) {
    Engine engine;
    
    // Test left shift operations
    Value result1 = engine.execute("1 << 3;");   // 1 * 2^3 = 8
    expect_eq(result1.as<Int>(), 8);
    
    Value result2 = engine.execute("5 << 2;");   // 5 * 2^2 = 20
    expect_eq(result2.as<Int>(), 20);
    
    Value result3 = engine.execute("10 << 1;");  // 10 * 2^1 = 20
    expect_eq(result3.as<Int>(), 20);
}

JAI_TEST(right_shift_operator) {
    Engine engine;
    
    // Test right shift operations
    Value result1 = engine.execute("8 >> 3;");   // 8 / 2^3 = 1
    expect_eq(result1.as<Int>(), 1);
    
    Value result2 = engine.execute("20 >> 2;");  // 20 / 2^2 = 5
    expect_eq(result2.as<Int>(), 5);
    
    Value result3 = engine.execute("100 >> 1;"); // 100 / 2^1 = 50
    expect_eq(result3.as<Int>(), 50);
}

JAI_TEST(complex_bitwise_expressions) {
    Engine engine;
    
    // Test combination of operations
    Value result1 = engine.execute("(5 << 2) & 15;");  // (5*4) & 15 = 20 & 15 = 4
    expect_eq(result1.as<Int>(), 4);
    
    // Test with variables
    engine.execute("var a = 12; var b = 3;");
    Value result2 = engine.execute("(a << b) >> 2;");  // (12*8)/4 = 96/4 = 24
    expect_eq(result2.as<Int>(), 24);
    
    // Test bitwise NOT with operations
    Value result3 = engine.execute("~(5 & 3);");  // ~(1) = ~1
    expect_eq(result3.as<Int>(), ~1LL);
}

JAI_TEST(bitwise_operator_precedence) {
    Engine engine;
    
    // Test operator precedence
    Value result1 = engine.execute("5 + 3 << 2;");  // (5+3) << 2 = 8 << 2 = 32
    expect_eq(result1.as<Int>(), 32);
    
    Value result2 = engine.execute("16 >> 2 + 1;"); // 16 >> (2+1) = 16 >> 3 = 2
    expect_eq(result2.as<Int>(), 2);
    
    Value result3 = engine.execute("5 & 3 | 4;");   // (5&3) | 4 = 1 | 4 = 5
    expect_eq(result3.as<Int>(), 5);
}

JAI_TEST(bitwise_assignment_operators) {
    Engine engine;
    
    // Test bitwise assignment operators
    engine.execute("var x = 12;");
    
    engine.execute("x &= 10;");  // x = x & 10 = 12 & 10 = 8
    Value result1 = engine.execute("x;");
    expect_eq(result1.as<Int>(), 8);
    
    engine.execute("x |= 3;");   // x = x | 3 = 8 | 3 = 11
    Value result2 = engine.execute("x;");
    expect_eq(result2.as<Int>(), 11);
    
    engine.execute("x ^= 5;");   // x = x ^ 5 = 11 ^ 5 = 14
    Value result3 = engine.execute("x;");
    expect_eq(result3.as<Int>(), 14);
    
    engine.execute("x <<= 1;");  // x = x << 1 = 14 << 1 = 28
    Value result4 = engine.execute("x;");
    expect_eq(result4.as<Int>(), 28);
    
    engine.execute("x >>= 2;");  // x = x >> 2 = 28 >> 2 = 7
    Value result5 = engine.execute("x;");
    expect_eq(result5.as<Int>(), 7);
}

JAI_TEST(bitwise_operators_with_variables) {
    Engine engine;
    
    // Test bitwise operations with variables
    engine.execute(R"(
        var a = 15;   // 1111
        var b = 9;    // 1001
        var c = 6;    // 0110
    )");
    
    Value andResult = engine.execute("a & b;");  // 1111 & 1001 = 1001 = 9
    expect_eq(andResult.as<Int>(), 9);
    
    Value orResult = engine.execute("a | c;");   // 1111 | 0110 = 1111 = 15
    expect_eq(orResult.as<Int>(), 15);
    
    Value xorResult = engine.execute("b ^ c;");  // 1001 ^ 0110 = 1111 = 15
    expect_eq(xorResult.as<Int>(), 15);
}

JAI_TEST(bitwise_operators_edge_cases) {
    Engine engine;
    
    // Test edge cases
    Value result1 = engine.execute("0 & 0xFFFFFFFF;");  // 0 & anything = 0
    expect_eq(result1.as<Int>(), 0);
    
    Value result2 = engine.execute("0xFFFFFFFF | 0;");   // anything | 0 = anything
    expect_eq(result2.as<Int>(), 0xFFFFFFFF);
    
    Value result3 = engine.execute("0 << 10;");         // 0 shifted = 0
    expect_eq(result3.as<Int>(), 0);
    
    Value result4 = engine.execute("0 >> 5;");          // 0 shifted = 0
    expect_eq(result4.as<Int>(), 0);
}

JAI_TEST(spaceship_operator_with_variables) {
    Engine engine;
    
    // Test spaceship operator with variables
    engine.execute(R"(
        var x = 10;
        var y = 20;
        var z = 10;
    )");
    
    Value result1 = engine.execute("x <=> y;");  // 10 <=> 20 = -1
    expect_eq(result1.as<Int>(), -1);
    
    Value result2 = engine.execute("y <=> x;");  // 20 <=> 10 = 1
    expect_eq(result2.as<Int>(), 1);
    
    Value result3 = engine.execute("x <=> z;");  // 10 <=> 10 = 0
    expect_eq(result3.as<Int>(), 0);
}

JAI_TEST(mixed_extended_operators) {
    Engine engine;
    
    // Test mixing different extended operators
    Value result1 = engine.execute("(5 <=> 3) << 2;");  // 1 << 2 = 4
    expect_eq(result1.as<Int>(), 4);
    
    Value result2 = engine.execute("(8 >> 2) <=> 2;");  // 2 <=> 2 = 0
    expect_eq(result2.as<Int>(), 0);
    
    Value result3 = engine.execute("~((3 <=> 5) + 2);"); // ~(-1 + 2) = ~1
    expect_eq(result3.as<Int>(), ~1LL);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()