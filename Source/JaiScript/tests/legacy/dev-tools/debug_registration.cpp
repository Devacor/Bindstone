#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

using namespace jai;

int main() {
    try {
        engine engine;
        
        std::cout << "Testing function registration..." << std::endl;
        
        // Test if register_all works
        stdlib::register_all(engine);
        std::cout << "register_all() completed successfully" << std::endl;
        
        // Check if to_json works (it should)
        script_value json_result = engine.execute("to_json(42)");
        std::cout << "to_json(42) works: " << json_result.as<script_string>() << std::endl;
        
        // Check if our engine has the function registered
        bool has_to_binary = engine.has_function("to_binary");
        std::cout << "Engine has to_binary function: " << has_to_binary << std::endl;
        
        if (has_to_binary) {
            std::cout << "Attempting to call to_binary..." << std::endl;
            script_value binary_result = engine.execute("to_binary(42)");
            std::cout << "to_binary(42) succeeded!" << std::endl;
        } else {
            std::cout << "to_binary function not found in engine" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}