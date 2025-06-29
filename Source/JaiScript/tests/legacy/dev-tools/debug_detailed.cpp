#include <iostream>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    engine eng;
    
    std::cout << "=== Testing map creation first ===\n";
    script_value map_result = eng.execute("var map = {{\"test\", 5}}; map");
    std::cout << "Map result is_null: " << map_result.is_null() << "\n";
    std::cout << "Map result is_map: " << map_result.is_map() << "\n";
    
    if (map_result.is_map()) {
        auto& map = map_result.as_map();
        std::cout << "Map size: " << map.size() << "\n";
        
        // Check if key exists
        script_value key("test");
        auto it = map.find(key);
        if (it != map.end()) {
            std::cout << "Key 'test' found, value: " << it->second.as<int>() << "\n";
        } else {
            std::cout << "Key 'test' NOT found\n";
        }
    }
    
    std::cout << "\n=== Testing map subscript ===\n";
    script_value subscript_result = eng.execute("var map = {{\"test\", 5}}; map[\"test\"]");
    std::cout << "Subscript result is_null: " << subscript_result.is_null() << "\n";
    if (!subscript_result.is_null()) {
        std::cout << "Subscript result value: " << subscript_result.as<int>() << "\n";
    }
    
    return 0;
}