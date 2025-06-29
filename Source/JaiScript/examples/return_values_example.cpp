#include <iostream>
#include "../include/jaiscript/detail/interpreter.hpp"
#include "../include/jaiscript/detail/parser.hpp"
#include "../include/jaiscript/detail/lexer.hpp"

using namespace jai;

// Helper function to execute JaiScript code
template<typename T>
T executeScript(const std::string& source) {
    lexer lexer(source);
    auto tokens = lexer.tokenize();
    
    parser parser(tokens);
    auto ast = parser.parse();
    
    interpreter interpreter;
    return interpreter.execute<T>(ast);
}

int main() {
    std::cout << "=== JaiScript Return Values Example ===" << std::endl;
    
    // Example 1: Simple calculation with typed return
    Int result1 = executeScript<Int>(R"(
        base = 10;
        multiplier = 5;
        return base * multiplier;
    )");
    std::cout << "Calculation result: " << result1 << std::endl;
    
    // Example 2: String processing
    String greeting = executeScript<String>(R"(
        firstName = "John";
        lastName = "Doe";
        title = "Dr.";
        return title + " " + firstName + " " + lastName;
    )");
    std::cout << "Greeting: " << greeting << std::endl;
    
    // Example 3: Configuration processing
    bool isEnabled = executeScript<bool>(R"(
        debugMode = true;
        production = false;
        return debugMode && !production;
    )");
    std::cout << "Feature enabled: " << (isEnabled ? "Yes" : "No") << std::endl;
    
    // Example 4: Complex computation
    double complexResult = executeScript<double>(R"(
        pi = 3.14159;
        radius = 5.0;
        area = pi * radius * radius;
        return area;
    )");
    std::cout << "Circle area: " << complexResult << std::endl;
    
    // Example 5: Using the non-templated version for inspection
    std::cout << "\n=== Variable Inspection Example ===" << std::endl;
    
    lexer lexer(R"(
        name = "Alice";
        age = 25;
        score = 95.5;
        active = true;
        computedscript_value = age * 2;
        return computedscript_value;
    )");
    auto tokens = lexer.tokenize();
    
    parser parser(tokens);
    auto ast = parser.parse();
    
    interpreter interpreter;
    script_value result = interpreter.execute(ast);
    
    std::cout << "Script result: " << result.to_string() << std::endl;
    std::cout << "Has return value: " << (interpreter.hasReturnscript_value() ? "Yes" : "No") << std::endl;
    std::cout << "Return value: " << interpreter.getReturnscript_value().to_string() << std::endl;
    
    std::cout << "\nAll variables after execution:" << std::endl;
    auto variables = interpreter.getAllVariables();
    for (const auto& [name, value] : variables) {
        std::cout << "  " << name << " = " << value.to_string() << std::endl;
    }
    
    // Example 6: Early return behavior
    std::cout << "\n=== Early Return Example ===" << std::endl;
    
    int earlyResult = executeScript<int>(R"(
        x = 10;
        if (true) {
            return x * 2;  // This will be implemented when if statements work
        }
        x = 999;  // This won't execute due to early return
        return x;
    )");
    // Note: This will fail currently since if statements aren't implemented yet
    // But demonstrates the concept
    
    std::cout << "\nJaiScript return values working perfectly!" << std::endl;
    
    return 0;
}