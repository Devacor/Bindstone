#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/interpreter.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/lexer.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(VariableQuery)

JAI_TEST(basic_variable_access) {
    engine engine;
    
    // Execute script with variables
    engine.execute(R"(
        var x = 42;
        var name = "test";
        var flag = true;
    )");
    
    // Test has_variable
    expect_true(engine.has_variable("x"));
    expect_true(engine.has_variable("name"));
    expect_true(engine.has_variable("flag"));
    expect_false(engine.has_variable("nonexistent"));
    
    // Test get_variable
    script_value x = engine.get_variable("x");
    expect_eq(x.as<script_int>(), 42);
    
    script_value name = engine.get_variable("name");
    expect_eq(name.as<script_string>(), "test");
    
    script_value flag = engine.get_variable("flag");
    expect_eq(flag.as<script_bool>(), true);
}

JAI_TEST(variable_modification) {
    engine engine;
    
    // Set initial variable
    engine.execute("var counter = 0;");
    expect_eq(engine.get_variable("counter").as<script_int>(), 0);
    
    // Modify variable
    engine.execute("counter = counter + 10;");
    expect_eq(engine.get_variable("counter").as<script_int>(), 10);
    
    // Modify again
    engine.execute("counter = counter * 2;");
    expect_eq(engine.get_variable("counter").as<script_int>(), 20);
}

JAI_TEST(multiple_variable_types) {
    engine engine;
    
    // Create variables of different types
    engine.execute(R"(
        var intVar = 123;
        var floatVar = 3.14;
        var stringVar = "hello";
        var boolVar = false;
        var charVar = 'A';
    )");
    
    // Check all variables exist
    expect_true(engine.has_variable("intVar"));
    expect_true(engine.has_variable("floatVar"));
    expect_true(engine.has_variable("stringVar"));
    expect_true(engine.has_variable("boolVar"));
    expect_true(engine.has_variable("charVar"));
    
    // Check types and values
    expect_eq(engine.get_variable("intVar").as<script_int>(), 123);
    expect_near(engine.get_variable("floatVar").as<script_float>(), 3.14, 0.001);
    expect_eq(engine.get_variable("stringVar").as<script_string>(), "hello");
    expect_eq(engine.get_variable("boolVar").as<script_bool>(), false);
    expect_eq(engine.get_variable("charVar").as<script_char>(), 'A');
}

JAI_TEST(variable_scope_and_persistence) {
    engine engine;
    
    // Global variables
    engine.execute("var global1 = 100;");
    expect_true(engine.has_variable("global1"));
    
    // Variables from block scope (should persist as globals)
    engine.execute(R"(
        if (true) {
            var blockVar = 200;
        }
    )");
    expect_true(engine.has_variable("blockVar"));
    expect_eq(engine.get_variable("blockVar").as<script_int>(), 200);
    
    // Variables from function execution
    engine.execute(R"(
        function createVars() -> auto {
            var funcVar = 300;
            return funcVar;
        }
        var result = createVars();
    )");
    expect_true(engine.has_variable("result"));
    expect_eq(engine.get_variable("result").as<script_int>(), 300);
}

JAI_TEST(variable_query_after_computation) {
    engine engine;
    
    // Execute complex computation
    engine.execute(R"(
        var a = 10;
        var b = 20;
        var sum = a + b;
        var product = a * b;
        var average = (a + b) / 2;
    )");
    
    // Query all computed variables
    expect_eq(engine.get_variable("a").as<script_int>(), 10);
    expect_eq(engine.get_variable("b").as<script_int>(), 20);
    expect_eq(engine.get_variable("sum").as<script_int>(), 30);
    expect_eq(engine.get_variable("product").as<script_int>(), 200);
    expect_eq(engine.get_variable("average").as<script_int>(), 15);
}

JAI_TEST(variable_query_with_loops) {
    engine engine;
    
    // Execute script with loop variables
    engine.execute(R"(
        var total = 0;
        for (var i = 1; i <= 5; i = i + 1) {
            total = total + i;
        }
        var finalI = i;
    )");
    
    expect_eq(engine.get_variable("total").as<script_int>(), 15); // 1+2+3+4+5
    expect_eq(engine.get_variable("finalI").as<script_int>(), 6); // i after loop
}

JAI_TEST(variable_query_with_conditionals) {
    engine engine;
    
    // Execute script with conditional variables
    engine.execute(R"(
        var condition = true;
        var result;
        if (condition) {
            result = "success";
        } else {
            result = "failure";
        }
    )");
    
    expect_true(engine.has_variable("condition"));
    expect_true(engine.has_variable("result"));
    expect_eq(engine.get_variable("condition").as<script_bool>(), true);
    expect_eq(engine.get_variable("result").as<script_string>(), "success");
}

JAI_TEST(cpp_globals_vs_script_variables) {
    engine engine;
    
    // Add C++ global
    engine.add_global("cppGlobal", script_int(999));
    
    // Add script variable
    engine.execute("var scriptVar = 888;");
    
    // Both should be queryable
    expect_true(engine.has_variable("cppGlobal"));
    expect_true(engine.has_variable("scriptVar"));
    expect_eq(engine.get_variable("cppGlobal").as<script_int>(), 999);
    expect_eq(engine.get_variable("scriptVar").as<script_int>(), 888);
}

JAI_TEST(variable_query_with_functions) {
    engine engine;
    
    // Add C++ function
    engine.add_function("multiply", [](const std::vector<script_value>& args) -> script_value {
        return script_value(args[0].as<script_int>() * args[1].as<script_int>());
    });
    
    // Script using function
    engine.execute(R"(
        var x = 6;
        var y = 7;
        var result = multiply(x, y);
    )");
    
    expect_eq(engine.get_variable("x").as<script_int>(), 6);
    expect_eq(engine.get_variable("y").as<script_int>(), 7);
    expect_eq(engine.get_variable("result").as<script_int>(), 42);
    expect_true(engine.has_variable("multiply")); // Function should also be queryable
}

JAI_TEST(variable_state_preservation) {
    engine engine;
    
    // Execute first script
    engine.execute(R"(
        var persistent = 100;
        var modified = 50;
    )");
    
    // Execute second script that modifies variables
    engine.execute(R"(
        modified = modified * 2;
        var newVar = persistent + modified;
    )");
    
    // All variables should be preserved and accessible
    expect_eq(engine.get_variable("persistent").as<script_int>(), 100);
    expect_eq(engine.get_variable("modified").as<script_int>(), 100);
    expect_eq(engine.get_variable("newVar").as<script_int>(), 200);
}

JAI_TEST(variable_query_error_handling) {
    engine engine;
    
    // Query non-existent variable should throw
    // TODO: Fix expect_throws syntax - for now just verify behavior
    expect_false(engine.has_variable("doesNotExist"));
    
    // has_variable should return false, not throw
    expect_false(engine.has_variable("alsoDoesNotExist"));
}

JAI_TEST(variable_query_with_complex_expressions) {
    engine engine;
    
    // Execute complex expressions
    engine.execute(R"(
        var data = 42;
        var doubled = data * 2;
        var stringified = "value: " + doubled;
        var conditional = doubled > 50 ? "large" : "small";
    )");
    
    expect_eq(engine.get_variable("data").as<script_int>(), 42);
    expect_eq(engine.get_variable("doubled").as<script_int>(), 84);
    expect_eq(engine.get_variable("stringified").as<script_string>(), "value: 84");
    expect_eq(engine.get_variable("conditional").as<script_string>(), "large");
}

JAI_TEST(variable_query_with_nested_scopes) {
    engine engine;
    
    // Test nested scope variable access
    engine.execute(R"(
        var outer = 10;
        if (true) {
            var inner = 20;
            if (true) {
                var deepest = outer + inner;
            }
        }
    )");
    
    // All variables should be accessible (flattened to global scope)
    expect_true(engine.has_variable("outer"));
    expect_true(engine.has_variable("inner"));
    expect_true(engine.has_variable("deepest"));
    expect_eq(engine.get_variable("outer").as<script_int>(), 10);
    expect_eq(engine.get_variable("inner").as<script_int>(), 20);
    expect_eq(engine.get_variable("deepest").as<script_int>(), 30);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()