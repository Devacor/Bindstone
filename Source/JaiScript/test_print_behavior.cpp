#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    
    // Register the standard library (includes new print function)
    jai::stdlib::register_all(engine);
    
    std::cout << "Testing enhanced print function:\n" << std::endl;
    
    // Test 1: Format string behavior (unchanged)
    std::cout << "1. Format string with {}: ";
    engine.execute(R"(print("Hello, {}!", "World"))");
    
    // Test 2: Multiple args without format specifiers
    std::cout << "2. Multiple args, no format: ";
    engine.execute(R"(print("Hello", " ", "World", "!"))");
    
    // Test 3: skip_newline
    std::cout << "3. With skip_newline: ";
    engine.execute(R"(print("No newline here", skip_newline))");
    std::cout << " <- See, no newline!" << std::endl;
    
    // Test 4: skip_flush
    std::cout << "4. With skip_flush: ";
    engine.execute(R"(print("No newline or flush", skip_flush))");
    std::cout << " <- Added manually" << std::endl;
    
    // Test 5: Multiple values with skip_newline
    std::cout << "5. Multiple values with skip_newline: ";
    engine.execute(R"(
        auto x = 42;
        auto y = 3.14;
        print("x=", x, " y=", y, skip_newline)
    )");
    std::cout << " <- Done" << std::endl;
    
    // Test 6: Format string with control arg
    std::cout << "6. Format string with skip_newline: ";
    engine.execute(R"(print("Value: {}", 123, skip_newline))");
    std::cout << " <- Done" << std::endl;
    
    // Test 7: Check that braces in non-format context work
    std::cout << "7. Braces without format intent: ";
    engine.execute(R"(print("JSON: ", "{value: 42}"))");
    
    return 0;
}