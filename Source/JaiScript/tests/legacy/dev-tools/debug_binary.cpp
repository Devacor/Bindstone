#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        engine engine;
        stdlib::register_all(engine);
        
        std::cout << "=== Testing Basic Binary Serialization ===" << std::endl;
        
        // Test simple integer
        std::cout << "\n1. Testing integer 42:" << std::endl;
        script_value binary_result = engine.execute("to_binary(42)");
        std::cout << "   Result type: " << static_cast<int>(binary_result.type()) << std::endl;
        std::cout << "   Is string: " << binary_result.is_string() << std::endl;
        
        if (binary_result.is_string()) {
            std::string binary_str = binary_result.as<script_string>();
            std::cout << "   Binary length: " << binary_str.length() << " bytes" << std::endl;
            std::cout << "   First few bytes (hex): ";
            for (size_t i = 0; i < std::min(size_t(10), binary_str.length()); ++i) {
                printf("%02x ", static_cast<unsigned char>(binary_str[i]));
            }
            std::cout << std::endl;
            
            // Try to deserialize
            std::cout << "\n2. Testing from_binary(to_binary(42)):" << std::endl;
            engine.add_global("binary_data", binary_result);
            script_value restored = engine.execute("from_binary(binary_data)");
            std::cout << "   Restored type: " << static_cast<int>(restored.type()) << std::endl;
            if (restored.is_int()) {
                std::cout << "   Restored value: " << restored.as<script_int>() << std::endl;
            } else {
                std::cout << "   Restoration failed - not an integer" << std::endl;
            }
        } else {
            std::cout << "   ERROR: to_binary didn't return a string!" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}