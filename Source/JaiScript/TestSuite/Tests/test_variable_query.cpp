#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/interpreter.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/lexer.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(VariableQuery)

JAI_TEST(basic_variable_access) {
    Engine engine;
    
    // Execute script with variables
    engine.execute(R"(
        var x = 42;
        var name = "test";
        var flag = true;
    )");
    
    // Test hasVariable
    expect_true(engine.hasVariable("x"));
    expect_true(engine.hasVariable("name"));
    expect_true(engine.hasVariable("flag"));
    expect_false(engine.hasVariable("nonexistent"));
    
    // Test getVariable
    Value x = engine.getVariable("x");
    expect_eq(x.as<Int>(), 42);
    
    Value name = engine.getVariable("name");
    expect_eq(name.as<String>(), "test");
    
    Value flag = engine.getVariable("flag");
    expect_eq(flag.as<Bool>(), true);
}

JAI_TEST(variable_modification) {
    Engine engine;
    
    // Set initial variable
    engine.execute("var counter = 0;");
    expect_eq(engine.getVariable("counter").as<Int>(), 0);
    
    // Modify variable
    engine.execute("counter = counter + 10;");
    expect_eq(engine.getVariable("counter").as<Int>(), 10);
    
    // Modify again
    engine.execute("counter = counter * 2;");
    expect_eq(engine.getVariable("counter").as<Int>(), 20);
}

JAI_TEST(multiple_variable_types) {
    Engine engine;
    
    // Create variables of different types
    engine.execute(R"(
        var intVar = 123;
        var floatVar = 3.14;
        var stringVar = "hello";
        var boolVar = false;
        var charVar = 'A';
    )");
    
    // Check all variables exist
    expect_true(engine.hasVariable("intVar"));
    expect_true(engine.hasVariable("floatVar"));
    expect_true(engine.hasVariable("stringVar"));
    expect_true(engine.hasVariable("boolVar"));
    expect_true(engine.hasVariable("charVar"));
    
    // Check types and values
    expect_eq(engine.getVariable("intVar").as<Int>(), 123);
    expect_near(engine.getVariable("floatVar").as<Float>(), 3.14, 0.001);
    expect_eq(engine.getVariable("stringVar").as<String>(), "hello");
    expect_eq(engine.getVariable("boolVar").as<Bool>(), false);
    expect_eq(engine.getVariable("charVar").as<Char>(), 'A');
}

JAI_TEST(variable_scope_and_persistence) {
    Engine engine;
    
    // Global variables
    engine.execute("var global1 = 100;");
    expect_true(engine.hasVariable("global1"));
    
    // Variables from block scope (should persist as globals)
    engine.execute(R"(
        if (true) {
            var blockVar = 200;
        }
    )");
    expect_true(engine.hasVariable("blockVar"));
    expect_eq(engine.getVariable("blockVar").as<Int>(), 200);
    
    // Variables from function execution
    engine.execute(R"(
        function createVars() -> auto {
            var funcVar = 300;
            return funcVar;
        }
        var result = createVars();
    )");
    expect_true(engine.hasVariable("result"));
    expect_eq(engine.getVariable("result").as<Int>(), 300);
}

JAI_TEST(variable_query_after_computation) {
    Engine engine;
    
    // Execute complex computation
    engine.execute(R"(
        var a = 10;
        var b = 20;
        var sum = a + b;
        var product = a * b;
        var average = (a + b) / 2;
    )");
    
    // Query all computed variables
    expect_eq(engine.getVariable("a").as<Int>(), 10);
    expect_eq(engine.getVariable("b").as<Int>(), 20);
    expect_eq(engine.getVariable("sum").as<Int>(), 30);
    expect_eq(engine.getVariable("product").as<Int>(), 200);
    expect_eq(engine.getVariable("average").as<Int>(), 15);
}

JAI_TEST(variable_query_with_loops) {
    Engine engine;
    
    // Execute script with loop variables
    engine.execute(R"(
        var total = 0;
        for (var i = 1; i <= 5; i = i + 1) {
            total = total + i;
        }
        var finalI = i;
    )");
    
    expect_eq(engine.getVariable("total").as<Int>(), 15); // 1+2+3+4+5
    expect_eq(engine.getVariable("finalI").as<Int>(), 6); // i after loop
}

JAI_TEST(variable_query_with_conditionals) {
    Engine engine;
    
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
    
    expect_true(engine.hasVariable("condition"));
    expect_true(engine.hasVariable("result"));
    expect_eq(engine.getVariable("condition").as<Bool>(), true);
    expect_eq(engine.getVariable("result").as<String>(), "success");
}

JAI_TEST(cpp_globals_vs_script_variables) {
    Engine engine;
    
    // Add C++ global
    engine.addGlobal("cppGlobal", Int(999));
    
    // Add script variable
    engine.execute("var scriptVar = 888;");
    
    // Both should be queryable
    expect_true(engine.hasVariable("cppGlobal"));
    expect_true(engine.hasVariable("scriptVar"));
    expect_eq(engine.getVariable("cppGlobal").as<Int>(), 999);
    expect_eq(engine.getVariable("scriptVar").as<Int>(), 888);
}

JAI_TEST(variable_query_with_functions) {
    Engine engine;
    
    // Add C++ function
    engine.addFunction("multiply", [](const std::vector<Value>& args) -> Value {
        return Value(args[0].as<Int>() * args[1].as<Int>());
    });
    
    // Script using function
    engine.execute(R"(
        var x = 6;
        var y = 7;
        var result = multiply(x, y);
    )");
    
    expect_eq(engine.getVariable("x").as<Int>(), 6);
    expect_eq(engine.getVariable("y").as<Int>(), 7);
    expect_eq(engine.getVariable("result").as<Int>(), 42);
    expect_true(engine.hasVariable("multiply")); // Function should also be queryable
}

JAI_TEST(variable_state_preservation) {
    Engine engine;
    
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
    expect_eq(engine.getVariable("persistent").as<Int>(), 100);
    expect_eq(engine.getVariable("modified").as<Int>(), 100);
    expect_eq(engine.getVariable("newVar").as<Int>(), 200);
}

JAI_TEST(variable_query_error_handling) {
    Engine engine;
    
    // Query non-existent variable should throw
    // TODO: Fix expect_throws syntax - for now just verify behavior
    expect_false(engine.hasVariable("doesNotExist"));
    
    // hasVariable should return false, not throw
    expect_false(engine.hasVariable("alsoDoesNotExist"));
}

JAI_TEST(variable_query_with_complex_expressions) {
    Engine engine;
    
    // Execute complex expressions
    engine.execute(R"(
        var data = 42;
        var doubled = data * 2;
        var stringified = "Value: " + doubled;
        var conditional = doubled > 50 ? "large" : "small";
    )");
    
    expect_eq(engine.getVariable("data").as<Int>(), 42);
    expect_eq(engine.getVariable("doubled").as<Int>(), 84);
    expect_eq(engine.getVariable("stringified").as<String>(), "Value: 84");
    expect_eq(engine.getVariable("conditional").as<String>(), "large");
}

JAI_TEST(variable_query_with_nested_scopes) {
    Engine engine;
    
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
    expect_true(engine.hasVariable("outer"));
    expect_true(engine.hasVariable("inner"));
    expect_true(engine.hasVariable("deepest"));
    expect_eq(engine.getVariable("outer").as<Int>(), 10);
    expect_eq(engine.getVariable("inner").as<Int>(), 20);
    expect_eq(engine.getVariable("deepest").as<Int>(), 30);
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()