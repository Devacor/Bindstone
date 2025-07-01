#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    std::cout << "JSON Building Strategies:\n" << std::endl;
    
    // Strategy 1: Escape the braces
    std::cout << "1. Escaping braces: ";
    engine.execute(R"(
        auto key = "name";
        auto value = "John";
        auto json = format("{{\"", key, "\": \"", value, "\"}}");
        print("Result: '", json, "'");
    )");
    
    // Strategy 2: Start with empty string to force concatenation
    std::cout << "2. Force concatenation mode: ";
    engine.execute(R"(
        auto key = "name";
        auto value = "John";
        auto json = format("", "{\"", key, "\": \"", value, "\"}");
        print("Result: '", json, "'");
    )");
    
    // Strategy 3: Use format placeholders
    std::cout << "3. Use format placeholders: ";
    engine.execute(R"(
        auto key = "name";
        auto value = "John";
        auto json = format("{\"{}\": \"{}\"}", key, value);
        print("Result: '", json, "'");
    )");
    
    // Strategy 4: Build with concatenation
    std::cout << "4. Direct concatenation: ";
    engine.execute(R"(
        auto key = "name";
        auto value = "John";
        auto json = "{\"" + key + "\": \"" + value + "\"}";
        print("Result: '", json, "'");
    )");
    
    return 0;
}