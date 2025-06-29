#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>
#include <jaiscript/jvm/virtual_machine.hpp>
#include <jaiscript/jvm/bytecode.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>

using namespace jai;
using namespace jai::jvm;
using namespace jai::test;

// Helper function to compile and execute JaiScript source using VM
script_value compile_and_execute_vm(const std::string& source) {
    lexer lex(source);
    auto tokens = lex.tokenize();
    
    parser parse(tokens);
    auto declarations = parse.parse();
    
    auto backend = create_vm_backend();
    return backend->execute(declarations);
}

JAI_TEST_SUITE(VMOperators)

// Arithmetic Operators
JAI_TEST(vm_arithmetic_operators) {
    // Basic arithmetic
    expect_eq(compile_and_execute_vm("10 + 5;").as<script_int>(), 15);
    expect_eq(compile_and_execute_vm("10 - 5;").as<script_int>(), 5);
    expect_eq(compile_and_execute_vm("10 * 5;").as<script_int>(), 50);
    expect_eq(compile_and_execute_vm("10 / 5;").as<script_int>(), 2);
    expect_eq(compile_and_execute_vm("10 % 3;").as<script_int>(), 1);
    
    // Unary negation
    expect_eq(compile_and_execute_vm("-5;").as<script_int>(), -5);
    expect_eq(compile_and_execute_vm("-(-10);").as<script_int>(), 10);
    
    // Float arithmetic
    expect_near(compile_and_execute_vm("10.5 + 5.5;").as<script_float>(), 16.0, 0.001);
    expect_near(compile_and_execute_vm("10.5 - 5.5;").as<script_float>(), 5.0, 0.001);
    expect_near(compile_and_execute_vm("2.5 * 4.0;").as<script_float>(), 10.0, 0.001);
    expect_near(compile_and_execute_vm("10.0 / 4.0;").as<script_float>(), 2.5, 0.001);
}

// Compound Assignment Operators
JAI_TEST(vm_compound_assignment_operators) {
    // Integer compound assignments
    auto result = compile_and_execute_vm(R"(
        var x = 10;
        x += 5;
        x;
    )");
    expect_eq(result.as<script_int>(), 15);
    
    result = compile_and_execute_vm(R"(
        var x = 10;
        x -= 3;
        x;
    )");
    expect_eq(result.as<script_int>(), 7);
    
    result = compile_and_execute_vm(R"(
        var x = 10;
        x *= 3;
        x;
    )");
    expect_eq(result.as<script_int>(), 30);
    
    result = compile_and_execute_vm(R"(
        var x = 20;
        x /= 4;
        x;
    )");
    expect_eq(result.as<script_int>(), 5);
    
    // Float compound assignments
    result = compile_and_execute_vm(R"(
        var x = 10.5;
        x += 5.5;
        x;
    )");
    expect_near(result.as<script_float>(), 16.0, 0.001);
}

// Increment/Decrement Operators
JAI_TEST(vm_increment_decrement_operators) {
    // Prefix increment
    auto result = compile_and_execute_vm(R"(
        var x = 5;
        ++x;
    )");
    expect_eq(result.as<script_int>(), 6);
    
    // Postfix increment
    result = compile_and_execute_vm(R"(
        var x = 5;
        x++;
    )");
    expect_eq(result.as<script_int>(), 5); // Returns old value
    
    // Verify the value was incremented
    result = compile_and_execute_vm(R"(
        var x = 5;
        x++;
        x;
    )");
    expect_eq(result.as<script_int>(), 6);
    
    // Prefix decrement
    result = compile_and_execute_vm(R"(
        var x = 5;
        --x;
    )");
    expect_eq(result.as<script_int>(), 4);
    
    // Postfix decrement
    result = compile_and_execute_vm(R"(
        var x = 5;
        x--;
    )");
    expect_eq(result.as<script_int>(), 5); // Returns old value
}

// Comparison Operators
JAI_TEST(vm_comparison_operators) {
    // Integer comparisons
    expect_true(compile_and_execute_vm("5 < 10;").as<script_bool>());
    expect_false(compile_and_execute_vm("10 < 5;").as<script_bool>());
    expect_true(compile_and_execute_vm("5 <= 5;").as<script_bool>());
    expect_true(compile_and_execute_vm("10 > 5;").as<script_bool>());
    expect_false(compile_and_execute_vm("5 > 10;").as<script_bool>());
    expect_true(compile_and_execute_vm("5 >= 5;").as<script_bool>());
    expect_true(compile_and_execute_vm("5 == 5;").as<script_bool>());
    expect_false(compile_and_execute_vm("5 == 10;").as<script_bool>());
    expect_true(compile_and_execute_vm("5 != 10;").as<script_bool>());
    expect_false(compile_and_execute_vm("5 != 5;").as<script_bool>());
    
    // Float comparisons
    expect_true(compile_and_execute_vm("5.5 < 10.5;").as<script_bool>());
    expect_true(compile_and_execute_vm("5.5 <= 5.5;").as<script_bool>());
    expect_true(compile_and_execute_vm("10.5 > 5.5;").as<script_bool>());
    expect_true(compile_and_execute_vm("5.5 >= 5.5;").as<script_bool>());
    expect_true(compile_and_execute_vm("5.5 == 5.5;").as<script_bool>());
    expect_true(compile_and_execute_vm("5.5 != 10.5;").as<script_bool>());
}

// Spaceship Operator (Three-way comparison)
JAI_TEST(vm_spaceship_operator) {
    // Integer spaceship
    expect_eq(compile_and_execute_vm("5 <=> 10;").as<script_int>(), -1);
    expect_eq(compile_and_execute_vm("10 <=> 5;").as<script_int>(), 1);
    expect_eq(compile_and_execute_vm("5 <=> 5;").as<script_int>(), 0);
    
    // Float spaceship
    expect_eq(compile_and_execute_vm("5.5 <=> 10.5;").as<script_int>(), -1);
    expect_eq(compile_and_execute_vm("10.5 <=> 5.5;").as<script_int>(), 1);
    expect_eq(compile_and_execute_vm("5.5 <=> 5.5;").as<script_int>(), 0);
}

// Logical Operators
JAI_TEST(vm_logical_operators) {
    // AND operator
    expect_true(compile_and_execute_vm("true && true;").as<script_bool>());
    expect_false(compile_and_execute_vm("true && false;").as<script_bool>());
    expect_false(compile_and_execute_vm("false && true;").as<script_bool>());
    expect_false(compile_and_execute_vm("false && false;").as<script_bool>());
    
    // OR operator
    expect_true(compile_and_execute_vm("true || true;").as<script_bool>());
    expect_true(compile_and_execute_vm("true || false;").as<script_bool>());
    expect_true(compile_and_execute_vm("false || true;").as<script_bool>());
    expect_false(compile_and_execute_vm("false || false;").as<script_bool>());
    
    // NOT operator
    expect_false(compile_and_execute_vm("!true;").as<script_bool>());
    expect_true(compile_and_execute_vm("!false;").as<script_bool>());
    
    // Short-circuit evaluation
    auto result = compile_and_execute_vm(R"(
        var x = 0;
        false && (x = 10) > 0;
        x;
    )");
    expect_eq(result.as<script_int>(), 0); // x should not be modified
    
    result = compile_and_execute_vm(R"(
        var x = 0;
        true || (x = 10) > 0;
        x;
    )");
    expect_eq(result.as<script_int>(), 0); // x should not be modified
}

// Bitwise Operators
JAI_TEST(vm_bitwise_operators) {
    // Bitwise AND
    expect_eq(compile_and_execute_vm("12 & 10;").as<script_int>(), 8);  // 1100 & 1010 = 1000
    expect_eq(compile_and_execute_vm("15 & 7;").as<script_int>(), 7);   // 1111 & 0111 = 0111
    
    // Bitwise OR (when implemented)
    // expect_eq(compile_and_execute_vm("12 | 10;").as<script_int>(), 14); // 1100 | 1010 = 1110
    
    // Bitwise XOR (when implemented)
    // expect_eq(compile_and_execute_vm("12 ^ 10;").as<script_int>(), 6);  // 1100 ^ 1010 = 0110
    
    // Bitwise NOT
    expect_eq(compile_and_execute_vm("~0;").as<script_int>(), -1);
    expect_eq(compile_and_execute_vm("~1;").as<script_int>(), -2);
    
    // Left shift
    expect_eq(compile_and_execute_vm("1 << 3;").as<script_int>(), 8);
    expect_eq(compile_and_execute_vm("5 << 2;").as<script_int>(), 20);
    
    // Right shift
    expect_eq(compile_and_execute_vm("8 >> 3;").as<script_int>(), 1);
    expect_eq(compile_and_execute_vm("20 >> 2;").as<script_int>(), 5);
}

// Mixed operator precedence
JAI_TEST(vm_operator_precedence) {
    // Arithmetic precedence
    expect_eq(compile_and_execute_vm("2 + 3 * 4;").as<script_int>(), 14);
    expect_eq(compile_and_execute_vm("(2 + 3) * 4;").as<script_int>(), 20);
    expect_eq(compile_and_execute_vm("10 - 2 * 3;").as<script_int>(), 4);
    expect_eq(compile_and_execute_vm("10 / 2 + 3;").as<script_int>(), 8);
    
    // Comparison and logical precedence
    expect_true(compile_and_execute_vm("5 < 10 && 10 > 5;").as<script_bool>());
    expect_false(compile_and_execute_vm("5 > 10 || 10 < 5;").as<script_bool>());
    
    // Bitwise and arithmetic precedence
    expect_eq(compile_and_execute_vm("2 + 3 << 1;").as<script_int>(), 10); // (2 + 3) << 1
    expect_eq(compile_and_execute_vm("8 >> 1 + 1;").as<script_int>(), 2);  // 8 >> (1 + 1)
}

// Type conversion operators
JAI_TEST(vm_type_conversion_operators) {
    // Implicit conversions in mixed operations
    expect_near(compile_and_execute_vm("5 + 3.5;").as<script_float>(), 8.5, 0.001);
    expect_near(compile_and_execute_vm("10.0 / 4;").as<script_float>(), 2.5, 0.001);
    
    // String concatenation with conversion
    auto result = compile_and_execute_vm(R"("Value: " + 42;)");
    expect_eq(result.as<script_string>(), "Value: 42");
    
    result = compile_and_execute_vm(R"("Result: " + (10 > 5);)");
    expect_eq(result.as<script_string>(), "Result: true");
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()