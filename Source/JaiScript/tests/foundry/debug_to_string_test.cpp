#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

using namespace jai;
using namespace jai::foundry;

int main() {
    try {
        auto engine = engine::make();
        jai::stdlib::register_all(*engine);
        
        std::cout << "Testing to_string function registration...\n";
        
        // Test 1: Check if to_string function exists
        try {
            script_value result = engine->execute("type_of(to_string);");
            std::cout << "to_string type: " << result.as_string() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Error getting to_string type: " << e.what() << "\n";
        }
        
        // Test 2: Try calling to_string with an integer
        try {
            script_value result = engine->execute("to_string(42);");
            std::cout << "to_string(42) result: " << result.as_string() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Error calling to_string(42): " << e.what() << "\n";
        }
        
        // Test 3: Try string concatenation 
        try {
            script_value result = engine->execute("\"Number: \" + to_string(42);");
            std::cout << "String concat result: " << result.as_string() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Error with string concatenation: " << e.what() << "\n";
        }
        
        // Test 4: Check what functions are available
        try {
            script_value result = engine->execute("print(\"Testing print function\");");
            std::cout << "Print function test passed\n";
        } catch (const std::exception& e) {
            std::cout << "Error with print function: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "Overall error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}