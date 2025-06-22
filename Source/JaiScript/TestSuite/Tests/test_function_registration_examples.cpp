#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(FunctionRegistrationExamples)

JAI_TEST(variadic_function_explicit) {
    Engine engine;
    
    // Use the explicit addVariadicFunction for ScriptFunction types
    engine.addVariadicFunction("Point", [](const std::vector<Value>& args) -> Value {
        return Value(static_cast<Int>(42));
    });
    
    Value result = engine.execute("Point();");
    expect_eq(static_cast<Int>(42), result.asInt());
}

JAI_TEST(template_overloaded_functions) {
    Engine engine;
    
    // Register functions with specific signatures for the template system
    engine.addFunction("MakePoint", []() -> int { 
        return 42; 
    });
    
    engine.addFunction("MakePoint", [](int x) -> int { 
        return x * 2; 
    });
    
    engine.addFunction("MakePoint", [](int x, int y) -> int { 
        return x + y; 
    });
    
    Value result1 = engine.execute("MakePoint();");
    expect_eq(42, result1.asInt());
    
    Value result2 = engine.execute("MakePoint(5);");
    expect_eq(10, result2.asInt());
    
    Value result3 = engine.execute("MakePoint(3, 7);");
    expect_eq(10, result3.asInt());
}

JAI_TEST(variadic_function_with_arity_zero) {
    Engine engine;
    
    // Register with arity 0 to indicate it handles any number of args
    engine.addFunctionWithArity("FlexPoint", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            return Value(static_cast<Int>(0));
        } else if (args.size() == 1) {
            return Value(args[0].asInt() * 10);
        } else {
            return Value(args[0].asInt() + args[1].asInt());
        }
    }, 0); // arity 0 = wildcard (accepts any number of args)
    
    Value result1 = engine.execute("FlexPoint();");
    expect_eq(static_cast<Int>(0), result1.asInt());
    
    Value result2 = engine.execute("FlexPoint(5);");
    expect_eq(static_cast<Int>(50), result2.asInt());
    
    Value result3 = engine.execute("FlexPoint(3, 7);");
    expect_eq(static_cast<Int>(10), result3.asInt());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()