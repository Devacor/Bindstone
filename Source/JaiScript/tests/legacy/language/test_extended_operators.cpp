#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ExtendedOperators)

// Pre-initialize a shared engine to avoid initialization overhead in test timing
static engine* shared_engine = nullptr;

JAI_INITIALIZE_TEST(
    shared_engine = new engine();
)

JAI_TEST(spaceship_operator_integers) {
    engine& engine = *shared_engine;
    
    // Test less than
    script_value result1 = engine.execute("5 <=> 10;");
    expect_eq(result1.as<script_int>(), -1);
    
    // Test equal
    script_value result2 = engine.execute("10 <=> 10;");
    expect_eq(result2.as<script_int>(), 0);
    
    // Test greater than
    script_value result3 = engine.execute("15 <=> 10;");
    expect_eq(result3.as<script_int>(), 1);
}

JAI_TEST(spaceship_operator_floats) {
    engine& engine = *shared_engine;
    
    // Test less than
    script_value result1 = engine.execute("5.5 <=> 10.5;");
    expect_eq(result1.as<script_int>(), -1);
    
    // Test equal
    script_value result2 = engine.execute("10.0 <=> 10.0;");
    expect_eq(result2.as<script_int>(), 0);
    
    // Test greater than
    script_value result3 = engine.execute("15.5 <=> 10.5;");
    expect_eq(result3.as<script_int>(), 1);
}

JAI_TEST(spaceship_operator_strings) {
    engine& engine = *shared_engine;
    
    // Test less than (lexicographical)
    script_value result1 = engine.execute("\"apple\" <=> \"banana\";");
    expect_eq(result1.as<script_int>(), -1);
    
    // Test equal
    script_value result2 = engine.execute("\"hello\" <=> \"hello\";");
    expect_eq(result2.as<script_int>(), 0);
    
    // Test greater than
    script_value result3 = engine.execute("\"zebra\" <=> \"apple\";");
    expect_eq(result3.as<script_int>(), 1);
}

JAI_TEST(bitwise_and_operator) {
    engine& engine = *shared_engine;
    
    // Test bitwise AND operations
    script_value result1 = engine.execute("12 & 10;");  // 1100 & 1010 = 1000 = 8
    expect_eq(result1.as<script_int>(), 8);
    
    script_value result2 = engine.execute("15 & 7;");   // 1111 & 0111 = 0111 = 7
    expect_eq(result2.as<script_int>(), 7);
    
    script_value result3 = engine.execute("5 & 3;");    // 0101 & 0011 = 0001 = 1
    expect_eq(result3.as<script_int>(), 1);
}

JAI_TEST(bitwise_or_operator) {
    engine& engine = *shared_engine;
    
    // Test bitwise OR operations
    script_value result1 = engine.execute("12 | 10;");  // 1100 | 1010 = 1110 = 14
    expect_eq(result1.as<script_int>(), 14);
    
    script_value result2 = engine.execute("5 | 3;");    // 0101 | 0011 = 0111 = 7
    expect_eq(result2.as<script_int>(), 7);
    
    script_value result3 = engine.execute("8 | 4;");    // 1000 | 0100 = 1100 = 12
    expect_eq(result3.as<script_int>(), 12);
}

JAI_TEST(bitwise_xor_operator) {
    engine& engine = *shared_engine;
    
    // Test bitwise XOR operations
    script_value result1 = engine.execute("12 ^ 10;");  // 1100 ^ 1010 = 0110 = 6
    expect_eq(result1.as<script_int>(), 6);
    
    script_value result2 = engine.execute("5 ^ 3;");    // 0101 ^ 0011 = 0110 = 6
    expect_eq(result2.as<script_int>(), 6);
    
    script_value result3 = engine.execute("15 ^ 15;");  // Same values XOR to 0
    expect_eq(result3.as<script_int>(), 0);
}

JAI_TEST(bitwise_not_operator) {
    engine& engine = *shared_engine;
    
    // Test bitwise NOT operations
    script_value result1 = engine.execute("~0;");
    expect_eq(result1.as<script_int>(), ~0LL);  // All bits flipped
    
    script_value result2 = engine.execute("~(-1);");
    expect_eq(result2.as<script_int>(), 0);     // ~(-1) = 0
    
    // Test with variable
    engine.execute("var x = 5;");
    script_value result3 = engine.execute("~x;");
    expect_eq(result3.as<script_int>(), ~5LL);
}

JAI_TEST(left_shift_operator) {
    engine& engine = *shared_engine;
    
    // Test left shift operations
    script_value result1 = engine.execute("1 << 3;");   // 1 * 2^3 = 8
    expect_eq(result1.as<script_int>(), 8);
    
    script_value result2 = engine.execute("5 << 2;");   // 5 * 2^2 = 20
    expect_eq(result2.as<script_int>(), 20);
    
    script_value result3 = engine.execute("10 << 1;");  // 10 * 2^1 = 20
    expect_eq(result3.as<script_int>(), 20);
}

JAI_TEST(right_shift_operator) {
    engine& engine = *shared_engine;
    
    // Test right shift operations
    script_value result1 = engine.execute("8 >> 3;");   // 8 / 2^3 = 1
    expect_eq(result1.as<script_int>(), 1);
    
    script_value result2 = engine.execute("20 >> 2;");  // 20 / 2^2 = 5
    expect_eq(result2.as<script_int>(), 5);
    
    script_value result3 = engine.execute("100 >> 1;"); // 100 / 2^1 = 50
    expect_eq(result3.as<script_int>(), 50);
}

JAI_TEST(complex_bitwise_expressions) {
    engine& engine = *shared_engine;
    
    // Test combination of operations
    script_value result1 = engine.execute("(5 << 2) & 15;");  // (5*4) & 15 = 20 & 15 = 4
    expect_eq(result1.as<script_int>(), 4);
    
    // Test with variables
    engine.execute("var a = 12; var b = 3;");
    script_value result2 = engine.execute("(a << b) >> 2;");  // (12*8)/4 = 96/4 = 24
    expect_eq(result2.as<script_int>(), 24);
    
    // Test bitwise NOT with operations
    script_value result3 = engine.execute("~(5 & 3);");  // ~(1) = ~1
    expect_eq(result3.as<script_int>(), ~1LL);
}

JAI_TEST(bitwise_operator_precedence) {
    engine& engine = *shared_engine;
    
    // Test operator precedence
    script_value result1 = engine.execute("5 + 3 << 2;");  // (5+3) << 2 = 8 << 2 = 32
    expect_eq(result1.as<script_int>(), 32);
    
    script_value result2 = engine.execute("16 >> 2 + 1;"); // 16 >> (2+1) = 16 >> 3 = 2
    expect_eq(result2.as<script_int>(), 2);
    
    script_value result3 = engine.execute("5 & 3 | 4;");   // (5&3) | 4 = 1 | 4 = 5
    expect_eq(result3.as<script_int>(), 5);
}

JAI_TEST(bitwise_assignment_operators) {
    engine& engine = *shared_engine;
    
    // Test bitwise assignment operators
    engine.execute("var x = 12;");
    
    engine.execute("x &= 10;");  // x = x & 10 = 12 & 10 = 8
    script_value result1 = engine.execute("x;");
    expect_eq(result1.as<script_int>(), 8);
    
    engine.execute("x |= 3;");   // x = x | 3 = 8 | 3 = 11
    script_value result2 = engine.execute("x;");
    expect_eq(result2.as<script_int>(), 11);
    
    engine.execute("x ^= 5;");   // x = x ^ 5 = 11 ^ 5 = 14
    script_value result3 = engine.execute("x;");
    expect_eq(result3.as<script_int>(), 14);
    
    engine.execute("x <<= 1;");  // x = x << 1 = 14 << 1 = 28
    script_value result4 = engine.execute("x;");
    expect_eq(result4.as<script_int>(), 28);
    
    engine.execute("x >>= 2;");  // x = x >> 2 = 28 >> 2 = 7
    script_value result5 = engine.execute("x;");
    expect_eq(result5.as<script_int>(), 7);
}

JAI_TEST(bitwise_operators_with_variables) {
    engine& engine = *shared_engine;
    
    // Test bitwise operations with variables
    engine.execute(R"(
        var a = 15;   // 1111
        var b = 9;    // 1001
        var c = 6;    // 0110
    )");
    
    script_value andResult = engine.execute("a & b;");  // 1111 & 1001 = 1001 = 9
    expect_eq(andResult.as<script_int>(), 9);
    
    script_value orResult = engine.execute("a | c;");   // 1111 | 0110 = 1111 = 15
    expect_eq(orResult.as<script_int>(), 15);
    
    script_value xorResult = engine.execute("b ^ c;");  // 1001 ^ 0110 = 1111 = 15
    expect_eq(xorResult.as<script_int>(), 15);
}

JAI_TEST(bitwise_operators_edge_cases) {
    engine& engine = *shared_engine;
    
    // Test edge cases
    script_value result1 = engine.execute("0 & 0xFFFFFFFF;");  // 0 & anything = 0
    expect_eq(result1.as<script_int>(), 0);
    
    script_value result2 = engine.execute("0xFFFFFFFF | 0;");   // anything | 0 = anything
    expect_eq(result2.as<script_int>(), 0xFFFFFFFF);
    
    script_value result3 = engine.execute("0 << 10;");         // 0 shifted = 0
    expect_eq(result3.as<script_int>(), 0);
    
    script_value result4 = engine.execute("0 >> 5;");          // 0 shifted = 0
    expect_eq(result4.as<script_int>(), 0);
}

JAI_TEST(spaceship_operator_with_variables) {
    engine& engine = *shared_engine;
    
    // Test spaceship operator with variables
    engine.execute(R"(
        var x = 10;
        var y = 20;
        var z = 10;
    )");
    
    script_value result1 = engine.execute("x <=> y;");  // 10 <=> 20 = -1
    expect_eq(result1.as<script_int>(), -1);
    
    script_value result2 = engine.execute("y <=> x;");  // 20 <=> 10 = 1
    expect_eq(result2.as<script_int>(), 1);
    
    script_value result3 = engine.execute("x <=> z;");  // 10 <=> 10 = 0
    expect_eq(result3.as<script_int>(), 0);
}

JAI_TEST(mixed_extended_operators) {
    engine& engine = *shared_engine;
    
    // Test mixing different extended operators
    script_value result1 = engine.execute("(5 <=> 3) << 2;");  // 1 << 2 = 4
    expect_eq(result1.as<script_int>(), 4);
    
    script_value result2 = engine.execute("(8 >> 2) <=> 2;");  // 2 <=> 2 = 0
    expect_eq(result2.as<script_int>(), 0);
    
    script_value result3 = engine.execute("~((3 <=> 5) + 2);"); // ~(-1 + 2) = ~1
    expect_eq(result3.as<script_int>(), ~1LL);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()