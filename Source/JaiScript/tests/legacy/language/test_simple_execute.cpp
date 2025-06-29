#include <iostream>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    engine engine;
    
    try {
        // Test simple expression
        std::cout << "Testing 2 + 2..." << std::endl;
        script_value result = engine.execute("2 + 2");
        std::cout << "Result: " << result.to_string() << std::endl;
        
        // Test variable
        std::cout << "\nTesting variable..." << std::endl;
        result = engine.execute("var x = 5; x");
        std::cout << "Result: " << result.to_string() << std::endl;
        
        // Test array
        std::cout << "\nTesting array..." << std::endl;
        result = engine.execute("[1, 2, 3]");
        std::cout << "Result type: " << static_cast<int>(result.type()) << std::endl;
        std::cout << "Result: " << result.to_string() << std::endl;
        
        // Test map with expression statement
        std::cout << "\nTesting map as expression..." << std::endl;
        result = engine.execute("{\"key\": \"value\"};");
        std::cout << "Result type: " << static_cast<int>(result.type()) << std::endl;
        std::cout << "Result: " << result.to_string() << std::endl;
        
        // Test map assigned to variable
        std::cout << "\nTesting map variable..." << std::endl;
        result = engine.execute("var m = {\"key\": \"value\"}");
        std::cout << "Result type after var: " << static_cast<int>(result.type()) << std::endl;
        
        // Test accessing the map
        result = engine.execute("var m2 = {\"key\": \"value\"}; m2");
        std::cout << "Result type: " << static_cast<int>(result.type()) << std::endl;
        std::cout << "Result: " << result.to_string() << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "exception: " << e.what() << std::endl;
    }
    
    return 0;
}