#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(FunctionRegistrationExamples)

JAI_TEST(variadic_function_explicit) {
    engine engine;
    
    // Use the explicit add_variadic_function for script_function types
    engine.add_variadic_function("Point", [](const std::vector<script_value>& args) -> script_value {
        return script_value(static_cast<script_int>(42));
    });
    
    script_value result = engine.execute("Point();");
    expect_eq(static_cast<script_int>(42), result.as_int());
}

JAI_TEST(template_overloaded_functions) {
    engine engine;
    
    // Register functions with specific signatures for the template system
    engine.add_function("MakePoint", []() -> int { 
        return 42; 
    });
    
    engine.add_function("MakePoint", [](int x) -> int { 
        return x * 2; 
    });
    
    engine.add_function("MakePoint", [](int x, int y) -> int { 
        return x + y; 
    });
    
    script_value result1 = engine.execute("MakePoint();");
    expect_eq(42, result1.as_int());
    
    script_value result2 = engine.execute("MakePoint(5);");
    expect_eq(10, result2.as_int());
    
    script_value result3 = engine.execute("MakePoint(3, 7);");
    expect_eq(10, result3.as_int());
}

JAI_TEST(variadic_function_with_arity_zero) {
    engine engine;
    
    // Register with arity 0 to indicate it handles any number of args
    engine.add_functionWithArity("FlexPoint", [](const std::vector<script_value>& args) -> script_value {
        if (args.empty()) {
            return script_value(static_cast<script_int>(0));
        } else if (args.size() == 1) {
            return script_value(args[0].as_int() * 10);
        } else {
            return script_value(args[0].as_int() + args[1].as_int());
        }
    }, 0); // arity 0 = wildcard (accepts any number of args)
    
    script_value result1 = engine.execute("FlexPoint();");
    expect_eq(static_cast<script_int>(0), result1.as_int());
    
    script_value result2 = engine.execute("FlexPoint(5);");
    expect_eq(static_cast<script_int>(50), result2.as_int());
    
    script_value result3 = engine.execute("FlexPoint(3, 7);");
    expect_eq(static_cast<script_int>(10), result3.as_int());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()