#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(ClassInstantiation)

JAI_TEST(function_call_syntax) {
    Engine engine;
    
    // Use addVariadicFunction for functions that take std::vector<Value>&
    engine.addVariadicFunction("Point", [](const std::vector<Value>& args) -> Value {
        return Value(static_cast<Int>(42)); // Return a dummy value
    });
    
    Value result = engine.execute("Point();");
    expect_eq(static_cast<Int>(42), result.asInt());
}

JAI_TEST(variable_assignment) {
    Engine engine;
    
    engine.addVariadicFunction("Point", [](const std::vector<Value>& args) -> Value {
        return Value(static_cast<Int>(42));
    });
    
    Value result = engine.execute("var p = Point(); p;");
    expect_eq(static_cast<Int>(42), result.asInt());
}

JAI_TEST(auto_variable) {
    Engine engine;
    
    engine.addVariadicFunction("Point", [](const std::vector<Value>& args) -> Value {
        return Value(static_cast<Int>(42));
    });
    
    Value result = engine.execute("auto p2 = Point(); p2;");
    expect_eq(static_cast<Int>(42), result.asInt());
}

JAI_TEST(return_statement) {
    Engine engine;
    
    engine.addVariadicFunction("Point", [](const std::vector<Value>& args) -> Value {
        return Value(static_cast<Int>(42));
    });
    
    Value result = engine.execute("auto p3 = Point(); return p3;");
    expect_eq(static_cast<Int>(42), result.asInt());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()