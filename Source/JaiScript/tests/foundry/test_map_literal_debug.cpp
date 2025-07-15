#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        // Test creating a map literal
        auto result = engine->execute(R"({"alice": 10, "bob": 20})");
        
        std::cout << "Map created: " << result.is_map() << std::endl;
        std::cout << "Map size: " << result.as_map().size() << std::endl;
        
        // Try to iterate over the map
        const auto& map = result.as_map();
        for (const auto& [key, value] : map) {
            std::cout << "Key type: " << static_cast<int>(key.type()) << std::endl;
            std::cout << "Key is_string: " << key.is_string() << std::endl;
            try {
                std::cout << "Key value: " << key.as_string() << std::endl;
            } catch (const std::exception& e) {
                std::cout << "Error getting key as string: " << e.what() << std::endl;
            }
            std::cout << "Value: " << value.as<int>() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
