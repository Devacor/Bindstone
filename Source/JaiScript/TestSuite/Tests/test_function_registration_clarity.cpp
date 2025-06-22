#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <sstream>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(FunctionRegistrationClarity)

// Demonstrate variadic functions
JAI_TEST(variadic_print_function) {
    Engine engine;
    
    // A print function that handles any number of arguments
    engine.addVariadicFunction("print", [](const std::vector<Value>& args) -> Value {
        std::stringstream ss;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) ss << " ";
            ss << args[i].toString();
        }
        // In real code, this would print to console
        return Value(ss.str());
    });
    
    Value result1 = engine.execute("print();");
    expect_eq("", result1.asString());
    
    Value result2 = engine.execute("print(\"Hello\");");
    expect_eq("Hello", result2.asString());
    
    Value result3 = engine.execute("print(\"Value:\", 42, true);");
    expect_eq("Value: 42 true", result3.asString());
}

// Demonstrate typed overloaded functions
JAI_TEST(typed_add_overloads) {
    Engine engine;
    
    // Different overloads for different types
    engine.addFunction("add", [](int a, int b) -> int {
        return a + b;
    });
    
    engine.addFunction("add", [](float a, float b) -> float {
        return a + b;
    });
    
    engine.addFunction("add", [](const std::string& a, const std::string& b) -> std::string {
        return a + b;
    });
    
    // Integer addition
    Value result1 = engine.execute("add(5, 3);");
    expect_eq(8, result1.asInt());
    
    // Float addition
    Value result2 = engine.execute("add(5.5, 2.5);");
    expect_eq(8.0f, result2.as<float>());
    
    // String concatenation
    Value result3 = engine.execute("add(\"Hello\", \" World\");");
    expect_eq("Hello World", result3.asString());
}

// Show how variadic and typed can work together
JAI_TEST(mixed_variadic_and_typed) {
    Engine engine;
    
    // Variadic max function that works with any number of arguments
    engine.addVariadicFunction("max", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            throw RuntimeError("max() requires at least one argument");
        }
        
        Value maxVal = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            if ((args[i].isInt() || args[i].isFloat()) && (maxVal.isInt() || maxVal.isFloat())) {
                Float current = args[i].isInt() ? static_cast<Float>(args[i].asInt()) : args[i].asFloat();
                Float max = maxVal.isInt() ? static_cast<Float>(maxVal.asInt()) : maxVal.asFloat();
                if (current > max) {
                    maxVal = args[i];
                }
            }
        }
        return maxVal;
    });
    
    // Typed min function for exactly 2 arguments
    engine.addFunction("min", [](int a, int b) -> int {
        return a < b ? a : b;
    });
    
    engine.addFunction("min", [](float a, float b) -> float {
        return a < b ? a : b;
    });
    
    // Test variadic max
    Value max1 = engine.execute("max(5);");
    expect_eq(5, max1.asInt());
    
    Value max2 = engine.execute("max(3, 7, 2, 9, 1);");
    expect_eq(9, max2.asInt());
    
    // Test typed min
    Value min1 = engine.execute("min(5, 3);");
    expect_eq(3, min1.asInt());
    
    Value min2 = engine.execute("min(5.5, 2.5);");
    expect_eq(2.5f, min2.as<float>());
}

// Demonstrate optional parameters with variadic
JAI_TEST(optional_parameters_variadic) {
    Engine engine;
    
    // Function with optional parameters
    engine.addVariadicFunction("greet", [](const std::vector<Value>& args) -> Value {
        std::string name = args.size() > 0 ? args[0].asString() : "World";
        std::string greeting = args.size() > 1 ? args[1].asString() : "Hello";
        return Value(greeting + ", " + name + "!");
    });
    
    Value result1 = engine.execute("greet();");
    expect_eq("Hello, World!", result1.asString());
    
    Value result2 = engine.execute("greet(\"Alice\");");
    expect_eq("Hello, Alice!", result2.asString());
    
    Value result3 = engine.execute("greet(\"Bob\", \"Hi\");");
    expect_eq("Hi, Bob!", result3.asString());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()