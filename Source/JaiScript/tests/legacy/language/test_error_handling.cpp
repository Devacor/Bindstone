#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <cmath>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ErrorHandling)

// Runtime Errors
JAI_TEST(undefined_variable) {
    engine engine;
    expect_throws<runtime_error>([&]() {
        engine.execute("undefinedVariable");
    });
}

JAI_TEST(division_by_zero_int) {
    engine engine;
    expect_throws<runtime_error>([&]() {
        engine.execute("1 / 0");
    });
}

JAI_TEST(division_by_zero_float) {
    engine engine;
    // Consistent with integer division by zero - should throw exception
    expect_throws<runtime_error>([&]() {
        engine.execute("1.0 / 0.0");
    });
}

JAI_TEST(undefined_function) {
    engine engine;
    expect_throws<runtime_error>([&]() {
        engine.execute("nonExistentFunction()");
    });
}

JAI_TEST(null_arithmetic) {
    engine engine;
    expect_throws<runtime_error>([&]() {
        engine.execute("null + 1");
    });
}

JAI_TEST(string_subtraction) {
    engine engine;
    expect_throws<runtime_error>([&]() {
        engine.execute("\"string\" - 1");
    });
}

JAI_TEST(array_out_of_bounds) {
    engine engine;
    expect_throws<runtime_error>([&]() {
        engine.execute("[1,2,3][10]");
    });
}

JAI_TEST(negative_array_index) {
    engine engine;
    expect_throws<runtime_error>([&]() {
        engine.execute("[1,2,3][-1]");
    });
}

JAI_TEST(map_invalid_key) {
    engine engine;
    script_value result = engine.execute(R"(
        var map = {{"one", 1}, {"two", 2}};
        map["three"]
    )");
    // Map lookup for non-existent key returns null
    expect_true(result.is_null());
}

JAI_TEST(type_mismatch_in_function) {
    engine engine;
    engine.add_function("takeInt", [](script_int x) { return x * 2; });
    expect_throws<runtime_error>([&]() {
        engine.execute("takeInt(\"not a number\")");
    });
}

// Syntax Errors
JAI_TEST(unmatched_paren) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("if (true");
    });
}

JAI_TEST(extra_paren) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("if true) {");
    });
}

JAI_TEST(incomplete_function) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("function");
    });
}

JAI_TEST(incomplete_var) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("var");
    });
}

JAI_TEST(incomplete_expression) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("1 +");
    });
}

JAI_TEST(unmatched_brace) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("function f() {");
    });
}

JAI_TEST(missing_value_in_assignment) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("var x = ;");
    });
}

JAI_TEST(invalid_operator_usage) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("++");
    });
}

JAI_TEST(two_expressions_without_operator) {
    engine engine;
    expect_throws<std::exception>([&]() {
        engine.execute("1 2");
    });
}

// Error Recovery
JAI_TEST(engine_state_after_error) {
    engine engine;
    engine.add_global("x", script_value(script_int(10)));
    
    // Cause an error
    try {
        engine.execute("undefinedVar + 1");
    } catch (...) {
        // Expected
    }
    
    // engine should still work after error
    script_value result = engine.execute("x * 2");
    expect_eq(result.as<script_int>(), 20);
}

JAI_TEST(multiple_errors_in_sequence) {
    engine engine;
    
    // Try multiple errors
    for (int i = 0; i < 3; i++) {
        try {
            engine.execute("invalid syntax @#$");
        } catch (...) {
            // Expected
        }
    }
    
    // engine should still work
    script_value result = engine.execute("100 + 23");
    expect_eq(result.as<script_int>(), 123);
}

// Edge Cases
JAI_TEST(empty_script) {
    engine engine;
    script_value result = engine.execute("");
    expect_true(result.is_null());
}

JAI_TEST(whitespace_only_script) {
    engine engine;
    script_value result = engine.execute("   \n\t\n   ");
    expect_true(result.is_null());
}

JAI_TEST(comment_only_script) {
    engine engine;
    script_value result = engine.execute("// Just a comment");
    expect_true(result.is_null());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()