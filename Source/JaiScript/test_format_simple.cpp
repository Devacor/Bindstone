#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    jai::engine engine;
    jai::stdlib::register_all(engine);
    
    try {
        std::cout << "Testing format function exists:\n" << std::endl;
        
        // Test 1: Check if we can call format at all
        std::cout << "1. Calling format with empty args: ";
        auto result = engine.execute("format()");
        std::cout << "Success! Type: " << (result.is_string() ? "string" : "other") << std::endl;
        
        // Test 2: Simple format call
        std::cout << "2. Calling format('test'): ";
        result = engine.execute("format('test')");
        if (result.is_string()) {
            std::cout << "Result: '" << result.as<std::string>() << "'" << std::endl;
        } else {
            std::cout << "Not a string!" << std::endl;
        }
        
        // Test 3: Check other stdlib functions
        std::cout << "3. Calling print('test'): ";
        engine.execute("print('test')");
        
        std::cout << "4. Calling to_string(42): ";
        result = engine.execute("to_string(42)");
        if (result.is_string()) {
            std::cout << "Result: '" << result.as<std::string>() << "'" << std::endl;
        } else {
            std::cout << "Not a string!" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    
    return 0;
}