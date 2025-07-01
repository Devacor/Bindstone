#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    std::cout << "Testing format edge cases:\n" << std::endl;
    
    // Test 1: Building JSON-like string
    std::cout << "1. JSON building (should concatenate): ";
    engine.execute(R"(
        auto result = format("{\"name\": \"value\"}");
        print("Result: '", result, "'");
    )");
    
    // Test 2: Escaped braces with placeholder
    std::cout << "2. {{}} with placeholder (should be {} with replacement): ";
    engine.execute(R"(
        auto result = format("Use {{}} for placeholders in {}", "format strings");
        print("Result: '", result, "'");
    )");
    
    // Test 3: Multiple args starting with {
    std::cout << "3. Multiple args starting with { (should concatenate): ";
    engine.execute(R"(
        auto result = format("{\"", "key", "\": \"", "value", "\"}");
        print("Result: '", result, "'");
    )");
    
    // Test 4: Only escaped braces
    std::cout << "4. Only escaped braces (no format mode): ";
    engine.execute(R"(
        auto result = format("{{abc}}");
        print("Result: '", result, "'");
    )");
    
    // Test 5: Mixed escaped and unescaped
    std::cout << "5. Mixed escaped and unescaped: ";
    engine.execute(R"(
        auto result = format("{{}} has {} items", 5);
        print("Result: '", result, "'");
    )");
    
    return 0;
}