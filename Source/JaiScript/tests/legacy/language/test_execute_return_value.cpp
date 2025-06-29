#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ExecuteReturnValue)

JAI_TEST(SingleExpressionReturnsValue) {
    engine engine;
    script_value result = engine.execute("42");
    expect_false(result.is_null());
    expect_eq(result.as<int>(), 42);
}

JAI_TEST(VariableDeclarationReturnsNull) {
    engine engine;
    script_value result = engine.execute("var x = 10;");
    expect_true(result.is_null());
}

JAI_TEST(MultipleStatementsWithSemicolons) {
    engine engine;
    script_value result = engine.execute("1 + 1; 2 + 2");
    expect_false(result.is_null());
    expect_eq(result.as<int>(), 4);
}

JAI_TEST(VariableDeclarationFollowedByExpression) {
    engine engine;
    script_value result = engine.execute(R"(
        var x = 10;
        x * 2
    )");
    expect_false(result.is_null());
    expect_eq(result.as<int>(), 20);
}

JAI_TEST(MultipleVariableDeclarationsReturnNull) {
    engine engine;
    script_value result = engine.execute(R"(
        var a = 5;
        var b = 10;
        var c = a + b;
    )");
    expect_true(result.is_null());
    expect_true(engine.has_variable("c"));
    expect_eq(engine.get_variable("c").as<int>(), 15);
}

JAI_TEST(MapCreationAndLookup) {
    engine engine;
    
    // First test just map literal creation
    script_value map_literal = engine.execute(R"(
        {{"one", 1}, {"two", 2}}
    )");
    
    std::cout << "Map literal: is_null=" << map_literal.is_null() << ", is_map=" << map_literal.is_map() << std::endl;
    
    // Test variable assignment + return
    script_value map_result = engine.execute(R"(
        var map = {{"one", 1}, {"two", 2}};
        map
    )");
    
    std::cout << "Map creation result: is_null=" << map_result.is_null() << ", is_map=" << map_result.is_map() << std::endl;
    
    // Check if variable was created correctly
    if (engine.has_variable("map")) {
        script_value map_var = engine.get_variable("map");
        std::cout << "Map variable: is_null=" << map_var.is_null() << ", is_map=" << map_var.is_map() << std::endl;
        
        if (map_var.is_map()) {
            const auto& map_data = map_var.as_map();
            std::cout << "Map size: " << map_data.size() << std::endl;
            
            // Try to access directly
            script_value key("one");
            auto it = map_data.find(key);
            std::cout << "Direct find result: " << (it != map_data.end() ? "found" : "not found") << std::endl;
        }
    }
    
    script_value result = engine.execute(R"(
        var map = {{"one", 1}, {"two", 2}};
        map["one"]
    )");
    
    std::cout << "Final result: is_null=" << result.is_null() << std::endl;
    if (!result.is_null()) {
        std::cout << "Result value: " << result.as<int>() << std::endl;
    }
    
    // This currently fails - returns null instead of 1
    expect_false(result.is_null());
    expect_eq(result.as<int>(), 1);
}

JAI_TEST(MapCreationAndAssignment) {
    engine engine;
    script_value result = engine.execute(R"(
        var map = {{"one", 1}, {"two", 2}};
        var m1 = map["one"];
    )");
    expect_true(result.is_null());
    expect_true(engine.has_variable("m1"));
    expect_eq(engine.get_variable("m1").as<int>(), 1);
}

JAI_TEST(ExplicitReturnStatement) {
    engine engine;
    script_value result = engine.execute(R"(
        var x = 100;
        return x * 2;
    )");
    expect_false(result.is_null());
    expect_eq(result.as<int>(), 200);
}

JAI_TEST(ExpressionWithoutSemicolonAtEnd) {
    engine engine;
    script_value result = engine.execute(R"(
        var x = 5;
        x + 10
    )");
    expect_false(result.is_null());
    expect_eq(result.as<int>(), 15);
}

JAI_TEST(BlockStatementReturnsNull) {
    engine engine;
    script_value result = engine.execute(R"(
        {
            var x = 5;
            x * 2;
        }
    )");
    expect_true(result.is_null());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()