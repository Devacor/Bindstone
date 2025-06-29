#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>
#include <sstream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(FunctionRegistrationClarity)

// Demonstrate variadic functions
JAI_TEST(variadic_print_function) {
    engine engine;
    
    // A print function that handles any number of arguments
    engine.add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
        std::stringstream ss;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) ss << " ";
            ss << args[i].to_string();
        }
        // In real code, this would print to console
        return script_value(ss.str());
    });
    
    script_value result1 = engine.execute("print();");
    expect_eq("", result1.as_string());
    
    script_value result2 = engine.execute("print(\"Hello\");");
    expect_eq("Hello", result2.as_string());
    
    script_value result3 = engine.execute("print(\"value:\", 42, true);");
    expect_eq("value: 42 true", result3.as_string());
}

// Demonstrate typed overloaded functions
JAI_TEST(typed_add_overloads) {
    engine engine;
    
    // Different overloads for different types
    engine.add_function("add", [](int a, int b) -> int {
        return a + b;
    });
    
    engine.add_function("add", [](float a, float b) -> float {
        return a + b;
    });
    
    engine.add_function("add", [](const std::string& a, const std::string& b) -> std::string {
        return a + b;
    });
    
    // Integer addition
    script_value result1 = engine.execute("add(5, 3);");
    expect_eq(8, result1.as_int());
    
    // script_float addition
    script_value result2 = engine.execute("add(5.5, 2.5);");
    expect_eq(8.0f, result2.as<float>());
    
    // script_string concatenation
    script_value result3 = engine.execute("add(\"Hello\", \" World\");");
    expect_eq("Hello World", result3.as_string());
}

// Show how variadic and typed can work together
JAI_TEST(mixed_variadic_and_typed) {
    engine engine;
    
    // Variadic max function that works with any number of arguments
    engine.add_variadic_function("max", [](const std::vector<script_value>& args) -> script_value {
        if (args.empty()) {
            throw runtime_error("max() requires at least one argument");
        }
        
        script_value maxVal = args[0];
        for (size_t i = 1; i < args.size(); ++i) {
            if ((args[i].is_int() || args[i].is_float()) && (maxVal.is_int() || maxVal.is_float())) {
                script_float current = args[i].is_int() ? static_cast<script_float>(args[i].as_int()) : args[i].as_float();
                script_float max = maxVal.is_int() ? static_cast<script_float>(maxVal.as_int()) : maxVal.as_float();
                if (current > max) {
                    maxVal = args[i];
                }
            }
        }
        return maxVal;
    });
    
    // Typed min function for exactly 2 arguments
    engine.add_function("min", [](int a, int b) -> int {
        return a < b ? a : b;
    });
    
    engine.add_function("min", [](float a, float b) -> float {
        return a < b ? a : b;
    });
    
    // Test variadic max
    script_value max1 = engine.execute("max(5);");
    expect_eq(5, max1.as_int());
    
    script_value max2 = engine.execute("max(3, 7, 2, 9, 1);");
    expect_eq(9, max2.as_int());
    
    // Test typed min
    script_value min1 = engine.execute("min(5, 3);");
    expect_eq(3, min1.as_int());
    
    script_value min2 = engine.execute("min(5.5, 2.5);");
    expect_eq(2.5f, min2.as<float>());
}

// Demonstrate optional parameters with variadic
JAI_TEST(optional_parameters_variadic) {
    engine engine;
    
    // Function with optional parameters
    engine.add_variadic_function("greet", [](const std::vector<script_value>& args) -> script_value {
        std::string name = args.size() > 0 ? args[0].as_string() : "World";
        std::string greeting = args.size() > 1 ? args[1].as_string() : "Hello";
        return script_value(greeting + ", " + name + "!");
    });
    
    script_value result1 = engine.execute("greet();");
    expect_eq("Hello, World!", result1.as_string());
    
    script_value result2 = engine.execute("greet(\"Alice\");");
    expect_eq("Hello, Alice!", result2.as_string());
    
    script_value result3 = engine.execute("greet(\"Bob\", \"Hi\");");
    expect_eq("Hi, Bob!", result3.as_string());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()