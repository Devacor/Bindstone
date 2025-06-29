#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(Operators)

// Compound Assignment Operators
JAI_TEST(compound_assignment_plus_equals) {
    engine engine;
    
    // Test with integers
    engine.execute("int x = 10;");
    engine.execute("x += 5;");
    expect_eq(engine.get_variable("x").as<int64_t>(), int64_t(15));
    
    // Test with floats
    engine.execute("float y = 10.5;");
    engine.execute("y += 2.5;");
    expect_eq(engine.get_variable("y").as<double>(), 13.0);
    
    // Test with strings
    engine.execute("string s = \"Hello\";");
    engine.execute("s += \" World\";");
    expect_eq(engine.get_variable("s").as<std::string>(), std::string("Hello World"));
}

JAI_TEST(compound_assignment_minus_equals) {
    engine engine;
    
    engine.execute("int x = 10;");
    engine.execute("x -= 3;");
    expect_eq(engine.get_variable("x").as<int64_t>(), int64_t(7));
    
    engine.execute("float y = 10.5;");
    engine.execute("y -= 2.5;");
    expect_eq(engine.get_variable("y").as<double>(), 8.0);
}

JAI_TEST(compound_assignment_multiply_equals) {
    engine engine;
    
    engine.execute("int x = 10;");
    engine.execute("x *= 3;");
    expect_eq(engine.get_variable("x").as<int>(), 30);
    
    engine.execute("float y = 2.5;");
    engine.execute("y *= 4.0;");
    expect_eq(engine.get_variable("y").as<double>(), 10.0);
}

JAI_TEST(compound_assignment_divide_equals) {
    engine engine;
    
    engine.execute("int x = 20;");
    engine.execute("x /= 4;");
    expect_eq(engine.get_variable("x").as<int>(), 5);
    
    engine.execute("float y = 10.0;");
    engine.execute("y /= 4.0;");
    expect_eq(engine.get_variable("y").as<double>(), 2.5);
}


// Increment/Decrement Operators
JAI_TEST(prefix_increment) {
    engine engine;
    
    engine.execute("int x = 10;");
    auto result = engine.execute("++x");
    expect_eq(result.as<int>(), 11);
    expect_eq(engine.get_variable("x").as<int>(), 11);
    
    engine.execute("float y = 1.5;");
    auto floatResult = engine.execute("++y");
    expect_eq(floatResult.as<double>(), 2.5);
    expect_eq(engine.get_variable("y").as<double>(), 2.5);
}

JAI_TEST(postfix_increment) {
    engine engine;
    
    engine.execute("int x = 10;");
    auto result = engine.execute("x++");
    expect_eq(result.as<int>(), 10);  // Should return old value
    expect_eq(engine.get_variable("x").as<int>(), 11);  // But variable is incremented
    
    engine.execute("float y = 1.5;");
    auto floatResult = engine.execute("y++");
    expect_eq(floatResult.as<double>(), 1.5);  // Should return old value
    expect_eq(engine.get_variable("y").as<double>(), 2.5);  // But variable is incremented
}

JAI_TEST(prefix_decrement) {
    engine engine;
    
    engine.execute("int x = 10;");
    auto result = engine.execute("--x");
    expect_eq(result.as<int>(), 9);
    expect_eq(engine.get_variable("x").as<int>(), 9);
    
    engine.execute("float y = 1.5;");
    auto floatResult = engine.execute("--y");
    expect_eq(floatResult.as<double>(), 0.5);
    expect_eq(engine.get_variable("y").as<double>(), 0.5);
}

JAI_TEST(postfix_decrement) {
    engine engine;
    
    engine.execute("int x = 10;");
    auto result = engine.execute("x--");
    expect_eq(result.as<int>(), 10);  // Should return old value
    expect_eq(engine.get_variable("x").as<int>(), 9);  // But variable is decremented
    
    engine.execute("float y = 1.5;");
    auto floatResult = engine.execute("y--");
    expect_eq(floatResult.as<double>(), 1.5);  // Should return old value
    expect_eq(engine.get_variable("y").as<double>(), 0.5);  // But variable is decremented
}

// Complex Operator Tests
JAI_TEST(chained_compound_assignments) {
    engine engine;
    
    engine.execute("int x = 10;");
    engine.execute("int y = 5;");
    engine.execute("x += y *= 2;");  // y *= 2 first (y = 10), then x += 10 (x = 20)
    expect_eq(engine.get_variable("x").as<int>(), 20);
    expect_eq(engine.get_variable("y").as<int>(), 10);
}

JAI_TEST(increment_in_expressions) {
    engine engine;
    
    engine.execute("int x = 5;");
    auto result = engine.execute("int y = x++ + ++x;");  // 5 + 7 = 12
    expect_eq(engine.get_variable("y").as<int>(), 12);
    expect_eq(engine.get_variable("x").as<int>(), 7);
}

JAI_TEST(compound_assignment_with_expressions) {
    engine engine;
    
    engine.execute("int x = 10;");
    engine.execute("int y = 3;");
    engine.execute("x += y * 2 + 1;");  // x += (3 * 2 + 1) = x += 7
    expect_eq(engine.get_variable("x").as<int>(), 17);
}

JAI_TEST(operator_precedence) {
    engine engine;
    
    // Test that operators have correct precedence
    auto result = engine.execute("2 + 3 * 4");  // Should be 14, not 20
    expect_eq(result.as<int>(), 14);
    
    result = engine.execute("10 - 4 - 2");  // Should be left-associative: (10 - 4) - 2 = 4
    expect_eq(result.as<int>(), 4);
    
    result = engine.execute("2 * 3 + 4 * 5");  // Should be (2*3) + (4*5) = 6 + 20 = 26
    expect_eq(result.as<int>(), 26);
}

JAI_TEST(comparison_operators) {
    engine engine;
    
    expect_eq(engine.execute("5 == 5").as<bool>(), true);
    expect_eq(engine.execute("5 == 6").as<bool>(), false);
    expect_eq(engine.execute("5 != 6").as<bool>(), true);
    expect_eq(engine.execute("5 != 5").as<bool>(), false);
    
    expect_eq(engine.execute("5 < 6").as<bool>(), true);
    expect_eq(engine.execute("6 < 5").as<bool>(), false);
    expect_eq(engine.execute("5 <= 5").as<bool>(), true);
    expect_eq(engine.execute("5 <= 6").as<bool>(), true);
    expect_eq(engine.execute("6 <= 5").as<bool>(), false);
    
    expect_eq(engine.execute("6 > 5").as<bool>(), true);
    expect_eq(engine.execute("5 > 6").as<bool>(), false);
    expect_eq(engine.execute("5 >= 5").as<bool>(), true);
    expect_eq(engine.execute("6 >= 5").as<bool>(), true);
    expect_eq(engine.execute("5 >= 6").as<bool>(), false);
}

JAI_TEST(logical_operators) {
    engine engine;
    
    // AND operator
    expect_eq(engine.execute("true && true").as<bool>(), true);
    expect_eq(engine.execute("true && false").as<bool>(), false);
    expect_eq(engine.execute("false && true").as<bool>(), false);
    expect_eq(engine.execute("false && false").as<bool>(), false);
    
    // OR operator
    expect_eq(engine.execute("true || true").as<bool>(), true);
    expect_eq(engine.execute("true || false").as<bool>(), true);
    expect_eq(engine.execute("false || true").as<bool>(), true);
    expect_eq(engine.execute("false || false").as<bool>(), false);
    
    // NOT operator
    expect_eq(engine.execute("!true").as<bool>(), false);
    expect_eq(engine.execute("!false").as<bool>(), true);
    
    // Short-circuit evaluation
    engine.execute("int x = 0;");
    engine.execute("false && (x = 10) == 10;");
    expect_eq(engine.get_variable("x").as<int>(), 0);  // x should not be modified
    
    engine.execute("true || (x = 20) == 20;");
    expect_eq(engine.get_variable("x").as<int>(), 0);  // x should not be modified
}

// Spaceship operator tests (C++20 three-way comparison)
JAI_TEST(spaceship_operator_integers) {
    engine engine;
    
    // Integer comparisons
    expect_eq(engine.execute("5 <=> 10").as<int>(), -1);  // Less than
    expect_eq(engine.execute("10 <=> 5").as<int>(), 1);   // Greater than  
    expect_eq(engine.execute("7 <=> 7").as<int>(), 0);    // Equal
    
    // Edge cases
    expect_eq(engine.execute("0 <=> 0").as<int>(), 0);
    expect_eq(engine.execute("-5 <=> 5").as<int>(), -1);
    expect_eq(engine.execute("5 <=> -5").as<int>(), 1);
}

JAI_TEST(spaceship_operator_floats) {
    engine engine;
    
    // script_float comparisons
    expect_eq(engine.execute("5.5 <=> 10.2").as<int>(), -1);
    expect_eq(engine.execute("10.2 <=> 5.5").as<int>(), 1);
    expect_eq(engine.execute("7.7 <=> 7.7").as<int>(), 0);
    
    // Mixed int/float
    expect_eq(engine.execute("5 <=> 5.0").as<int>(), 0);
    expect_eq(engine.execute("5 <=> 5.1").as<int>(), -1);
    expect_eq(engine.execute("5.1 <=> 5").as<int>(), 1);
}

JAI_TEST(spaceship_operator_strings) {
    engine engine;
    
    // script_string comparisons (lexicographic)
    expect_eq(engine.execute("\"apple\" <=> \"banana\"").as<int>(), -1);
    expect_eq(engine.execute("\"banana\" <=> \"apple\"").as<int>(), 1);
    expect_eq(engine.execute("\"hello\" <=> \"hello\"").as<int>(), 0);
    
    // Case sensitivity
    expect_eq(engine.execute("\"Apple\" <=> \"apple\"").as<int>(), -1);  // 'A' < 'a' in ASCII
}

JAI_TEST(spaceship_operator_consistency) {
    engine engine;
    
    // Verify spaceship operator is consistent with comparison operators
    // If a <=> b returns -1, then a < b should be true and a > b should be false
    engine.execute("int a = 5; int b = 10;");
    
    auto spaceship_result = engine.execute("a <=> b").as<int>();
    expect_eq(spaceship_result, -1);
    expect_eq(engine.execute("a < b").as<bool>(), true);
    expect_eq(engine.execute("a > b").as<bool>(), false);
    expect_eq(engine.execute("a == b").as<bool>(), false);
    
    // Test equality case
    engine.execute("int c = 7; int d = 7;");
    spaceship_result = engine.execute("c <=> d").as<int>();
    expect_eq(spaceship_result, 0);
    expect_eq(engine.execute("c == d").as<bool>(), true);
    expect_eq(engine.execute("c < d").as<bool>(), false);
    expect_eq(engine.execute("c > d").as<bool>(), false);
}

JAI_TEST(spaceship_operator_in_expressions) {
    engine engine;
    
    // Use spaceship operator in more complex expressions
    auto result = engine.execute("(5 <=> 3) + (10 <=> 15)");  // 1 + (-1) = 0
    expect_eq(result.as<int>(), 0);
    
    // Use in conditional
    engine.execute("int comparison = 5 <=> 10;");
    engine.execute("int result = comparison < 0 ? -100 : (comparison > 0 ? 100 : 0);");
    expect_eq(engine.get_variable("result").as<int>(), -100);
}

// Bitwise operator tests
JAI_TEST(bitwise_operators) {
    engine engine;
    
    // Bitwise AND
    expect_eq(engine.execute("12 & 10").as<int>(), 8);  // 1100 & 1010 = 1000 = 8
    expect_eq(engine.execute("7 & 3").as<int>(), 3);    // 0111 & 0011 = 0011 = 3
    
    // Bitwise NOT
    expect_eq(engine.execute("~0").as<int>(), -1);      // Two's complement
    expect_eq(engine.execute("~(-1)").as<int>(), 0);
    
    // Left shift
    expect_eq(engine.execute("1 << 3").as<int>(), 8);   // 1 shifted left 3 positions
    expect_eq(engine.execute("5 << 2").as<int>(), 20);  // 5 * 4 = 20
    
    // Right shift  
    expect_eq(engine.execute("16 >> 2").as<int>(), 4);  // 16 / 4 = 4
    expect_eq(engine.execute("20 >> 2").as<int>(), 5);  // 20 / 4 = 5
}

JAI_TEST(bitwise_operator_precedence) {
    engine engine;
    
    // Test precedence: shift has higher precedence than bitwise AND
    auto result = engine.execute("8 & 4 << 1");  // Should be 8 & (4 << 1) = 8 & 8 = 8
    expect_eq(result.as<int>(), 8);
    
    // Verify with parentheses
    result = engine.execute("(8 & 4) << 1");     // Should be 0 << 1 = 0
    expect_eq(result.as<int>(), 0);
}

// Benchmark tests
JAI_BENCHMARK(compound_assignment_performance) {
    engine engine;
    
    engine.execute(R"(
        int sum = 0;
        for (int i = 0; i < 10000; ++i) {
            sum += i;
        }
    )");
}

JAI_BENCHMARK(increment_operator_performance) {
    engine engine;
    
    engine.execute(R"(
        int counter = 0;
        for (int i = 0; i < 10000; ++i) {
            counter++;
        }
    )");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()