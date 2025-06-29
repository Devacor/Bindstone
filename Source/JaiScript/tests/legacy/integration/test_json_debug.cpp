#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;

int main() {
    engine engine;
    stdlib::register_all(engine);
    
    try {
        std::cout << "Testing empty map..." << std::endl;
        script_value result = engine.execute("to_json({})");
        std::cout << "Result type: " << static_cast<int>(result.type()) << std::endl;
        std::cout << "Result: " << result.to_string() << std::endl;
        
        std::cout << "\nTesting simple map..." << std::endl;
        result = engine.execute("to_json({\"name\": \"John\"})");
        std::cout << "Result type: " << static_cast<int>(result.type()) << std::endl;
        if (result.is_string()) {
            std::cout << "Result string: " << result.as_string() << std::endl;
        }
        std::cout << "Result: " << result.to_string() << std::endl;
        
        // Test direct map creation
        std::cout << "\nTesting direct map..." << std::endl;
        result = engine.execute("var m = {\"name\": \"John\"}; m");
        std::cout << "Map type: " << static_cast<int>(result.type()) << std::endl;
        if (result.is_map()) {
            std::cout << "Map size: " << result.as_map().size() << std::endl;
            for (const auto& [k, v] : result.as_map()) {
                std::cout << "Key type: " << static_cast<int>(k.type()) << ", value type: " << static_cast<int>(v.type()) << std::endl;
                std::cout << "Key: " << k.to_string() << ", value: " << v.to_string() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cout << "exception: " << e.what() << std::endl;
    }
    
    return 0;
}