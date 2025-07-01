#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    
    // Register the standard library
    jai::stdlib::register_all(engine);
    
    std::cout << "Testing format function:\n" << std::endl;
    
    // Test 1: Single value
    std::cout << "1. Single value: ";
    engine.execute(R"(
        auto s = format(42);
        print("Result: '", s, "'");
    )");
    
    // Test 2: Format string with placeholders
    std::cout << "2. Format string: ";
    engine.execute(R"(
        auto s = format("Hello, {}!", "World");
        print("Result: '", s, "'");
    )");
    
    // Test 3: Multiple args without format specifiers
    std::cout << "3. Sequential concatenation: ";
    engine.execute(R"(
        auto s = format("The answer is ", 42, " and pi is ", 3.14);
        print("Result: '", s, "'");
    )");
    
    // Test 4: Building complex strings
    std::cout << "4. Building complex strings: ";
    engine.execute(R"(
        auto name = "Alice";
        auto age = 30;
        auto city = "Seattle";
        
        // Using format string
        auto s1 = format("{} is {} years old and lives in {}", name, age, city);
        
        // Using concatenation
        auto s2 = format(name, " is ", age, " years old and lives in ", city);
        
        print("Format style: '", s1, "'");
        print("   Concat style: '", s2, "'");
    )");
    
    // Test 5: Format ignores control types
    std::cout << "5. Control types are ignored: ";
    engine.execute(R"(
        auto s = format("Hello", skip_newline, " World", skip_flush);
        print("Result: '", s, "'");
    )");
    
    // Test 6: Positional arguments
    std::cout << "6. Positional arguments: ";
    engine.execute(R"(
        auto s = format("{2} {0} {1}", "B", "C", "A");
        print("Result: '", s, "'");
    )");
    
    // Test 7: Practical use - building JSON
    std::cout << "7. Building JSON: ";
    engine.execute(R"(
        auto key = "name";
        auto value = "John Doe";
        auto json = format("{\"", key, "\": \"", value, "\"}");
        print("Result: '", json, "'");
    )");
    
    // Test 8: Escaped braces
    std::cout << "8. Escaped braces: ";
    engine.execute(R"(
        auto s = format("Use {{}} for placeholders in {}", "format strings");
        print("Result: '", s, "'");
    )");
    
    return 0;
}